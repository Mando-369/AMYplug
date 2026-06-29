// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// PatchModel — the canonical, serializable description of the AMY state. This is
// the SINGLE SOURCE OF TRUTH. Both backends are projections of it:
//   * SoftwareBackend.rebuildFrom(model) resets libamy and replays toWireMessages()
//   * HardwareBackend.rebuildFrom(model) re-sends the same wire messages to the board
//
// Recall works because get/setStateInformation only ever (de)serialize this model
// (+ the APVTS); we never trust AMY's live internal state to survive a save.

#include <juce_data_structures/juce_data_structures.h>
#include <string>
#include <vector>

namespace amyplug
{
class PatchModel
{
public:
    PatchModel();

    // --- Canonical fields (extend as the editor grows) ---------------------
    // For the MVP a "patch" can be just a base AMY patch number + a handful of
    // macro edits. As the editor deepens, store the full per-osc graph here
    // (waves, envelopes as bp strings, mod routings, CtrlCoefs, effects).
    // Which sound engine the synth uses. FactoryPreset plays a baked AMY patch by
    // number (K<n>); Analog is our editable subtractive voice (the Juno tab) built
    // osc-by-osc from AnalogParams. (FM = the future DX7 editor, M3c.)
    enum class Engine { FactoryPreset, Analog, FM };

    // The editable analog (Juno-style) voice — a 4-oscillator subtractive synth that
    // mirrors AMY's "amyboard default" template: osc0 = VCF/VCA, osc1 = LFO,
    // osc2/osc3 = OSC A/OSC B. All values are the source of truth (fully recalled).
    struct AnalogParams
    {
        // OSC A (osc 2) / OSC B (osc 3) — note-following audio oscillators.
        int   aWave = 3,   bWave = 1;      // amy::Wave (SawUp, Pulse)
        float aFreq = 440.0f, bFreq = 440.0f; // Hz at A4 (note 69); the osc tracks
                                             // the keyboard from there. 440 = normal.
        float aDuty = 0.5f, bDuty = 0.5f;
        float aLevel = 0.7f, bLevel = 0.5f;
        // LFO (osc 1) — fixed low frequency (note-coef 0), modulates the others.
        int   lfoWave = 4;                 // Triangle
        float lfoFreq = 4.0f;              // Hz
        float lfoToPitch = 0.0f, lfoToPwm = 0.0f, lfoToFilter = 0.0f;
        // VCF (osc 0).
        int   filterType = 1;              // amy::Filter (LPF)
        float vcfFreq = 2500.0f, vcfReso = 0.7f, vcfKbd = 0.0f, vcfEnv = 0.0f;
        // VCF envelope (bp1) and amp envelope (bp0), seconds + sustain 0..1.
        float vcfA = 0.0f, vcfD = 0.6f, vcfS = 0.3f, vcfR = 0.4f;
        float ampA = 0.005f, ampD = 0.4f, ampS = 0.8f, ampR = 0.3f;
        float level = 0.8f;                // overall voice level (osc0 amp)
    };

    // The editable FM (DX7-style) voice — a 6-operator phase-modulation synth that
    // mirrors AMY's ALGO engine: osc0 is the ALGO controller (algorithm + feedback +
    // master envelope), oscs 1..6 are sine operators. Each operator outputs
    // level x its own envelope; pitch = note x ratio. All values are the source of
    // truth (fully recalled). The master amp envelope reuses Synth::ampA..ampR.
    struct FmOp
    {
        float ratio = 1.0f;       // operator freq = note freq x ratio
        float level = 0.0f;       // output amplitude / modulation index (0..4)
        float a = 0.005f, d = 0.3f, s = 0.7f, r = 0.4f;   // per-op envelope (seconds + sustain 0..1)
    };
    static constexpr int kFmOps = 6;
    struct FmParams
    {
        int   algorithm = 1;      // 1..32 (DX7 algorithms)
        float feedback  = 0.0f;   // 0..1, self-feedback on the ALGO osc
        FmOp  ops[kFmOps];
    };

    struct Synth
    {
        int  channel     = 1;     // 1..16 (== AMY synth)
        Engine engine    = Engine::FactoryPreset;
        int  patchNumber = 0;     // AMY patch (FactoryPreset)
        int  numVoices   = 6;

        AnalogParams analog;      // used when engine == Analog
        FmParams     fm;          // used when engine == FM

        // Automatable macros applied on top of a FACTORY patch (defaults mirror
        // Parameters.h). cutoff/reso broadcast to every voice via i<ch>F/R; the
        // amp ADSR becomes AMY breakpoint set bp0 (i<ch>A). See toWireMessages().
        float filterCutoff = 8000.0f; // Hz
        float filterReso   = 0.7f;
        float ampAttack    = 0.005f;  // seconds
        float ampDecay     = 0.1f;
        float ampSustain   = 0.7f;    // 0..1
        float ampRelease   = 0.25f;

        // User-patch wire commands (for synths built from scratch / edited).
        std::vector<std::string> oscWireCommands;
    };

    std::vector<Synth> synths { Synth {} };

    // Global effects + mix (mirrors Parameters.h). AMY's volume is unity at 10
    // (it scales by 0.1*volume), so 4.0 ~= -8 dB on a single voice.
    float masterVolume = 4.0f;
    float reverb = 0.0f, chorus = 0.0f, echo = 0.0f;
    float eqLow = 0.0f, eqMid = 0.0f, eqHigh = 0.0f;   // dB, AMY 3-band EQ ('x')

    // --- Rebuild + persistence --------------------------------------------
    // Ordered wire messages that recreate this exact state in a fresh AMY.
    // Begins with a full reset. Used by both backends' rebuildFrom().
    std::vector<std::string> toWireMessages() const;

    // (De)serialize to a ValueTree so it nests inside the plugin state blob.
    juce::ValueTree toValueTree() const;
    void            fromValueTree(const juce::ValueTree& tree);

    static constexpr const char* kStateType = "AMYplugPatch";
};
} // namespace amyplug
