// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "PatchModel.h"
#include "../engine/AmyWire.h"
#include "AnalogLfo.h"
#include "Dx7Envelope.h"
#include "Dx7Lfo.h"
#include "Dx7Osc.h"
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
// The unison detune offset (in cents) for copy k of U, spread symmetric about 0.
float unisonOffsetCents(int k, int U, float detune)
{
    if (U <= 1) return 0.0f;
    return -detune + 2.0f * detune * (float) k / (float) (U - 1);
}

void emitAnalog(std::vector<std::string>& out, const PatchModel::Synth& s,
                int unisonVoices, float unisonDetune)
{
    const auto& a = s.analog;
    const juce::String pre = "i" + juce::String(s.channel);
    auto F = [] (float v) { return juce::String(v, 4); };
    const juce::String ampEnv = adsrBp(a.ampA, a.ampD, a.ampS, a.ampR);
    // Fold Coarse (semitones) + Fine (cents) into the osc freq const: a pitch offset
    // of (coarse + fine/100) semitones scales the base frequency by 2^(semis/12).
    auto tuned = [] (float baseHz, int coarse, int fine)
    { return baseHz * std::pow(2.0f, ((float) coarse + (float) fine / 100.0f) / 12.0f); };

    // Unison: U detuned copies of the OSC A/B/C/D set, stacked as extra audio oscs
    // and chained into the VCF (osc0<-osc2<-osc3<-...<-oscLast). This is how AMY's
    // own factory "unison" patches fatten a voice. Subtle detune works here (unlike
    // note-fanning) because every copy is its own oscillator, not its own note.
    // Four base oscs (C/D usually silent) let the 4-DCO Juno factory patches load.
    const int U        = juce::jlimit(1, 4, unisonVoices);
    const int perCopy  = 4;                            // OSC A + OSC B + OSC C + OSC D
    const int numAudio = perCopy * U;
    const int oscs     = 2 + numAudio;                // + osc0 (VCF) and osc1 (LFO)

    out.emplace_back((pre + "iv" + juce::String(juce::jlimit(1, 16, s.numVoices))
        + "in" + juce::String(oscs) + "Z").toStdString());

    // osc 0 — filter only (+VCA passthrough). F = freq,kbd(idx1),,,env(idx4),lfo(idx5);
    // constant amp + velocity (a const,note,vel,...); B = filter env. NO amp env here.
    // Chains osc2 (the first audio osc); the audio oscs chain in a line below.
    out.emplace_back((pre + "v0w20"
        + "G" + juce::String(a.filterType)
        + "F" + F(a.vcfFreq) + "," + F(a.vcfKbd) + ",,," + F(a.vcfEnv) + "," + F(a.lfoToFilter)
        + "R" + F(a.vcfReso)
        + "a1,0,1,0,0,0"
        + "B" + adsrBp(a.vcfA, a.vcfD, a.vcfS, a.vcfR)
        + "c2L1Z").toStdString());

    // osc 1 — LFO (note-coef 0 keeps it at lfoFreq regardless of the played note).
    // Free/Sync phase-lock the per-voice LFO copies at build: a single P0 aligns
    // every voice's osc1, and identical freq off the same clock keeps them in
    // lockstep (one global LFO). Poly omits it (each copy free-runs); Key resets
    // phase per note-on from the processor. Sync's freq is corrected live from the
    // host tempo — lfoFreq is only the fallback baked here.
    {
        const bool phaseLock = (a.lfoMode == analoglfo::Free || a.lfoMode == analoglfo::Sync);
        juce::String lfo = pre + "v1w" + juce::String(a.lfoWave) + "f" + F(a.lfoFreq) + ",0";
        if (phaseLock) lfo += "P0";
        out.emplace_back((lfo + "Z").toStdString());
    }

    // Audio oscs (2..oscs-1): U copies of OSC A then OSC B, each detuned, chained.
    // amp = level via const+eg0 coef shaped by the amp env (A); freq folds tune +
    // unison detune; duty/freq carry the LFO mod-depth (idx5).
    for (int i = 0; i < numAudio; ++i)
    {
        const int   osc    = 2 + i;
        const int   copy   = i / perCopy;
        const int   which  = i % perCopy;             // 0=A, 1=B, 2=C, 3=D
        const float offCt  = unisonOffsetCents(copy, U, unisonDetune);
        int   wave; float level, duty, baseF; int coarse, fine;
        switch (which)
        {
            case 0:  wave=a.aWave; level=a.aLevel; duty=a.aDuty; baseF=a.aFreq; coarse=a.aCoarse; fine=a.aFine; break;
            case 1:  wave=a.bWave; level=a.bLevel; duty=a.bDuty; baseF=a.bFreq; coarse=a.bCoarse; fine=a.bFine; break;
            case 2:  wave=a.cWave; level=a.cLevel; duty=a.cDuty; baseF=a.cFreq; coarse=a.cCoarse; fine=a.cFine; break;
            default: wave=a.dWave; level=a.dLevel; duty=a.dDuty; baseF=a.dFreq; coarse=a.dCoarse; fine=a.dFine; break;
        }
        const float baseHz = tuned(baseF, coarse, fine);
        const float freq   = baseHz * std::pow(2.0f, offCt / 1200.0f);

        juce::String line = pre + "v" + juce::String(osc) + "w" + juce::String(wave)
            + "a" + F(level) + ",0,0," + F(level) + ",0,0"
            + "A" + ampEnv
            + "d" + F(duty) + ",,,,," + F(a.lfoToPwm)
            + "f" + F(freq) + ",,,,," + F(a.lfoToPitch);
        if (i < numAudio - 1) line += "c" + juce::String(osc + 1);   // chain the next
        line += "L1Z";
        out.emplace_back(line.toStdString());
    }
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

    // 8 oscs per voice (matches fm.py): 0 = ALGO controller, 1 = LFO, 2..7 = operators.
    out.emplace_back((pre + "iv" + juce::String(juce::jlimit(1, 16, s.numVoices)) + "in8Z").toStdString());

    // LFO tremolo depth (amp mod-coef), shared by every operator whose AMS > 0.
    const float ampLfo = (float) amyplug::dx7lfo::ampLfoAmp(fm.lfoAmd);

    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const auto& op = fm.ops[i];
        const int osc = i + 2;   // operators live on oscs 2..7 (osc 1 is the LFO)
        // Frequency from DX7 Coarse/Fine/Detune: a harmonic ratio, or an absolute Hz
        // in fixed mode. Fixed MUST zero the note coef (f<hz>,0) — else the operator
        // still key-tracks and a high note + high fixed Hz pushes log-freq past AMY's
        // wavetable range (out-of-bounds -> abort).
        const juce::String freq = op.fixedFreq
            ? ("f" + F((float) juce::jlimit(0.001, 20000.0,
                        amyplug::dx7osc::coarseFineFixedHz(op.coarse, op.fine, op.detune))) + ",0")
            : ("I" + F((float) amyplug::dx7osc::coarseFineRatio(op.coarse, op.fine, op.detune)));
        // amp coefs [const,note,vel,eg0,eg1,mod]: const = Output Level -> amplitude
        // (fm.py: 2*dx7level_to_linear, max 2.0); vel = Key Velocity Sensitivity/7 (AMY
        // scales amp by velocity in the log domain, coef 1 = full 60 dB, 0 = none); eg0
        // = 1 (the operator's own bp0 env is the modulation depth over time); mod = the
        // LFO tremolo depth when AMS > 0 (mod_source = osc 1). Env = the DX7 4R/4L EG.
        const float amp  = (float) amyplug::dx7osc::outputLevelToAmp(op.outputLevel);
        // NO velocity coef on operators. In AMY's ALGO engine every operator is an
        // algo_source with velocity 0 (only the ALGO controller receives note velocity),
        // so ANY vel coef here drives amp_combine to ~0 and the operator vanishes even at
        // full velocity. Velocity reaches the voice through the controller osc below
        // (a1,0,1,..) which scales the carriers -> velocity = loudness. Per-operator
        // velocity (DX7 KVS/brightness) is not reachable this way; fm.py drops it too.
        const float vel  = 0.0f;
        const float trem = (op.ampModSens > 0) ? ampLfo : 0.0f;
        const juce::String env = amyplug::dx7env::egToBreakpoints(op.egRate, op.egLevel);
        out.emplace_back((pre + "v" + juce::String(osc) + "w0"
            + "a" + F(amp) + ",0," + F(vel) + ",1,0," + F(trem)
            + "P0.25"                       // reset the operator phase on every note-on
                                            // (like fm.py/the factory bank). Without it
                                            // the operators free-run, so each note catches
                                            // them at a different phase -> the FM timbre
                                            // differs note-to-note ("drifts inconsistently").
            + freq
            + "A" + env
            + "L1"                          // mod_source = osc 1 (the LFO)
            + "Z").toStdString());
    }

    // osc 1 — the LFO. wave + speed (Hz); amp const 1, cosine phase like DX7 (fm.py).
    out.emplace_back((pre + "v1"
        + "w" + juce::String(amyplug::dx7lfo::lfoWaveToAmy(fm.lfoWave))
        + "a1"
        + "f" + F((float) amyplug::dx7lfo::lfoSpeedToHz(fm.lfoSpeed))
        + "P0.25"
        + "Z").toStdString());

    // osc 0 — ALGO controller. CONSTANT amp (a const,note,vel,...): const 1 + vel,
    // NO amp envelope — the per-operator envelopes own the voice's shape AND its
    // release. (A master amp env here would gate the whole voice off before the
    // operators finish releasing, which breaks note-off tails.) Matches fm.py.
    // AMY's algorithm table lists operators 6→1 (algo_source[5] = DX7 operator 1), so
    // we map oscs 6..1 into the O list to make our "OP 1" = DX7 operator 1.
    //
    // Frequency: fm.py sets freq = "0,1,0,1,0,.." — const 0, note-coef 1, AND an EG0
    // coef of 1 driven by a pitch envelope (bp0) whose DX7-centre value is 1.0. AMY
    // sums freq coefs linearly in log2 (combine_controls), so logfreq = note + 1.0 =
    // note + ONE OCTAVE. The whole voice (operators inherit this base) sits an octave
    // up; that is AMY's DX7 tuning. Omitting the EG0 term drops every FM voice an
    // octave below the built-in DX7 presets, so we emit a neutral pitch env held at
    // 1.0 (attack + release both stay at 1.0 — no pitch glide on note-off).
    // Pitch EG on bp0 (level 50 = ratio 1.0 = the +1 octave; non-flat sweeps around it).
    // freq mod-coef (index 5) = the LFO vibrato depth (pitch_lfo_amp from PMS+PMD),
    // mod_source = osc 1. O lists the operator oscs 7..2 (reverse) so DX7 OP1 == fm.ops[0].
    // NOTE: freq const is 0 (AMY reads the freq const as an absolute Hz via
    // logfreq_of_freq, NOT a log2 offset). Transpose is applied as a note shift in
    // NoteRouter instead (a keyboard transpose — fixed ops keep their pitch).
    const float pitchLfo = (float) amyplug::dx7lfo::pitchLfoAmp(fm.lfoPms, fm.lfoPmd);
    const juce::String pitchEnv = amyplug::dx7env::pitchEgToBreakpoints(fm.pitchEgRate, fm.pitchEgLevel);
    out.emplace_back((pre + "v0w8"
        + "f0,1,0,1,0," + F(pitchLfo)     // const 0, note 1, EG0 (pitch env) 1, LFO vibrato
        + "a1,0,1,0,0,0"
        + "A" + pitchEnv                 // DX7 pitch envelope (all-50 default = flat = +1 octave)
        + "b" + F(fm.feedback)
        + "L1"                           // mod_source = osc 1 (the LFO)
        + "O7,6,5,4,3,2"
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
            emitAnalog(out, s, unisonVoices, unisonDetune);
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
    // Portamento (glide). AMY glides a REUSED voice, so it only bites in Mono/Legato
    // (voiceMode != 0). Crucially it must sit on the oscs that GENERATE THE PITCH: on
    // the analog engine those are the audio oscs (2..), NOT the synth base (osc0 = the
    // VCF sink) — a synth-level `i<ch>m` would set portamento on the filter and the note
    // would never glide. FM's base osc IS the note-follower, so the synth-level form
    // works there (and for factory presets).
    const int glideMs = (voiceMode != 0) ? juce::roundToInt(glide) : 0;
    for (const auto& s : synths)
    {
        if (s.engine == Engine::Analog)
        {
            const int numAudio = 4 * juce::jlimit(1, 4, unisonVoices);   // OSC A/B/C/D x unison
            for (int osc = 2; osc < 2 + numAudio; ++osc)
            { WireBuilder w; w.synth(s.channel).raw(("v" + juce::String(osc) + "m" + juce::String(glideMs)).toStdString().c_str());
              out.emplace_back(w.str()); }
        }
        else
        { WireBuilder w; w.synth(s.channel).raw(("m" + juce::String(glideMs)).toStdString().c_str());
          out.emplace_back(w.str()); }
    }
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
            { "a_cWave", (float) a.cWave }, { "a_dWave", (float) a.dWave },
            { "a_aFreq", a.aFreq }, { "a_bFreq", a.bFreq }, { "a_cFreq", a.cFreq }, { "a_dFreq", a.dFreq },
            { "a_aCoarse", (float) a.aCoarse }, { "a_bCoarse", (float) a.bCoarse },
            { "a_cCoarse", (float) a.cCoarse }, { "a_dCoarse", (float) a.dCoarse },
            { "a_aFine", (float) a.aFine }, { "a_bFine", (float) a.bFine },
            { "a_cFine", (float) a.cFine }, { "a_dFine", (float) a.dFine },
            { "a_aDuty", a.aDuty }, { "a_bDuty", a.bDuty }, { "a_cDuty", a.cDuty }, { "a_dDuty", a.dDuty },
            { "a_aLevel", a.aLevel }, { "a_bLevel", a.bLevel }, { "a_cLevel", a.cLevel }, { "a_dLevel", a.dLevel },
            { "a_lfoWave", (float) a.lfoWave }, { "a_lfoFreq", a.lfoFreq },
            { "a_lfoToPitch", a.lfoToPitch }, { "a_lfoToPwm", a.lfoToPwm }, { "a_lfoToFilter", a.lfoToFilter },
            { "a_lfoMode", (float) a.lfoMode }, { "a_lfoSyncRate", (float) a.lfoSyncRate },
            { "a_filterType", (float) a.filterType }, { "a_vcfFreq", a.vcfFreq }, { "a_vcfReso", a.vcfReso },
            { "a_vcfKbd", a.vcfKbd }, { "a_vcfEnv", a.vcfEnv },
            { "a_vcfA", a.vcfA }, { "a_vcfD", a.vcfD }, { "a_vcfS", a.vcfS }, { "a_vcfR", a.vcfR },
            { "a_ampA", a.ampA }, { "a_ampD", a.ampD }, { "a_ampS", a.ampS }, { "a_ampR", a.ampR },
            { "a_level", a.level } })
            sv.setProperty(p.first, p.second, nullptr);
        const auto& fm = s.fm;
        sv.setProperty("fm_algorithm", fm.algorithm, nullptr);
        sv.setProperty("fm_feedback",  fm.feedback,  nullptr);
        for (int e = 0; e < 4; ++e)
        {
            sv.setProperty("fm_pitchr" + juce::String(e + 1), fm.pitchEgRate[e],  nullptr);
            sv.setProperty("fm_pitchl" + juce::String(e + 1), fm.pitchEgLevel[e], nullptr);
        }
        sv.setProperty("fm_lfoSpeed",   fm.lfoSpeed,   nullptr);
        sv.setProperty("fm_lfoWave",    fm.lfoWave,    nullptr);
        sv.setProperty("fm_lfoPmd",     fm.lfoPmd,     nullptr);
        sv.setProperty("fm_lfoAmd",     fm.lfoAmd,     nullptr);
        sv.setProperty("fm_lfoPms",     fm.lfoPms,     nullptr);
        sv.setProperty("fm_lfoDelay",   fm.lfoDelay,   nullptr);
        sv.setProperty("fm_lfoKeySync", fm.lfoKeySync, nullptr);
        sv.setProperty("fm_oscKeySync", fm.oscKeySync, nullptr);
        sv.setProperty("fm_transpose",  fm.transpose,  nullptr);
        for (int i = 0; i < kFmOps; ++i)
        {
            const auto& op = fm.ops[i];
            const juce::String k = "fm_op" + juce::String(i + 1) + "_";
            sv.setProperty(k + "coarse", op.coarse, nullptr);
            sv.setProperty(k + "fine",   op.fine,   nullptr);
            sv.setProperty(k + "detune", op.detune, nullptr);
            sv.setProperty(k + "outlvl", op.outputLevel, nullptr);
            for (int e = 0; e < 4; ++e)
            {
                sv.setProperty(k + "r" + juce::String(e + 1), op.egRate[e],  nullptr);
                sv.setProperty(k + "l" + juce::String(e + 1), op.egLevel[e], nullptr);
            }
            sv.setProperty(k + "fixed", op.fixedFreq, nullptr);
            sv.setProperty(k + "ams", op.ampModSens, nullptr);
            sv.setProperty(k + "vel", op.velSens, nullptr);
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
        a.cWave = (int) sv.getProperty("a_cWave", a.cWave);   a.dWave = (int) sv.getProperty("a_dWave", a.dWave);
        a.aFreq = (float) sv.getProperty("a_aFreq", a.aFreq); a.bFreq = (float) sv.getProperty("a_bFreq", a.bFreq);
        a.cFreq = (float) sv.getProperty("a_cFreq", a.cFreq); a.dFreq = (float) sv.getProperty("a_dFreq", a.dFreq);
        a.aCoarse = (int) sv.getProperty("a_aCoarse", a.aCoarse); a.bCoarse = (int) sv.getProperty("a_bCoarse", a.bCoarse);
        a.cCoarse = (int) sv.getProperty("a_cCoarse", a.cCoarse); a.dCoarse = (int) sv.getProperty("a_dCoarse", a.dCoarse);
        a.aFine = (int) sv.getProperty("a_aFine", a.aFine); a.bFine = (int) sv.getProperty("a_bFine", a.bFine);
        a.cFine = (int) sv.getProperty("a_cFine", a.cFine); a.dFine = (int) sv.getProperty("a_dFine", a.dFine);
        a.aDuty = (float) sv.getProperty("a_aDuty", a.aDuty); a.bDuty = (float) sv.getProperty("a_bDuty", a.bDuty);
        a.cDuty = (float) sv.getProperty("a_cDuty", a.cDuty); a.dDuty = (float) sv.getProperty("a_dDuty", a.dDuty);
        a.aLevel = (float) sv.getProperty("a_aLevel", a.aLevel); a.bLevel = (float) sv.getProperty("a_bLevel", a.bLevel);
        a.cLevel = (float) sv.getProperty("a_cLevel", a.cLevel); a.dLevel = (float) sv.getProperty("a_dLevel", a.dLevel);
        a.lfoWave = (int) sv.getProperty("a_lfoWave", a.lfoWave); a.lfoFreq = (float) sv.getProperty("a_lfoFreq", a.lfoFreq);
        a.lfoToPitch = (float) sv.getProperty("a_lfoToPitch", a.lfoToPitch);
        a.lfoToPwm = (float) sv.getProperty("a_lfoToPwm", a.lfoToPwm);
        a.lfoToFilter = (float) sv.getProperty("a_lfoToFilter", a.lfoToFilter);
        a.lfoMode = (int) sv.getProperty("a_lfoMode", a.lfoMode);
        a.lfoSyncRate = (int) sv.getProperty("a_lfoSyncRate", a.lfoSyncRate);
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
        for (int e = 0; e < 4; ++e)
        {
            fm.pitchEgRate[e]  = (float) sv.getProperty("fm_pitchr" + juce::String(e + 1), fm.pitchEgRate[e]);
            fm.pitchEgLevel[e] = (float) sv.getProperty("fm_pitchl" + juce::String(e + 1), fm.pitchEgLevel[e]);
        }
        fm.lfoSpeed   = (float) sv.getProperty("fm_lfoSpeed",   fm.lfoSpeed);
        fm.lfoWave    = (int)   sv.getProperty("fm_lfoWave",    fm.lfoWave);
        fm.lfoPmd     = (float) sv.getProperty("fm_lfoPmd",     fm.lfoPmd);
        fm.lfoAmd     = (float) sv.getProperty("fm_lfoAmd",     fm.lfoAmd);
        fm.lfoPms     = (int)   sv.getProperty("fm_lfoPms",     fm.lfoPms);
        fm.lfoDelay   = (float) sv.getProperty("fm_lfoDelay",   fm.lfoDelay);
        fm.lfoKeySync = (int)   sv.getProperty("fm_lfoKeySync", fm.lfoKeySync);
        fm.oscKeySync = (int)   sv.getProperty("fm_oscKeySync", fm.oscKeySync);
        fm.transpose  = (int)   sv.getProperty("fm_transpose",  fm.transpose);
        for (int i = 0; i < kFmOps; ++i)
        {
            auto& op = fm.ops[i];
            const juce::String k = "fm_op" + juce::String(i + 1) + "_";
            op.coarse      = (int)  sv.getProperty(k + "coarse", op.coarse);
            op.fine        = (int)  sv.getProperty(k + "fine",   op.fine);
            op.detune      = (int)  sv.getProperty(k + "detune", op.detune);
            op.outputLevel = (int)  sv.getProperty(k + "outlvl", op.outputLevel);
            for (int e = 0; e < 4; ++e)
            {
                op.egRate[e]  = (float) sv.getProperty(k + "r" + juce::String(e + 1), op.egRate[e]);
                op.egLevel[e] = (float) sv.getProperty(k + "l" + juce::String(e + 1), op.egLevel[e]);
            }
            op.fixedFreq   = (bool)  sv.getProperty(k + "fixed", op.fixedFreq);
            op.ampModSens  = (int)   sv.getProperty(k + "ams", op.ampModSens);
            op.velSens     = (int)   sv.getProperty(k + "vel", op.velSens);
        }
        auto lines = juce::StringArray::fromLines(sv.getProperty("oscWire").toString());
        for (auto& l : lines) if (l.isNotEmpty()) s.oscWireCommands.push_back(l.toStdString());
        synths.push_back(std::move(s));
    }
    if (synths.empty()) synths.push_back(Synth {});
}
} // namespace amyplug
