// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// Parameters.h — the host-automatable parameter layout (JUCE APVTS).
//
// RULE: every parameter here must also be reflected into PatchModel and the AMY
// rebuild path, or recall/automation will desync. Keep IDs stable forever (the
// host stores them in projects) — never renumber, only append.

#include <juce_audio_processors/juce_audio_processors.h>
#include "FmAlgorithms.h"

namespace amyplug::params
{
// Stable string IDs (do not change once shipped).
namespace id
{
    inline constexpr auto mode          = "mode";          // 0 = Software, 1 = Hardware
    inline constexpr auto masterVolume  = "master_volume"; // 0..10 (AMY "Synth Vol", upstream)
    inline constexpr auto outputGain    = "output_gain";   // final JUCE-side output gain, dB
    inline constexpr auto patchA        = "patch_a";       // active patch number on synth 1
    inline constexpr auto numVoices     = "num_voices";    // polyphony
    inline constexpr auto filterCutoff  = "filter_cutoff";
    inline constexpr auto filterReso    = "filter_reso";
    inline constexpr auto ampAttack     = "amp_attack";
    inline constexpr auto ampDecay      = "amp_decay";
    inline constexpr auto ampSustain    = "amp_sustain";
    inline constexpr auto ampRelease    = "amp_release";
    inline constexpr auto reverb        = "reverb_level";
    inline constexpr auto chorus        = "chorus_level";
    inline constexpr auto echo          = "echo_level";
    // Deeper per-effect controls (the rest of AMY's h/k/M parameter lists).
    inline constexpr auto reverbSize    = "reverb_size";     // liveness (decay/size)
    inline constexpr auto reverbDamping = "reverb_damping";
    inline constexpr auto chorusRate    = "chorus_rate";     // LFO freq (Hz)
    inline constexpr auto chorusDepth   = "chorus_depth";
    inline constexpr auto echoTime      = "echo_time";       // delay (ms)
    inline constexpr auto echoFeedback  = "echo_feedback";
    inline constexpr auto echoTone      = "echo_tone";       // feedback filter coef
    inline constexpr auto clipDrive     = "clip_drive";      // WDF saturator drive (THD), dB
    inline constexpr auto bcFreq        = "bc_freq";         // bitcrusher downsample target (Hz)
    inline constexpr auto bcBits        = "bc_bits";         // bitcrusher bit depth (2..16)
    inline constexpr auto pitchBendRange= "pitch_bend_range";
    // Performance / voicing (all engines). Glide is AMY-native portamento (i<synth>m);
    // voice mode + unison are handled in NoteRouter.
    inline constexpr auto glide         = "glide";          // portamento, ms
    inline constexpr auto voiceMode     = "voice_mode";     // 0 Poly, 1 Mono, 2 Legato
    inline constexpr auto unisonVoices  = "unison_voices";  // stacked detuned voices per note
    inline constexpr auto unisonDetune  = "unison_detune";  // spread, cents

    // --- Analog (Juno) engine, M3b. filterCutoff/filterReso/ampADSR/masterVolume
    //     above are reused as VCF freq/reso, amp env and level. ---
    inline constexpr auto engine        = "engine";        // 0 = Factory preset, 1 = Analog
    inline constexpr auto oscAWave      = "osc_a_wave";
    inline constexpr auto oscAFreq      = "osc_a_freq";
    inline constexpr auto oscACoarse    = "osc_a_coarse";   // ±24 semitones
    inline constexpr auto oscAFine      = "osc_a_fine";     // ±100 cents
    inline constexpr auto oscADuty      = "osc_a_duty";
    inline constexpr auto oscALevel     = "osc_a_level";
    inline constexpr auto oscBWave      = "osc_b_wave";
    inline constexpr auto oscBFreq      = "osc_b_freq";
    inline constexpr auto oscBCoarse    = "osc_b_coarse";
    inline constexpr auto oscBFine      = "osc_b_fine";
    inline constexpr auto oscBDuty      = "osc_b_duty";
    inline constexpr auto oscBLevel     = "osc_b_level";
    inline constexpr auto oscCWave      = "osc_c_wave";
    inline constexpr auto oscCFreq      = "osc_c_freq";
    inline constexpr auto oscCCoarse    = "osc_c_coarse";
    inline constexpr auto oscCFine      = "osc_c_fine";
    inline constexpr auto oscCDuty      = "osc_c_duty";
    inline constexpr auto oscCLevel     = "osc_c_level";
    inline constexpr auto oscDWave      = "osc_d_wave";
    inline constexpr auto oscDFreq      = "osc_d_freq";
    inline constexpr auto oscDCoarse    = "osc_d_coarse";
    inline constexpr auto oscDFine      = "osc_d_fine";
    inline constexpr auto oscDDuty      = "osc_d_duty";
    inline constexpr auto oscDLevel     = "osc_d_level";
    inline constexpr auto lfoWave       = "lfo_wave";
    inline constexpr auto lfoFreq       = "lfo_freq";
    inline constexpr auto lfoToPitch    = "lfo_to_pitch";
    inline constexpr auto lfoToPwm      = "lfo_to_pwm";
    inline constexpr auto lfoToFilter   = "lfo_to_filter";
    inline constexpr auto vcfType       = "vcf_type";
    inline constexpr auto vcfKbd        = "vcf_kbd";
    inline constexpr auto vcfEnv        = "vcf_env";
    inline constexpr auto vcfAttack     = "vcf_attack";
    inline constexpr auto vcfDecay      = "vcf_decay";
    inline constexpr auto vcfSustain    = "vcf_sustain";
    inline constexpr auto vcfRelease    = "vcf_release";
    inline constexpr auto eqLow         = "eq_low";
    inline constexpr auto eqMid         = "eq_mid";
    inline constexpr auto eqHigh        = "eq_high";

