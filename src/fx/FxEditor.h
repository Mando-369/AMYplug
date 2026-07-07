// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../gui/AmyLookAndFeel.h"

namespace amyplug
{
class FxProcessor;

// AMYplugFX editor — an FX rack laid out in the synth's signal-flow order:
//   FILTER -> EQ -> CHORUS -> ECHO -> REVERB -> BITCRUSH -> DIODE -> OUT
// Each effect is a titled card. Cards not yet implemented render as dimmed
// placeholders in their correct slot, so later phases just fill them in — the
// layout never has to be reflowed.
class FxEditor : public juce::AudioProcessorEditor
{
public:
    explicit FxEditor(FxProcessor&);
    ~FxEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttachment> attach;
    };

    struct Card
    {
        juce::String title;
        juce::Colour accent;
        juce::Rectangle<int> bounds;
        bool placeholder = false;
    };

    void addKnob(Knob&, const juce::String& paramId, const juce::String& name, juce::Colour accent);
    void layoutCards();     // compute card rects + place children (called from resized)

    FxProcessor& proc;
    AmyLookAndFeel lnf;

    // Filter card
    juce::ComboBox fltType;
    std::unique_ptr<ComboAttachment> fltTypeAtt;
    juce::Label    fltTypeLabel;
    Knob cutoff, reso, envAmt, follower;

    // Bitcrush / Diode / Out cards
    Knob freq, bit, drive, mix, output;

    std::vector<Card> cards;   // for paint (backgrounds + titles + placeholders)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEditor)
};
} // namespace amyplug
