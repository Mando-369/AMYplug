// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AmyPlugProcessor.h"

namespace amyplug
{
// MVP editor: mode switch, patch selector, a few macro knobs, and a Panic button.
// Grow into per-engine panels (Juno / FM / PCM / modular) over the roadmap.
class AmyPlugEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AmyPlugEditor(AmyPlugProcessor&);
    ~AmyPlugEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    AmyPlugProcessor& proc;

    juce::ComboBox modeBox;          // Software / Hardware
    juce::Slider   patchSlider;      // patch number
    juce::Slider   cutoff, reso, reverb;
    juce::TextButton panicButton { "PANIC" };

    std::unique_ptr<Apvts::ComboBoxAttachment> modeAtt;
    std::unique_ptr<Apvts::SliderAttachment>   patchAtt, cutoffAtt, resoAtt, reverbAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugEditor)
};
} // namespace amyplug
