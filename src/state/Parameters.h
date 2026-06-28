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
    // ... append more as the editor grows. Group per-engine params behind these.
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

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID { id::patchA, 1 }, "Patch", 0, 1055, 0));

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

    return layout;
}
} // namespace amyplug::params
