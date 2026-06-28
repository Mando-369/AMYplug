// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugEditor.h"
#include "state/Parameters.h"

namespace amyplug
{
AmyPlugEditor::AmyPlugEditor(AmyPlugProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p)
{
    auto& s = proc.apvts();

    modeBox.addItemList({ "Software", "Hardware" }, 1);
    addAndMakeVisible(modeBox);
    modeAtt = std::make_unique<Apvts::ComboBoxAttachment>(s, params::id::mode, modeBox);

    auto setupRotary = [this] (juce::Slider& sl)
    {
        sl.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        sl.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
        addAndMakeVisible(sl);
    };
    patchSlider.setSliderStyle(juce::Slider::IncDecButtons);
    patchSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 64, 20);
    addAndMakeVisible(patchSlider);
    setupRotary(cutoff); setupRotary(reso); setupRotary(reverb);

    patchAtt  = std::make_unique<Apvts::SliderAttachment>(s, params::id::patchA,       patchSlider);
    cutoffAtt = std::make_unique<Apvts::SliderAttachment>(s, params::id::filterCutoff, cutoff);
    resoAtt   = std::make_unique<Apvts::SliderAttachment>(s, params::id::filterReso,   reso);
    reverbAtt = std::make_unique<Apvts::SliderAttachment>(s, params::id::reverb,       reverb);

    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    panicButton.onClick = [this] { proc.requestPanic(); };
    addAndMakeVisible(panicButton);

    setSize(560, 280);
}

AmyPlugEditor::~AmyPlugEditor() = default;

void AmyPlugEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e2327));   // shore-pine dark
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("AMYplug", 16, 12, 300, 28, juce::Justification::left);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(juce::String::fromUTF8("AMY for your DAW · pre-alpha scaffold"), 16, 40, 400, 18,
               juce::Justification::left);
}

void AmyPlugEditor::resized()
{
    auto r = getLocalBounds().reduced(16).withTrimmedTop(60);
    auto top = r.removeFromTop(28);
    modeBox.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(12);
    patchSlider.setBounds(top.removeFromLeft(160));
    panicButton.setBounds(top.removeFromRight(90));

    r.removeFromTop(20);
    auto row = r.removeFromTop(140);
    const int w = row.getWidth() / 3;
    cutoff.setBounds(row.removeFromLeft(w).reduced(8));
    reso  .setBounds(row.removeFromLeft(w).reduced(8));
    reverb.setBounds(row.removeFromLeft(w).reduced(8));
}
} // namespace amyplug
