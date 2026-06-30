// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "PatchModel.h"
#include "../engine/AmyWire.h"
#include <cmath>

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
//
// The amp ENVELOPE lives on the AUDIO oscs (osc2/osc3), NOT on the silent osc0
// "VCA". AMY frees the sound-producing oscs on note-off the instant they have no
// release envelope of their own, so an amp env parked on osc0 cuts the note dead
// (the chained audio is already gone before osc0's VCA can fade it). Giving the
// audio oscs the amp env (a<level>,0,0,<level>,0,0 + A<adsr>, the same const+eg0
// idiom the FM operators use) makes the release actually shape the tail. osc0
// keeps only the FILTER env (B). Same lesson as the DX7 release fix.
void emitAnalog(std::vector<std::string>& out, const PatchModel::Synth& s)
{
    const auto& a = s.analog;
    const juce::String pre = "i" + juce::String(s.channel);
    auto F = [] (float v) { return juce::String(v, 4); };
    const juce::String ampEnv = adsrBp(a.ampA, a.ampD, a.ampS, a.ampR);
    // Fold Coarse (semitones) + Fine (cents) into the osc freq const: a pitch offset
    // of (coarse + fine/100) semitones scales the base frequency by 2^(semis/12).
    auto tuned = [] (float baseHz, int coarse, int fine)
    { return baseHz * std::pow(2.0f, ((float) coarse + (float) fine / 100.0f) / 12.0f); };

    out.emplace_back((pre + "iv" + juce::String(juce::jlimit(1, 16, s.numVoices)) + "in4Z").toStdString());

    // osc 0 — filter only (+VCA passthrough). F = freq,kbd(idx1),,,env(idx4),lfo(idx5);
    // constant amp + velocity (a const,note,vel,...); B = filter env. NO amp env here.
    out.emplace_back((pre + "v0w20"
        + "G" + juce::String(a.filterType)
        + "F" + F(a.vcfFreq) + "," + F(a.vcfKbd) + ",,," + F(a.vcfEnv) + "," + F(a.lfoToFilter)
        + "R" + F(a.vcfReso)
        + "a1,0,1,0,0,0"
        + "B" + adsrBp(a.vcfA, a.vcfD, a.vcfS, a.vcfR)
        + "c2L1Z").toStdString());

    // osc 1 — LFO (note-coef 0 keeps it at lfoFreq regardless of the played note).
    // NOTE: this LFO is per-voice (each voice has its own osc 1), i.e. "Poly" mode.
    // TODO (M5 LFO modes): add Free (one shared global LFO), Key (retrigger on
    // note-on), and Sync (lock rate to host tempo). See ROADMAP M5.
    out.emplace_back((pre + "v1w" + juce::String(a.lfoWave) + "f" + F(a.lfoFreq) + ",0Z").toStdString());

    // osc 2 — OSC A. amp = level via const+eg0 coef shaped by its own amp env (A);
    // freq = tune(idx0), note tracking (idx1 default 1), lfoPitch(idx5); duty =
    // const,,,,,lfoPwm(idx5).
    out.emplace_back((pre + "v2w" + juce::String(a.aWave)
        + "a" + F(a.aLevel) + ",0,0," + F(a.aLevel) + ",0,0"
        + "A" + ampEnv
        + "d" + F(a.aDuty) + ",,,,," + F(a.lfoToPwm)
        + "f" + F(tuned(a.aFreq, a.aCoarse, a.aFine)) + ",,,,," + F(a.lfoToPitch)
        + "c3L1Z").toStdString());

    // osc 3 — OSC B.
    out.emplace_back((pre + "v3w" + juce::String(a.bWave)
        + "a" + F(a.bLevel) + ",0,0," + F(a.bLevel) + ",0,0"
        + "A" + ampEnv
        + "d" + F(a.bDuty) + ",,,,," + F(a.lfoToPwm)
        + "f" + F(tuned(a.bFreq, a.bCoarse, a.bFine)) + ",,,,," + F(a.lfoToPitch)
        + "L1Z").toStdString());
}