    // --- FM (DX7) engine, M3c. A 6-operator voice: an ALGO controller osc plus
    //     6 sine operators. The amp ADSR (ampAttack..) above is reused as the
    //     master output envelope on the ALGO osc. ---
    inline constexpr auto fmAlgorithm   = "fm_algorithm";   // 1..32 (DX7 algorithms)
    inline constexpr auto fmFeedback    = "fm_feedback";    // 0..1 on the ALGO osc

    // Per-operator ids are generated (op = 1..6). field = ratio|level|attack|decay|sustain|release.
    inline juce::String fmOp(int op, const char* field)
    {
        return juce::String("fm_op") + juce::String(op) + "_" + field;
    }
}

// How many FM operators the DX7 engine exposes (oscs 1..6; osc 0 is the ALGO ctrl).
inline constexpr int kFmOps = 6;

// Builds the parameter layout. Implemented inline so the processor can call it
// from its initializer list.
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { id::mode, 1 }, "Mode", StringArray { "Software", "Hardware" }, 0));

    // AMY treats volume=10 as unity (it scales output by 0.1*volume), so the old
    // default of 1.0 ran ~20 dB down. 4.0 puts a single voice near -8 dB (healthy);
    // very dense chords can still approach full scale (AMY clips internally there).
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { id::masterVolume, 1 }, "Synth Vol",
        NormalisableRange<float> { 0.0f, 10.0f, 0.001f }, 4.0f));

    // Final output gain — a true JUCE-side gain at the very END of the chain
    // (after bitcrusher + saturator), since AMY's "Synth Vol" is applied upstream
    // inside the engine and can't be moved to the end.
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { id::outputGain, 1 }, "Output Gain",
        NormalisableRange<float> { -60.0f, 12.0f, 0.1f }, 0.0f));

    // Built-in banks: Juno 0..127, DX7 128..255, piano 256, amyboard-default 257
    // (patch_commands[258] in AMY's patches.h). Keeping the range to loadable
    // patches stops host parameter-fuzzing from asking AMY for the undefined
    // 258..1023 gap (which crashes it). User patches are presets, not patch slots.
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID { id::patchA, 1 }, "Patch", 0, 257, 0));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID { id::numVoices, 1 }, "Voices", 1, 16, 6));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { id::filterCutoff, 1 }, "Filter Cutoff",
        NormalisableRange<float> { 20.0f, 18000.0f, 0.0f, 0.3f }, 8000.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { id::filterReso, 1 }, "Filter Resonance",
        NormalisableRange<float> { 0.5f, 16.0f, 0.0f, 0.4f }, 0.7f));

    auto sec = [] (float v) { return NormalisableRange<float> { 0.0f, 10.0f, 0.0f, 0.3f }; };
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::ampAttack,  1 }, "Attack",  sec(0), 0.005f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::ampDecay,   1 }, "Decay",   sec(0), 0.1f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::ampSustain, 1 }, "Sustain", NormalisableRange<float> { 0.0f, 1.0f }, 0.7f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::ampRelease, 1 }, "Release", sec(0), 0.25f));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::reverb, 1 }, "Reverb", NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::chorus, 1 }, "Chorus", NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::echo,   1 }, "Echo",   NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
    // Deeper effect controls (AMY defaults: reverb 0.85/0.5, chorus 0.5/0.5, echo 500ms/0/0).
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::reverbSize,    1 }, "Reverb Size",    NormalisableRange<float> { 0.0f, 1.0f }, 0.85f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::reverbDamping, 1 }, "Reverb Damping", NormalisableRange<float> { 0.0f, 1.0f }, 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::chorusRate,    1 }, "Chorus Rate",    NormalisableRange<float> { 0.1f, 10.0f, 0.0f, 0.4f }, 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::chorusDepth,   1 }, "Chorus Depth",   NormalisableRange<float> { 0.0f, 1.0f }, 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::echoTime,      1 }, "Echo Time",      NormalisableRange<float> { 1.0f, 700.0f, 0.0f, 0.5f }, 500.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::echoFeedback,  1 }, "Echo Feedback",  NormalisableRange<float> { 0.0f, 0.95f }, 0.0f));
    // Echo feedback-path tone = AMY's bipolar filter_coef: >0 low-pass (darker
    // repeats), <0 high-pass (brighter/thinner), 0 flat. AMY clamps >0.99.
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::echoTone,      1 }, "Echo Tone",      NormalisableRange<float> { -0.95f, 0.95f }, 0.0f));

    // Retro bitcrusher (sample-rate + bit-depth reduction). Freq = the crushed
    // sample rate in Hz (skewed so the low, gritty end is reachable); Bit = the
    // amplitude resolution. Both default to "clean" (Freq high, 16 bit = bypass).
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { id::bcFreq, 1 }, "Freq",
        NormalisableRange<float> { 200.0f, 48000.0f, 1.0f, 0.25f }, 48000.0f));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID { id::bcBits, 1 }, "Bit", 2, 16, 16));

    // Output WDF diode saturator (analog warmth). "Drive" = the diode push (THD)
    // with built-in gain compensation, so level stays steady as you drive harder.
    // 0 dB = the diode's character at unity; negative = cleaner, positive = hotter.
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { id::clipDrive, 1 }, "Drive",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::pitchBendRange, 1 }, "Pitch Bend Range", 1, 24, 2));

    // Performance / voicing (all engines).
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::glide, 1 }, "Glide",
        NormalisableRange<float> { 0.0f, 2000.0f, 1.0f, 0.4f }, 0.0f));   // portamento, ms
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::voiceMode, 1 }, "Voice Mode",
        StringArray { "Poly", "Mono", "Legato" }, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::unisonVoices, 1 }, "Unison", 1, 4, 1));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::unisonDetune, 1 }, "Detune",
        NormalisableRange<float> { 0.0f, 50.0f, 0.1f }, 12.0f));          // cents

    // --- Analog (Juno) engine -------------------------------------------------
    const StringArray waveNames { "Sine", "Pulse", "Saw Down", "Saw Up", "Triangle", "Noise" };
    const StringArray filtNames { "Off", "LPF", "BPF", "HPF", "LPF24" };
    auto unit  = [] { return NormalisableRange<float> { 0.0f, 1.0f }; };
    auto secs  = [] { return NormalisableRange<float> { 0.0f, 10.0f, 0.0f, 0.3f }; };

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::engine, 1 }, "Engine",
        StringArray { "Factory", "Analog", "FM" }, 0));

    // Osc freq = the pitch at A4 (note 69); the osc tracks the keyboard from there.
    // 440 = normal tuning, 220 = octave down, ~466 = +1 semitone, etc.
    auto oscFreq = [] { return NormalisableRange<float> { 20.0f, 2000.0f, 0.0f, 0.3f }; };
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::oscAWave, 1 }, "Osc A Wave", waveNames, 3));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscAFreq, 1 }, "Osc A Freq", oscFreq(), 440.0f));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::oscACoarse, 1 }, "Osc A Coarse", -24, 24, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::oscAFine, 1 }, "Osc A Fine", -100, 100, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscADuty, 1 }, "Osc A Duty", unit(), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscALevel, 1 }, "Osc A Level", unit(), 0.7f));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::oscBWave, 1 }, "Osc B Wave", waveNames, 1));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscBFreq, 1 }, "Osc B Freq", oscFreq(), 440.0f));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::oscBCoarse, 1 }, "Osc B Coarse", -24, 24, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::oscBFine, 1 }, "Osc B Fine", -100, 100, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscBDuty, 1 }, "Osc B Duty", unit(), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscBLevel, 1 }, "Osc B Level", unit(), 0.5f));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::oscCWave, 1 }, "Osc C Wave", waveNames, 3));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscCFreq, 1 }, "Osc C Freq", oscFreq(), 440.0f));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::oscCCoarse, 1 }, "Osc C Coarse", -24, 24, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::oscCFine, 1 }, "Osc C Fine", -100, 100, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscCDuty, 1 }, "Osc C Duty", unit(), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscCLevel, 1 }, "Osc C Level", unit(), 0.0f));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::oscDWave, 1 }, "Osc D Wave", waveNames, 3));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscDFreq, 1 }, "Osc D Freq", oscFreq(), 440.0f));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::oscDCoarse, 1 }, "Osc D Coarse", -24, 24, 0));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::oscDFine, 1 }, "Osc D Fine", -100, 100, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscDDuty, 1 }, "Osc D Duty", unit(), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscDLevel, 1 }, "Osc D Level", unit(), 0.0f));

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::lfoWave, 1 }, "LFO Wave", waveNames, 4));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::lfoFreq, 1 }, "LFO Freq",
        NormalisableRange<float> { 0.1f, 20.0f, 0.0f, 0.4f }, 4.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::lfoToPitch, 1 }, "LFO→Pitch", unit(), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::lfoToPwm, 1 }, "LFO→PWM", unit(), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::lfoToFilter, 1 }, "LFO→Filter", unit(), 0.0f));

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::vcfType, 1 }, "VCF Type", filtNames, 1));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::vcfKbd, 1 }, "VCF Kbd", unit(), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::vcfEnv, 1 }, "VCF Env",
        NormalisableRange<float> { 0.0f, 8.0f }, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::vcfAttack, 1 }, "VCF Attack", secs(), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::vcfDecay, 1 }, "VCF Decay", secs(), 0.6f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::vcfSustain, 1 }, "VCF Sustain", unit(), 0.3f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::vcfRelease, 1 }, "VCF Release", secs(), 0.4f));

    auto eqRange = [] { return NormalisableRange<float> { -15.0f, 15.0f }; };
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::eqLow, 1 }, "EQ Low", eqRange(), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::eqMid, 1 }, "EQ Mid", eqRange(), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::eqHigh, 1 }, "EQ High", eqRange(), 0.0f));

    // --- FM (DX7) engine ------------------------------------------------------
    // Algorithm: a 32-entry menu (1..32); the editor draws the operator diagram for
    // the selected one. A 32-step choice has the same normalised stepping as the old
    // 1..32 int param, so existing projects recall the same algorithm (0-based index).
    StringArray algoLabels;
    for (int a = 1; a <= 32; ++a) algoLabels.add(juce::String(a));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { id::fmAlgorithm, 1 }, "FM Algorithm", algoLabels, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::fmFeedback, 1 }, "FM Feedback", unit(), 0.0f));

    // Per-operator: frequency ratio (multiple of the note), output level (= FM
    // modulation index for modulators), and a full A/D/S/R envelope. Defaults give
    // a simple 2-operator tone (op1 carrier, op2 modulator) under algorithm 1.
    auto ratioRange = [] { return NormalisableRange<float> { 0.5f, 16.0f, 0.0f, 0.4f }; };
    auto levelRange = [] { return NormalisableRange<float> { 0.0f, 4.0f, 0.0f, 0.5f }; };
    for (int op = 1; op <= kFmOps; ++op)
    {
        const bool active = (op <= 2);                 // op1+op2 sound by default
        const float lvl   = active ? 1.0f : 0.0f;
        const float ratio = 1.0f;
        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID { id::fmOp(op, "ratio"), 1 }, "Op " + juce::String(op) + " Ratio", ratioRange(), ratio));
        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID { id::fmOp(op, "level"), 1 }, "Op " + juce::String(op) + " Level", levelRange(), lvl));
        // DX7 4-rate / 4-level operator envelope (each 0..99) — the native DX7 EG.
        const float rDef[4] = { 95.0f, 60.0f, 40.0f, 55.0f };
        const float lDef[4] = { 99.0f, 80.0f, 65.0f,  0.0f };
        auto eg99 = [] { return NormalisableRange<float> { 0.0f, 99.0f, 1.0f }; };
        for (int e = 1; e <= 4; ++e)
        {
            layout.add(std::make_unique<AudioParameterFloat>(
                ParameterID { id::fmOp(op, ("r" + juce::String(e)).toRawUTF8()), 1 },
                "Op " + juce::String(op) + " R" + juce::String(e), eg99(), rDef[e - 1]));
            layout.add(std::make_unique<AudioParameterFloat>(
                ParameterID { id::fmOp(op, ("l" + juce::String(e)).toRawUTF8()), 1 },
                "Op " + juce::String(op) + " L" + juce::String(e), eg99(), lDef[e - 1]));
        }
        // fixed = fixed-frequency mode; fixedhz = its absolute Hz.
        layout.add(std::make_unique<AudioParameterBool>(
            ParameterID { id::fmOp(op, "fixed"), 1 }, "Op " + juce::String(op) + " Fixed", false));
        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID { id::fmOp(op, "fixedhz"), 1 }, "Op " + juce::String(op) + " Fixed Hz",
            NormalisableRange<float> { 1.0f, 20000.0f, 0.0f, 0.3f }, 440.0f));
    }

    return layout;
}
} // namespace amyplug::params
