// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "FxEditor.h"
#include "FxProcessor.h"
#include "../gui/AmyColours.h"
#include "../gui/AmyFonts.h"

namespace amyplug
{
namespace col = amyplug::colours;

void PowerButton::paintButton(juce::Graphics& g, bool highlighted, bool /*down*/)
{
    // Sits on a bright accent title bar. ON = the "FX" teal (lit), OFF = dark.
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(getToggleState() ? col::engineCyan : col::groove);
    g.fillEllipse(r);
    g.setColour(col::panel.withAlpha(0.6f));   // subtle rim for definition on bright bars
    g.drawEllipse(r.reduced(0.5f), 1.0f);
    if (highlighted)
    {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.fillEllipse(r);
    }
}

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
    addKnob(follower, fxid::follower, "SPEED",    col::filterViolet);

    addKnob(eqLow,    fxid::eqLow,    "LOW",      col::amber);
    addKnob(eqMid,    fxid::eqMid,    "MID",      col::amber);
    addKnob(eqHigh,   fxid::eqHigh,   "HIGH",     col::amber);

    addKnob(choMix,   fxid::choMix,   "MIX",      col::lfoGreen);
    addKnob(choRate,  fxid::choRate,  "RATE",     col::lfoGreen);
    addKnob(choDepth, fxid::choDepth, "DEPTH",    col::lfoGreen);

    addKnob(echMix,   fxid::echMix,   "MIX",      col::engineCyan);
    addKnob(echTime,  fxid::echTime,  "TIME",     col::engineCyan);
    addKnob(echFb,    fxid::echFb,    "FBK",      col::engineCyan);
    addKnob(echTone,  fxid::echTone,  "TONE",     col::engineCyan);

    addKnob(revMix,   fxid::revMix,   "MIX",      col::junoBlue);
    addKnob(revSize,  fxid::revSize,  "SIZE",     col::junoBlue);
    addKnob(revDamp,  fxid::revDamp,  "DAMP",     col::junoBlue);

    addKnob(freq,   fxid::freq,   "FREQ",   col::junoRed);
    addKnob(bit,    fxid::bits,   "BIT",    col::junoRed);
    addKnob(drive,  fxid::drive,  "DRIVE",  col::junoRed);
    addKnob(mix,    fxid::mix,    "MIX",    col::amber);
    addKnob(output, fxid::output, "OUTPUT", col::amber);

    addPower(fltPower,   fltPowerAtt,   fxid::fltOn);
    addPower(eqPower,    eqPowerAtt,    fxid::eqOn);
    addPower(choPower,   choPowerAtt,   fxid::choOn);
    addPower(echPower,   echPowerAtt,   fxid::echOn);
    addPower(revPower,   revPowerAtt,   fxid::revOn);
    addPower(crushPower, crushPowerAtt, fxid::crushOn);
    addPower(diodePower, diodePowerAtt, fxid::diodeOn);

    // Width chosen so each of the 4 columns is wide enough to hold up to 4 knobs at
    // the full (capped) knob size — uniform spacing across every card.
    setSize(1328, 476);
}

void FxEditor::addPower(PowerButton& b, std::unique_ptr<ButtonAttachment>& att, const juce::String& paramId)
{
    b.setTooltip("Bypass");
    addAndMakeVisible(b);
    att = std::make_unique<ButtonAttachment>(proc.apvts(), paramId, b);
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

    // Uniform 4-column x 2-row grid, in signal-flow order:
    //   FILTER  EQ      CHORUS  ECHO
    //   REVERB  BITCRUSH DIODE  OUT
    const int cardW = (getWidth() - 2 * m - 3 * gap) / 4;
    const int cardH = 184;
    const int y1 = 64, y2 = y1 + cardH + 10;
    auto cell = [&](int colv, int yv) { return juce::Rectangle<int>(m + colv * (cardW + gap), yv, cardW, cardH); };

    juce::Rectangle<int> rFilter = cell(0, y1), rEq    = cell(1, y1), rChorus = cell(2, y1), rEcho  = cell(3, y1);
    juce::Rectangle<int> rReverb = cell(0, y2), rCrush = cell(1, y2), rDiode  = cell(2, y2), rOut   = cell(3, y2);

    auto content = [](juce::Rectangle<int> card) { return card.withTrimmedTop(titleH).reduced(8, 8); };

    // Lay a row of knobs as a fixed-size block, centred in `area` (uniform knob size
    // across every card, regardless of how many the card has).
    auto placeRow = [](std::vector<Knob*> knobs, juce::Rectangle<int> area)
    {
        const int count = (int) knobs.size();
        if (count == 0) return;
        const int cellW = juce::jmin(76, area.getWidth() / count);
        const int knobH = juce::jmin(area.getHeight(), 102);
        int x = area.getCentreX() - (cellW * count) / 2;
        const int yTop = area.getCentreY() - knobH / 2;
        for (auto* k : knobs)
        {
            juce::Rectangle<int> c(x, yTop, cellW, knobH);
            k->label.setBounds(c.removeFromTop(15));
            k->slider.setBounds(c.reduced(3, 2));
            x += cellW;
        }
    };
    auto placePower = [](juce::Component& b, juce::Rectangle<int> card) { b.setBounds(card.getRight() - 21, card.getY() + 5, 13, 13); };

    placePower(fltPower, rFilter); placePower(eqPower, rEq); placePower(choPower, rChorus); placePower(echPower, rEcho);
    placePower(revPower, rReverb); placePower(crushPower, rCrush); placePower(diodePower, rDiode);

    {   // FILTER: type selector on top, then the 4 knobs.
        auto c = content(rFilter);
        fltTypeLabel.setBounds(c.removeFromTop(13));
        fltType.setBounds(c.removeFromTop(24));
        c.removeFromTop(6);
        placeRow({ &cutoff, &reso, &envAmt, &follower }, c);
    }
    placeRow({ &eqLow, &eqMid, &eqHigh },       content(rEq));
    placeRow({ &choMix, &choRate, &choDepth },  content(rChorus));
    placeRow({ &echMix, &echTime, &echFb, &echTone }, content(rEcho));
    placeRow({ &revMix, &revSize, &revDamp },   content(rReverb));
    placeRow({ &freq, &bit },                   content(rCrush));
    placeRow({ &drive },                        content(rDiode));
    placeRow({ &mix, &output },                 content(rOut));

    cards = {
        { "FILTER",   col::filterViolet, rFilter, false },
        { "EQ",       col::amber,        rEq,     false },
        { "CHORUS",   col::lfoGreen,     rChorus, false },
        { "ECHO",     col::engineCyan,   rEcho,   false },
        { "REVERB",   col::junoBlue,     rReverb, false },
        { "BITCRUSH", col::junoRed,      rCrush,  false },
        { "DIODE CLIPPER", col::junoRed, rDiode,  false },
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
