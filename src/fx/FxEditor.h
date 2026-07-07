// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../gui/AmyLookAndFeel.h"

namespace amyplug
{
class FxProcessor;

// Compact editor for AMYplugFX: title + five knobs (Freq, Bit, Drive, Mix, Output)
// in the AMYplug house style, reusing AmyLookAndFeel.
class FxEditor : public juce::AudioProcessorEditor
{
public:
    explicit FxEditor(FxProcessor&);
    ~FxEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttachment> attach;
    };

    void addKnob(Knob& k, const juce::String& paramId, const juce::String& name, juce::Colour accent);

    FxProcessor& proc;
    AmyLookAndFeel lnf;

    Knob freq, bit, drive, mix, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEditor)
};
} // namespace amyplug
