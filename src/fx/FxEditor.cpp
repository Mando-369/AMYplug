// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "FxEditor.h"
#include "FxProcessor.h"
#include "../gui/AmyColours.h"
#include "../gui/AmyFonts.h"

namespace amyplug
{
namespace col = amyplug::colours;

FxEditor::FxEditor(FxProcessor& p) : juce::AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&lnf);

    addKnob(freq,   fxid::freq,   "FREQ",   col::engineCyan);
    addKnob(bit,    fxid::bits,   "BIT",    col::engineCyan);
    addKnob(drive,  fxid::drive,  "DRIVE",  col::junoRed);
    addKnob(mix,    fxid::mix,    "MIX",    col::amber);
    addKnob(output, fxid::output, "OUTPUT", col::amber);

    setSize(560, 260);
}

FxEditor::~FxEditor() { setLookAndFeel(nullptr); }

void FxEditor::addKnob(Knob& k, const juce::String& paramId, const juce::String& name, juce::Colour accent)
{
    k.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 18);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible(k.slider);
    k.attach = std::make_unique<SliderAttachment>(proc.apvts(), paramId, k.slider);

    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, col::textDim);
    k.label.setFont(fonts::label(13.0f));
    addAndMakeVisible(k.label);
}

void FxEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bg(col::shellTop, 0, 0, col::shellBottom, 0, (float) getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    // Brand wordmark — identical to the instrument: "AMY" primary + "plug" grey,
    // same font/size/position, with an "FX" accent marking the effect variant.
    auto logo = fonts::logo(30.0f);
    g.setFont(logo);
    const int lx = 16, ly = 12;
    const int amyW  = (int) juce::GlyphArrangement::getStringWidth(logo, "AMY");
    const int plugW = (int) juce::GlyphArrangement::getStringWidth(logo, "plug");
    g.setColour(col::textPrimary);
    g.drawText("AMY", lx, ly, amyW + 4, 30, juce::Justification::left);
    g.setColour(col::tabIndicator);
    g.drawText("plug", lx + amyW, ly, plugW + 4, 30, juce::Justification::left);
    g.setColour(col::engineCyan);
    g.drawText("FX", lx + amyW + plugW + 8, ly, 60, 30, juce::Justification::left);

    // Subtitle — same style/position as the instrument's "AMY FOR YOUR DAW · EDITOR V2".
    g.setColour(col::textFaint);
    g.setFont(fonts::label(11.0f).withExtraKerningFactor(0.04f));
    g.drawText(juce::String::fromUTF8("BITCRUSHER · DIODE SATURATOR"),
               lx, ly + 28, 320, 12, juce::Justification::left);

    g.setColour(col::hairline);
    g.fillRect(0, 56, getWidth(), 1);
}

void FxEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(56);                 // header
    area.reduce(14, 12);

    Knob* knobs[] = { &freq, &bit, &drive, &mix, &output };
    const int n = 5;
    const int cellW = area.getWidth() / n;
    for (int i = 0; i < n; ++i)
    {
        auto cell = area.removeFromLeft(cellW).reduced(6, 0);
        knobs[i]->label.setBounds(cell.removeFromTop(18));
        knobs[i]->slider.setBounds(cell.reduced(4, 2));
    }
}
} // namespace amyplug