// Build the editable FM (DX7) voice osc-by-osc — AMY's ALGO engine. osc0 is the
// ALGO controller (wave ALGO=8): it tracks the note (f0,1), carries the algorithm
// (o<n>), the operator list (O1,2,3,4,5,6), feedback (b) and the master amp
// envelope (A). oscs 1..6 are sine operators: amp = level via the eg0 coef
// (a0,0,0,<level>,0,0) so each operator is shaped by its own bp0 envelope, and
// pitch = note x ratio (I<ratio>). Operators derive their frequency from the ALGO
// osc, so they take no note-tracking f of their own.
void emitFm(std::vector<std::string>& out, const PatchModel::Synth& s)
{
    const auto& fm = s.fm;
    const juce::String pre = "i" + juce::String(s.channel);
    auto F = [] (float v) { return juce::String(v, 4); };

    // 7 oscs per voice: 0 = ALGO controller, 1..6 = operators.
    out.emplace_back((pre + "iv" + juce::String(juce::jlimit(1, 16, s.numVoices)) + "in7Z").toStdString());

    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const auto& op = fm.ops[i];
        // amp = level x (1 + eg0): const + eg0 coef both = level, so the operator's
        // own A/D/S/R envelope shapes it and level 0 is truly silent. A pure-eg0 amp
        // (const 0) leaves the operator inaudible in AMY, hence the const term.
        out.emplace_back((pre + "v" + juce::String(i + 1) + "w0"
            + "a" + F(op.level) + ",0,0," + F(op.level) + ",0,0"
            + "I" + F(op.ratio)                      // freq = note x ratio
            + "A" + adsrBp(op.a, op.d, op.s, op.r)
            + "Z").toStdString());
    }

    // osc 0 — ALGO controller. CONSTANT amp (a const,note,vel,...): const 1 + vel,
    // NO amp envelope — the per-operator envelopes own the voice's shape AND its
    // release. (A master amp env here would gate the whole voice off before the
    // operators finish releasing, which breaks note-off tails.) Matches fm.py.
    // f0,1 = const 0, note-coef 1 → standard A4=440 tracking. AMY's algorithm table
    // lists operators 6→1 (algo_source[5] = DX7 operator 1), so we map oscs 6..1
    // into the O list to make our "OP 1" = DX7 operator 1.
    out.emplace_back((pre + "v0w8"
        + "f0,1"
        + "a1,0,1,0,0,0"
        + "b" + F(fm.feedback)
        + "O6,5,4,3,2,1"
        + "o" + juce::String(juce::jlimit(1, 32, fm.algorithm))
        + "Z").toStdString());
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
        if (s.engine == Engine::FM)
        {
            emitFm(out, s);
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

    // 3. Global mix + effects (bus 0). The effects take AMY's full parameter lists:
    //    reverb h<level,size,damp,xover>, chorus k<level,maxdelay,rate,depth>,
    //    echo M<level,time,maxdelay,feedback,tone>. Buffer-size params we don't
    //    expose are pinned to AMY's defaults (xover 3000, chorus maxdelay 320,
    //    echo maxdelay 743).
    auto F = [] (float v) { return juce::String(v, 4); };
    // Portamento (glide) — AMY-native, broadcast to every synth (all engines). i<ch>m<ms>.
    for (const auto& s : synths)
    { WireBuilder w; w.synth(s.channel).raw(("m" + juce::String(juce::roundToInt(glide))).toStdString().c_str());
      out.emplace_back(w.str()); }
    { WireBuilder w; w.volume(masterVolume); out.emplace_back(w.str()); }
    { WireBuilder w; w.raw("h").raw((F(reverb) + "," + F(reverbSize) + "," + F(reverbDamping) + ",3000").toStdString().c_str());
      out.emplace_back(w.str()); }
    { WireBuilder w; w.raw("k").raw((F(chorus) + ",320," + F(chorusRate) + "," + F(chorusDepth)).toStdString().c_str());
      out.emplace_back(w.str()); }
    { WireBuilder w; w.raw("M").raw((F(echo) + "," + F(echoTime) + ",743," + F(echoFeedback) + "," + F(echoTone)).toStdString().c_str());
      out.emplace_back(w.str()); }
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
    root.setProperty("reverbSize", reverbSize, nullptr);
    root.setProperty("reverbDamping", reverbDamping, nullptr);
    root.setProperty("chorusRate", chorusRate, nullptr);
    root.setProperty("chorusDepth", chorusDepth, nullptr);
    root.setProperty("echoTime", echoTime, nullptr);
    root.setProperty("echoFeedback", echoFeedback, nullptr);
    root.setProperty("echoTone", echoTone, nullptr);
    root.setProperty("clipDrive", clipDrive, nullptr);
    root.setProperty("bcFreq", bcFreq, nullptr);
    root.setProperty("bcBits", bcBits, nullptr);
    root.setProperty("outputGain", outputGain, nullptr);
    root.setProperty("glide", glide, nullptr);
    root.setProperty("voiceMode", voiceMode, nullptr);
    root.setProperty("unisonVoices", unisonVoices, nullptr);
    root.setProperty("unisonDetune", unisonDetune, nullptr);

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
            { "a_aCoarse", (float) a.aCoarse }, { "a_bCoarse", (float) a.bCoarse },
            { "a_aFine", (float) a.aFine }, { "a_bFine", (float) a.bFine },
            { "a_aDuty", a.aDuty }, { "a_bDuty", a.bDuty }, { "a_aLevel", a.aLevel }, { "a_bLevel", a.bLevel },
            { "a_lfoWave", (float) a.lfoWave }, { "a_lfoFreq", a.lfoFreq },
            { "a_lfoToPitch", a.lfoToPitch }, { "a_lfoToPwm", a.lfoToPwm }, { "a_lfoToFilter", a.lfoToFilter },
            { "a_filterType", (float) a.filterType }, { "a_vcfFreq", a.vcfFreq }, { "a_vcfReso", a.vcfReso },
            { "a_vcfKbd", a.vcfKbd }, { "a_vcfEnv", a.vcfEnv },
            { "a_vcfA", a.vcfA }, { "a_vcfD", a.vcfD }, { "a_vcfS", a.vcfS }, { "a_vcfR", a.vcfR },
            { "a_ampA", a.ampA }, { "a_ampD", a.ampD }, { "a_ampS", a.ampS }, { "a_ampR", a.ampR },
            { "a_level", a.level } })
            sv.setProperty(p.first, p.second, nullptr);
        const auto& fm = s.fm;
        sv.setProperty("fm_algorithm", fm.algorithm, nullptr);
        sv.setProperty("fm_feedback",  fm.feedback,  nullptr);
        for (int i = 0; i < kFmOps; ++i)
        {
            const auto& op = fm.ops[i];
            const juce::String k = "fm_op" + juce::String(i + 1) + "_";
            sv.setProperty(k + "ratio", op.ratio, nullptr);
            sv.setProperty(k + "level", op.level, nullptr);
            sv.setProperty(k + "a", op.a, nullptr);
            sv.setProperty(k + "d", op.d, nullptr);
            sv.setProperty(k + "s", op.s, nullptr);
            sv.setProperty(k + "r", op.r, nullptr);
        }
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
    reverbSize    = (float) tree.getProperty("reverbSize",    0.85);
    reverbDamping = (float) tree.getProperty("reverbDamping", 0.5);
    chorusRate    = (float) tree.getProperty("chorusRate",    0.5);
    chorusDepth   = (float) tree.getProperty("chorusDepth",   0.5);
    echoTime      = (float) tree.getProperty("echoTime",      500.0);
    echoFeedback  = (float) tree.getProperty("echoFeedback",  0.0);
    echoTone      = (float) tree.getProperty("echoTone",      0.0);
    clipDrive     = (float) tree.getProperty("clipDrive",     0.0);
    bcFreq        = (float) tree.getProperty("bcFreq",        48000.0);
    bcBits        = (float) tree.getProperty("bcBits",        16.0);
    outputGain    = (float) tree.getProperty("outputGain",    0.0);
    glide         = (float) tree.getProperty("glide",         0.0);
    voiceMode     = (int)   tree.getProperty("voiceMode",     0);
    unisonVoices  = (int)   tree.getProperty("unisonVoices",  1);
    unisonDetune  = (float) tree.getProperty("unisonDetune",  12.0);

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
        a.aCoarse = (int) sv.getProperty("a_aCoarse", a.aCoarse); a.bCoarse = (int) sv.getProperty("a_bCoarse", a.bCoarse);
        a.aFine = (int) sv.getProperty("a_aFine", a.aFine); a.bFine = (int) sv.getProperty("a_bFine", a.bFine);
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
        auto& fm = s.fm;
        fm.algorithm = (int)   sv.getProperty("fm_algorithm", fm.algorithm);
        fm.feedback  = (float) sv.getProperty("fm_feedback",  fm.feedback);
        for (int i = 0; i < kFmOps; ++i)
        {
            auto& op = fm.ops[i];
            const juce::String k = "fm_op" + juce::String(i + 1) + "_";
            op.ratio = (float) sv.getProperty(k + "ratio", op.ratio);
            op.level = (float) sv.getProperty(k + "level", op.level);
            op.a = (float) sv.getProperty(k + "a", op.a);
            op.d = (float) sv.getProperty(k + "d", op.d);
            op.s = (float) sv.getProperty(k + "s", op.s);
            op.r = (float) sv.getProperty(k + "r", op.r);
        }
        auto lines = juce::StringArray::fromLines(sv.getProperty("oscWire").toString());
        for (auto& l : lines) if (l.isNotEmpty()) s.oscWireCommands.push_back(l.toStdString());
        synths.push_back(std::move(s));
    }
    if (synths.empty()) synths.push_back(Synth {});
}
} // namespace amyplug
