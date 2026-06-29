// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// Parameters.h — the host-automatable parameter layout (JUCE APVTS).
//
// RULE: every parameter here must also be reflected into PatchModel and the AMY
// rebuild path, or recall/automation will desync. Keep IDs stable forever (the
// host stores them in projects) — never renumber, only append.

#include <juce_audio_processors/juce_audio_processors.h>

namespace amyplug::params
{
// Stable string IDs (do not change once shipped).
namespace id
{
    inline constexpr auto mode          = "mode";          // 0 = Software, 1 = Hardware
    inline constexpr auto masterVolume  = "master_volume"; // 0..10 (AMY volume)
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
    inline constexpr auto pitchBendRange= "pitch_bend_range";

    // --- Analog (Juno) engine, M3b. filterCutoff/filterReso/ampADSR/masterVolume
    //     above are reused as VCF freq/reso, amp env and level. ---
    inline constexpr auto engine        = "engine";        // 0 = Factory preset, 1 = Analog
    inline constexpr auto oscAWave      = "osc_a_wave";
    inline constexpr auto oscADuty      = "osc_a_duty";
    inline constexpr auto oscALevel     = "osc_a_level";
    inline constexpr auto oscBWave      = "osc_b_wave";
    inline constexpr auto oscBDuty      = "osc_b_duty";
    inline constexpr auto oscBLevel     = "osc_b_level";
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
}

// Builds the parameter layout. Implemented inline so the processor can call it
// from its initializer list.
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { id::mode, 1 }, "Mode", StringArray { "Software", "Hardware" }, 0));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { id::masterVolume, 1 }, "Master Volume",
        NormalisableRange<float> { 0.0f, 10.0f, 0.001f }, 1.0f));

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

    layout.add(std::make_unique<AudioParameterInt>(ParameterID { id::pitchBendRange, 1 }, "Pitch Bend Range", 1, 24, 2));

    // --- Analog (Juno) engine -------------------------------------------------
    const StringArray waveNames { "Sine", "Pulse", "Saw Down", "Saw Up", "Triangle", "Noise" };
    const StringArray filtNames { "Off", "LPF", "BPF", "HPF", "LPF24" };
    auto unit  = [] { return NormalisableRange<float> { 0.0f, 1.0f }; };
    auto secs  = [] { return NormalisableRange<float> { 0.0f, 10.0f, 0.0f, 0.3f }; };

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::engine, 1 }, "Engine",
        StringArray { "Factory", "Analog" }, 0));

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::oscAWave, 1 }, "Osc A Wave", waveNames, 3));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscADuty, 1 }, "Osc A Duty", unit(), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscALevel, 1 }, "Osc A Level", unit(), 0.7f));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id::oscBWave, 1 }, "Osc B Wave", waveNames, 1));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscBDuty, 1 }, "Osc B Duty", unit(), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id::oscBLevel, 1 }, "Osc B Level", unit(), 0.5f));

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

    return layout;
}
} // namespace amyplug::params
