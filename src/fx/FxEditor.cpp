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

    // Filter card: type selector + 4 knobs.
    fltTypeLabel.setText("TYPE", juce::dontSendNotification);
    fltTypeLabel.setJustificationType(juce::Justification::centredLeft);
    fltTypeLabel.setColour(juce::Label::textColourId, col::textDim);
    fltTypeLabel.setFont(fonts::label(11.0f));
    addAndMakeVisible(fltTypeLabel);

    fltType.addItem("LP 24", 1);
    fltType.addItem("LP 12", 2);
    fltType.addItem("HP", 3);
    fltType.addItem("BP", 4);
    addAndMakeVisible(fltType);
    fltTypeAtt = std::make_unique<ComboAttachment>(proc.apvts(), fxid::fltType, fltType);

    addKnob(cutoff,   fxid::cutoff,   "CUTOFF",   col::filterViolet);
    addKnob(reso,     fxid::reso,     "RESO",     col::filterViolet);
    addKnob(envAmt,   fxid::envAmt,   "ENV AMT",  col::filterViolet);
    addKnob(follower, fxid::follower, "FOLLOWER", col::filterViolet);

    addKnob(freq,   fxid::freq,   "FREQ",   col::junoRed);
    addKnob(bit,    fxid::bits,   "BIT",    col::junoRed);
    addKnob(drive,  fxid::drive,  "DRIVE",  col::junoRed);
    addKnob(mix,    fxid::mix,    "MIX",    col::amber);
    addKnob(output, fxid::output, "OUTPUT", col::amber);

    setSize(940, 420);
}

FxEditor::~FxEditor() { setLookAndFeel(nullptr); }

void FxEditor::addKnob(Knob& k, const juce::String& paramId, const juce::String& name, juce::Colour accent)
{
    k.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible(k.slider);
    k.attach = std::make_unique<SliderAttachment>(proc.apvts(), paramId, k.slider);

    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, col::textDim);
    k.label.setFont(fonts::label(12.0f));
    addAndMakeVisible(k.label);
}

void FxEditor::layoutCards()
{
    const int m = 12, gap = 8;
    constexpr int titleH = 24;

    // Row A — filter + bus FX, in signal-flow order.
    const int ay = 64, ah = 176;
    int x = m;
    juce::Rectangle<int> rFilter(x, ay, 300, ah); x += 300 + gap;
    juce::Rectangle<int> rEq    (x, ay, 145, ah); x += 145 + gap;
    juce::Rectangle<int> rChorus(x, ay, 145, ah); x += 145 + gap;
    juce::Rectangle<int> rEcho  (x, ay, 145, ah); x += 145 + gap;
    juce::Rectangle<int> rReverb(x, ay, 145, ah);

    // Row B — output stage.
    const int by = ay + ah + 10, bh = 150;
    x = m;
    juce::Rectangle<int> rCrush(x, by, 200, bh); x += 200 + gap;
    juce::Rectangle<int> rDiode(x, by, 130, bh); x += 130 + gap;
    juce::Rectangle<int> rOut  (x, by, 200, bh);

    auto content   = [](juce::Rectangle<int> card) { return card.withTrimmedTop(titleH).reduced(8, 6); };
    auto placeKnob = [](Knob& k, juce::Rectangle<int> cell) { k.label.setBounds(cell.removeFromTop(15)); k.slider.setBounds(cell.reduced(2)); };

    {   // FILTER
        auto c = content(rFilter);
        fltTypeLabel.setBounds(c.removeFromTop(14));
        fltType.setBounds(c.removeFromTop(24));
        c.removeFromTop(6);
        const int kw = c.getWidth() / 4;
        placeKnob(cutoff,   c.removeFromLeft(kw));
        placeKnob(reso,     c.removeFromLeft(kw));
        placeKnob(envAmt,   c.removeFromLeft(kw));
        placeKnob(follower, c);
    }
    {   // BITCRUSH
        auto c = content(rCrush);
        const int kw = c.getWidth() / 2;
        placeKnob(freq, c.removeFromLeft(kw));
        placeKnob(bit,  c);
    }
    {   // DIODE
        auto c = content(rDiode);
        placeKnob(drive, c);
    }
    {   // OUT
        auto c = content(rOut);
        const int kw = c.getWidth() / 2;
        placeKnob(mix,    c.removeFromLeft(kw));
        placeKnob(output, c);
    }

    cards = {
        { "FILTER",   col::filterViolet, rFilter, false },
        { "EQ",       col::amber,        rEq,     true  },
        { "CHORUS",   col::lfoGreen,     rChorus, true  },
        { "ECHO",     col::engineCyan,   rEcho,   true  },
        { "REVERB",   col::junoBlue,     rReverb, true  },
        { "BITCRUSH", col::junoRed,      rCrush,  false },
        { "DIODE",    col::junoRed,      rDiode,  false },
        { "OUT",      col::amber,        rOut,    false },
    };
}

void FxEditor::paint(juce::Graphics& g)
{
    g.setGradientFill({ col::shellTop, 0, 0, col::shellBottom, 0, (float) getHeight(), false });
    g.fillAll();

    // Brand wordmark — identical to the synth: "AMY" + "plug" + cyan "FX".
    auto logo = fonts::logo(30.0f);
    g.setFont(logo);
    const int lx = 16, ly = 12;
    const int amyW  = (int) juce::GlyphArrangement::getStringWidth(logo, "AMY");
    const int plugW = (int) juce::GlyphArrangement::getStringWidth(logo, "plug");
    g.setColour(col::textPrimary);  g.drawText("AMY",  lx, ly, amyW + 4, 30, juce::Justification::left);
    g.setColour(col::tabIndicator); g.drawText("plug", lx + amyW, ly, plugW + 4, 30, juce::Justification::left);
    g.setColour(col::engineCyan);   g.drawText("FX",   lx + amyW + plugW + 8, ly, 60, 30, juce::Justification::left);
    g.setColour(col::textFaint);
    g.setFont(fonts::label(11.0f).withExtraKerningFactor(0.04f));
    g.drawText(juce::String::fromUTF8("THE AMY FX BUS · INSERT"), lx, ly + 28, 360, 12, juce::Justification::left);
    g.setColour(col::hairline);
    g.fillRect(0, 56, getWidth(), 1);

    // Cards.
    for (auto& c : cards)
    {
        g.setColour(col::panel);
        g.fillRoundedRectangle(c.bounds.toFloat(), 6.0f);
        g.setColour(col::hairline);
        g.drawRoundedRectangle(c.bounds.toFloat().reduced(0.5f), 6.0f, 1.0f);

        // Title bar (rounded top, squared bottom).
        auto tb = c.bounds.withHeight(22);
        g.setColour(c.placeholder ? c.accent.withAlpha(0.30f) : c.accent);
        g.fillRoundedRectangle(tb.toFloat(), 6.0f);
        g.fillRect(tb.getX(), tb.getY() + 11, tb.getWidth(), 11);
        g.setColour(c.placeholder ? col::textDim : col::headerTextOn(c.accent));
        g.setFont(fonts::header(12.0f).withExtraKerningFactor(0.08f));
        g.drawText(c.title, tb.reduced(9, 0), juce::Justification::centredLeft);

        if (c.placeholder)
        {
            g.setColour(col::textFaint);
            g.setFont(fonts::label(12.0f).withExtraKerningFactor(0.12f));
            g.drawText("SOON", c.bounds.withTrimmedTop(22), juce::Justification::centred);
        }
    }
}

void FxEditor::resized() { layoutCards(); }
} // namespace amyplug
