// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../gui/AmyLookAndFeel.h"

namespace amyplug
{
class FxProcessor;

// A compact power toggle drawn as a dot in a card's title bar (filled = on).
class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button("bypass") { setClickingTogglesState(true); }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

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
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

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
    void addPower(PowerButton&, std::unique_ptr<ButtonAttachment>&, const juce::String& paramId);
    void layoutCards();     // compute card rects + place children (called from resized)

    FxProcessor& proc;
    AmyLookAndFeel lnf;

    // Filter card
    juce::ComboBox fltType;
    std::unique_ptr<ComboAttachment> fltTypeAtt;
    juce::Label    fltTypeLabel;
    Knob cutoff, reso, envAmt, follower;

    // EQ card
    Knob eqLow, eqMid, eqHigh;

    // Reverb card
    Knob revMix, revSize, revDamp;

    // Bitcrush / Diode / Out cards
    Knob freq, bit, drive, mix, output;

    // Per-effect bypass toggles (in the title bars).
    PowerButton fltPower, eqPower, revPower, crushPower, diodePower;
    std::unique_ptr<ButtonAttachment> fltPowerAtt, eqPowerAtt, revPowerAtt, crushPowerAtt, diodePowerAtt;

    std::vector<Card> cards;   // for paint (backgrounds + titles + placeholders)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEditor)
};
} // namespace amyplug
