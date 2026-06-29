// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "PatchModel.h"
#include "../engine/AmyWire.h"

namespace amyplug
{
PatchModel::PatchModel() = default;

namespace
{
// Patch numbers AMY can actually load: built-in banks 0..256 (Juno/DX7/piano) and
// the user-patch slots 1024..1055. Anything else (the 257..1023 gap, empty user
// slots out of range) makes AMY read garbage osc counts and crash, so we never
// emit a load for them — fall back to Juno patch 0.
bool isLoadablePatch(int p) { return (p >= 0 && p <= 257) || (p >= 1024 && p <= 1055); }

// The amp-ADSR macro writes breakpoint set bp0 (eg0). That is only the AMPLITUDE
// envelope on the Juno analog engine (0..127); on DX7 (128..255) bp0/eg0 drives the
// carrier's PITCH envelope (freq coef references eg0), so overriding it wrecks the
// note's pitch at onset. Until the per-engine editor can target the right envelope,
// only apply the ADSR macro for Juno patches.
bool ampEnvIsBp0(int patchNumber) { return patchNumber >= 0 && patchNumber <= 127; }

// AMY envelope as a breakpoint set: "attackMs,1,decayMs,sustain,releaseMs,0".
// Time/value pairs; the final pair is the release, triggered on note-off.
juce::String adsrBp(float attack, float decay, float sustain, float release)
{
    auto ms = [] (float sec) { return juce::String(juce::roundToInt(sec * 1000.0f)); };
    return ms(attack) + ",1," + ms(decay) + "," + juce::String(sustain, 3) + "," + ms(release) + ",0";
}
std::string adsrToBp0(const PatchModel::Synth& s)
{
    return adsrBp(s.ampAttack, s.ampDecay, s.ampSustain, s.ampRelease).toStdString();
}

// Build the editable analog (Juno) voice osc-by-osc — the "amyboard default"
// template: osc0 VCF/VCA, osc1 LFO (note-coef 0 so it stays low-freq), osc2/osc3
// OSC A/OSC B (note-following), chained osc0<-osc2<-osc3, LFO as mod source (L1).
void emitAnalog(std::vector<std::string>& out, const PatchModel::Synth& s)
{
    const auto& a = s.analog;
    const juce::String pre = "i" + juce::String(s.channel);
    auto F = [] (float v) { return juce::String(v, 4); };

    out.emplace_back((pre + "iv" + juce::String(juce::jlimit(1, 16, s.numVoices)) + "in4Z").toStdString());

    // osc 0 — filter + VCA. F = freq,kbd(idx1),,,env(idx4),lfo(idx5); A=amp env, B=filter env.
    out.emplace_back((pre + "v0w20"
        + "G" + juce::String(a.filterType)
        + "F" + F(a.vcfFreq) + "," + F(a.vcfKbd) + ",,," + F(a.vcfEnv) + "," + F(a.lfoToFilter)
        + "R" + F(a.vcfReso)
        + "A" + adsrBp(a.ampA, a.ampD, a.ampS, a.ampR)
        + "B" + adsrBp(a.vcfA, a.vcfD, a.vcfS, a.vcfR)
        + "c2L1Z").toStdString());

    // osc 1 — LFO (note-coef 0 keeps it at lfoFreq regardless of the played note).
    // NOTE: this LFO is per-voice (each voice has its own osc 1), i.e. "Poly" mode.
    // TODO (M5 LFO modes): add Free (one shared global LFO), Key (retrigger on
    // note-on), and Sync (lock rate to host tempo). See ROADMAP M5.
    out.emplace_back((pre + "v1w" + juce::String(a.lfoWave) + "f" + F(a.lfoFreq) + ",0Z").toStdString());

    // osc 2 — OSC A. freq = tune(idx0, octaves), note tracking (idx1 default 1),
    // lfoPitch(idx5); duty = const,,,,,lfoPwm(idx5).
    out.emplace_back((pre + "v2w" + juce::String(a.aWave)
        + "a" + F(a.aLevel)
        + "d" + F(a.aDuty) + ",,,,," + F(a.lfoToPwm)
        + "f" + F(a.aFreq) + ",,,,," + F(a.lfoToPitch)
        + "c3L1Z").toStdString());

    // osc 3 — OSC B.
    out.emplace_back((pre + "v3w" + juce::String(a.bWave)
        + "a" + F(a.bLevel)
        + "d" + F(a.bDuty) + ",,,,," + F(a.lfoToPwm)
        + "f" + F(a.bFreq) + ",,,,," + F(a.lfoToPitch)
        + "L1Z").toStdString());
}
} // namespace

std::vector<std::string> PatchModel::toWireMessages() const
{
    std::vector<std::string> out;

    // 1. Release all synths/voices/oscs so we start from a known state. We use
    //    RESET_SYNTHS, NOT RESET_AMY: RESET_AMY does amy_stop()+amy_start() (frees
    //    and reallocates every global buffer) immediately on the calling thread —
    //    catastrophic when this runs on the audio thread during a patch change.
    //    RESET_SYNTHS just tears down synths prior to re-loading them.
    {
        WireBuilder w; w.reset(amy::Reset::Synths);
        out.emplace_back(w.str());
    }

    // 2. Recreate each synth. Analog = our editable osc graph; FactoryPreset = a
    //    baked AMY patch by number plus the broadcast macros.
    for (const auto& s : synths)
    {
        if (s.engine == Engine::Analog)
        {
            emitAnalog(out, s);
            continue;
        }

        const int patch  = isLoadablePatch(s.patchNumber) ? s.patchNumber : 0;
        const int voices = juce::jlimit(1, 16, s.numVoices);
        { WireBuilder w; w.synth(s.channel).numVoices(voices).patch(patch);
          out.emplace_back(w.str()); }

        for (const auto& cmd : s.oscWireCommands)
            out.emplace_back(cmd); // already-formed wire strings for edited oscs

        { WireBuilder w; w.synth(s.channel).filterFreq(s.filterCutoff); out.emplace_back(w.str()); }
        { WireBuilder w; w.synth(s.channel).resonance(s.filterReso);    out.emplace_back(w.str()); }
        if (ampEnvIsBp0(patch))   // skip on DX7 etc., where bp0 is the pitch envelope
            { WireBuilder w; w.synth(s.channel).bp0(adsrToBp0(s).c_str()); out.emplace_back(w.str()); }
    }

    // 3. Global mix + effects (bus 0).
    { WireBuilder w; w.volume(masterVolume); out.emplace_back(w.str()); }
    { WireBuilder w; w.reverb(reverb);       out.emplace_back(w.str()); }
    { WireBuilder w; w.chorus(chorus);       out.emplace_back(w.str()); }
    { WireBuilder w; w.echo(echo);           out.emplace_back(w.str()); }
    { WireBuilder w; w.raw("x")
        .raw((juce::String(eqLow,2) + "," + juce::String(eqMid,2) + "," + juce::String(eqHigh,2)).toStdString().c_str());
      out.emplace_back(w.str()); }

    return out;
}

juce::ValueTree PatchModel::toValueTree() const
{
    juce::ValueTree root { kStateType };
    root.setProperty("masterVolume", masterVolume, nullptr);
    root.setProperty("reverb", reverb, nullptr);
    root.setProperty("chorus", chorus, nullptr);
    root.setProperty("echo",   echo,   nullptr);
    root.setProperty("eqLow",  eqLow,  nullptr);
    root.setProperty("eqMid",  eqMid,  nullptr);
    root.setProperty("eqHigh", eqHigh, nullptr);

    for (const auto& s : synths)
    {
        juce::ValueTree sv { "Synth" };
        sv.setProperty("channel",     s.channel,     nullptr);
        sv.setProperty("engine",      (int) s.engine, nullptr);
        sv.setProperty("patchNumber", s.patchNumber, nullptr);
        sv.setProperty("numVoices",   s.numVoices,   nullptr);
        sv.setProperty("filterCutoff", s.filterCutoff, nullptr);
        sv.setProperty("filterReso",   s.filterReso,   nullptr);
        sv.setProperty("ampAttack",    s.ampAttack,    nullptr);
        sv.setProperty("ampDecay",     s.ampDecay,     nullptr);
        sv.setProperty("ampSustain",   s.ampSustain,   nullptr);
        sv.setProperty("ampRelease",   s.ampRelease,   nullptr);
        const auto& a = s.analog;
        for (auto p : { std::pair<const char*, float>
            { "a_aWave", (float) a.aWave }, { "a_bWave", (float) a.bWave },
            { "a_aFreq", a.aFreq }, { "a_bFreq", a.bFreq },
            { "a_aDuty", a.aDuty }, { "a_bDuty", a.bDuty }, { "a_aLevel", a.aLevel }, { "a_bLevel", a.bLevel },
            { "a_lfoWave", (float) a.lfoWave }, { "a_lfoFreq", a.lfoFreq },
            { "a_lfoToPitch", a.lfoToPitch }, { "a_lfoToPwm", a.lfoToPwm }, { "a_lfoToFilter", a.lfoToFilter },
            { "a_filterType", (float) a.filterType }, { "a_vcfFreq", a.vcfFreq }, { "a_vcfReso", a.vcfReso },
            { "a_vcfKbd", a.vcfKbd }, { "a_vcfEnv", a.vcfEnv },
            { "a_vcfA", a.vcfA }, { "a_vcfD", a.vcfD }, { "a_vcfS", a.vcfS }, { "a_vcfR", a.vcfR },
            { "a_ampA", a.ampA }, { "a_ampD", a.ampD }, { "a_ampS", a.ampS }, { "a_ampR", a.ampR },
            { "a_level", a.level } })
            sv.setProperty(p.first, p.second, nullptr);
        juce::String joined;
        for (const auto& c : s.oscWireCommands) joined << juce::String(c) << "\n";
        sv.setProperty("oscWire", joined, nullptr);
        root.addChild(sv, -1, nullptr);
    }
    return root;
}

void PatchModel::fromValueTree(const juce::ValueTree& tree)
{
    if (! tree.hasType(kStateType)) return;
    masterVolume = (float) tree.getProperty("masterVolume", 1.0);
    reverb = (float) tree.getProperty("reverb", 0.0);
    chorus = (float) tree.getProperty("chorus", 0.0);
    echo   = (float) tree.getProperty("echo",   0.0);
    eqLow  = (float) tree.getProperty("eqLow",  0.0);
    eqMid  = (float) tree.getProperty("eqMid",  0.0);
    eqHigh = (float) tree.getProperty("eqHigh", 0.0);

    synths.clear();
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto sv = tree.getChild(i);
        if (! sv.hasType("Synth")) continue;
        Synth s;
        s.channel     = (int) sv.getProperty("channel", 1);
        s.engine      = (Engine) (int) sv.getProperty("engine", (int) Engine::FactoryPreset);
        s.patchNumber = (int) sv.getProperty("patchNumber", 0);
        s.numVoices   = (int) sv.getProperty("numVoices", 6);
        s.filterCutoff = (float) sv.getProperty("filterCutoff", 8000.0);
        s.filterReso   = (float) sv.getProperty("filterReso",   0.7);
        s.ampAttack    = (float) sv.getProperty("ampAttack",    0.005);
        s.ampDecay     = (float) sv.getProperty("ampDecay",     0.1);
        s.ampSustain   = (float) sv.getProperty("ampSustain",   0.7);
        s.ampRelease   = (float) sv.getProperty("ampRelease",   0.25);
        auto& a = s.analog;
        a.aWave = (int) sv.getProperty("a_aWave", a.aWave);   a.bWave = (int) sv.getProperty("a_bWave", a.bWave);
        a.aFreq = (float) sv.getProperty("a_aFreq", a.aFreq); a.bFreq = (float) sv.getProperty("a_bFreq", a.bFreq);
        a.aDuty = (float) sv.getProperty("a_aDuty", a.aDuty); a.bDuty = (float) sv.getProperty("a_bDuty", a.bDuty);
        a.aLevel = (float) sv.getProperty("a_aLevel", a.aLevel); a.bLevel = (float) sv.getProperty("a_bLevel", a.bLevel);
        a.lfoWave = (int) sv.getProperty("a_lfoWave", a.lfoWave); a.lfoFreq = (float) sv.getProperty("a_lfoFreq", a.lfoFreq);
        a.lfoToPitch = (float) sv.getProperty("a_lfoToPitch", a.lfoToPitch);
        a.lfoToPwm = (float) sv.getProperty("a_lfoToPwm", a.lfoToPwm);
        a.lfoToFilter = (float) sv.getProperty("a_lfoToFilter", a.lfoToFilter);
        a.filterType = (int) sv.getProperty("a_filterType", a.filterType);
        a.vcfFreq = (float) sv.getProperty("a_vcfFreq", a.vcfFreq); a.vcfReso = (float) sv.getProperty("a_vcfReso", a.vcfReso);
        a.vcfKbd = (float) sv.getProperty("a_vcfKbd", a.vcfKbd); a.vcfEnv = (float) sv.getProperty("a_vcfEnv", a.vcfEnv);
        a.vcfA = (float) sv.getProperty("a_vcfA", a.vcfA); a.vcfD = (float) sv.getProperty("a_vcfD", a.vcfD);
        a.vcfS = (float) sv.getProperty("a_vcfS", a.vcfS); a.vcfR = (float) sv.getProperty("a_vcfR", a.vcfR);
        a.ampA = (float) sv.getProperty("a_ampA", a.ampA); a.ampD = (float) sv.getProperty("a_ampD", a.ampD);
        a.ampS = (float) sv.getProperty("a_ampS", a.ampS); a.ampR = (float) sv.getProperty("a_ampR", a.ampR);
        a.level = (float) sv.getProperty("a_level", a.level);
        auto lines = juce::StringArray::fromLines(sv.getProperty("oscWire").toString());
        for (auto& l : lines) if (l.isNotEmpty()) s.oscWireCommands.push_back(l.toStdString());
        synths.push_back(std::move(s));
    }
    if (synths.empty()) synths.push_back(Synth {});
}
} // namespace amyplug
