// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyLookAndFeel.h"
#include "AmyColours.h"
#include "AmyFonts.h"

namespace amyplug
{
namespace col = amyplug::colours;

// The design knob: 270° sweep with a 90° gap at the bottom. In JUCE angle terms
// (clockwise from 12 o'clock) that's 225° → 495°. Hard-coded here so every knob
// gets the look regardless of its own rotary parameters.
static constexpr float kKnobStart = 3.92699082f;   // 225°
static constexpr float kKnobEnd   = 8.63937979f;   // 495°

AmyLookAndFeel::AmyLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, col::shellTop);

    // Rotary defaults (each section overrides rotarySliderFillColourId with its accent).
    setColour(juce::Slider::rotarySliderFillColourId, col::engineCyan);
    setColour(juce::Slider::textBoxTextColourId,       col::lcdText);
    setColour(juce::Slider::textBoxBackgroundColourId, col::lcdFill);
    setColour(juce::Slider::textBoxOutlineColourId,    col::lcdBorder);

    setColour(juce::ComboBox::backgroundColourId, col::comboFill);
    setColour(juce::ComboBox::outlineColourId,    col::comboBorder);
    setColour(juce::ComboBox::textColourId,       col::textPrimary);
    setColour(juce::ComboBox::arrowColourId,      col::textFaint);

    setColour(juce::PopupMenu::backgroundColourId,          col::panel);
    setColour(juce::PopupMenu::textColourId,                col::textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col::engineCyan.withAlpha(0.20f));
    setColour(juce::PopupMenu::highlightedTextColourId,     col::textPrimary);

    setColour(juce::Label::textColourId, col::textDim);

    setColour(juce::TextButton::textColourOnId,  col::textPrimary);
    setColour(juce::TextButton::textColourOffId, col::textPrimary);
}

// ===========================================================================
// Rotary knob
// ===========================================================================
void AmyLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                      float pos, float, float, juce::Slider& s)
{
    const auto accent = s.findColour(juce::Slider::rotarySliderFillColourId);
    auto area = juce::Rectangle<int>(x, y, w, h).toFloat();

    // Design ⌀36; allow a little more when the cell is large, but keep it a knob.
    const float diameter = juce::jmin((float) w, (float) h, 46.0f);
    const float radius   = diameter * 0.5f;
    auto face = juce::Rectangle<float>(diameter, diameter).withCentre(area.getCentre());
    const float cx = face.getCentreX(), cy = face.getCentreY();

    const float bandW    = diameter * 0.125f;                 // arc ring band (~4.5 @ 36)
    const float arcR     = radius - bandW * 0.5f;
    const float toAngle  = kKnobStart + pos * (kKnobEnd - kKnobStart);

    // Unfilled track (full range) then the accent fill up to the value.
    juce::Path track, fill;
    track.addCentredArc(cx, cy, arcR, arcR, 0.0f, kKnobStart, kKnobEnd, true);
    g.setColour(col::arcTrack);
    g.strokePath(track, { bandW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    if (pos > 0.0001f)
    {
        fill.addCentredArc(cx, cy, arcR, arcR, 0.0f, kKnobStart, toAngle, true);
        g.setColour(accent);
        g.strokePath(fill, { bandW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    // Inset face: radial gradient with a top highlight, dark rim.
    const float faceR = radius - bandW;
    auto faceRect = juce::Rectangle<float>(faceR * 2.0f, faceR * 2.0f).withCentre({ cx, cy });
    juce::ColourGradient grad(col::faceTop, cx, cy - faceR * 0.5f,
                              col::faceBottom, cx, cy + faceR, true);
    g.setGradientFill(grad);
    g.fillEllipse(faceRect);
    g.setColour(col::faceBorder);
    g.drawEllipse(faceRect, 1.0f);

    // Pointer: rounded bar from the hub outward, with a soft accent glow.
    const float ptrW   = juce::jmax(1.5f, diameter * 0.045f);
    const float ptrLen = faceR * 0.92f;
    juce::Path ptr;
    ptr.addRoundedRectangle(-ptrW * 0.5f, -ptrLen, ptrW, ptrLen, ptrW * 0.5f);
    ptr.applyTransform(juce::AffineTransform::rotation(toAngle).translated(cx, cy));
    g.setColour(accent.withAlpha(0.40f));
    g.strokePath(ptr, juce::PathStrokeType(ptrW + 2.5f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    g.setColour(accent);
    g.fillPath(ptr);

    // Hub.
    const float hubR = diameter * 0.10f;
    g.setColour(col::hub);
    g.fillEllipse(cx - hubR, cy - hubR, hubR * 2.0f, hubR * 2.0f);
}

juce::Slider::SliderLayout AmyLookAndFeel::getSliderLayout(juce::Slider& s)
{
    juce::Slider::SliderLayout layout;
    auto b = s.getLocalBounds();
    if (s.getTextBoxPosition() == juce::Slider::TextBoxBelow)
    {
        // Taller LCD well so the segmented font isn't cramped (value stays centred),
        // with a gap above it so the knob has bottom breathing room.
        const int tbH = juce::jlimit(16, 22, b.getHeight() / 3);
        layout.textBoxBounds = b.removeFromBottom(tbH).reduced(3, 1);
        b.removeFromBottom(7);
    }
    layout.sliderBounds = b;
    return layout;
}

// ===========================================================================
// Combo box
// ===========================================================================
void AmyLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool,
                                  int, int, int, int, juce::ComboBox& box)
{
    auto b = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(b, 3.0f);
    // Subtle top inset shadow line.
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawLine(b.getX() + 3, b.getY() + 1.5f, b.getRight() - 3, b.getY() + 1.5f, 1.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(b, 3.0f, 1.0f);

    // Caret ▾.
    juce::Rectangle<float> arrow((float) w - 16.0f, 0, 12.0f, (float) h);
    juce::Path p;
    const float ax = arrow.getCentreX(), ay = arrow.getCentreY();
    p.startNewSubPath(ax - 3.5f, ay - 2.0f);
    p.lineTo(ax,        ay + 2.5f);
    p.lineTo(ax + 3.5f, ay - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(p, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
}

void AmyLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(1, 1, box.getWidth() - 18, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::Font AmyLookAndFeel::getComboBoxFont(juce::ComboBox&) { return fonts::label(18.0f); }
juce::Font AmyLookAndFeel::getPopupMenuFont()               { return fonts::label(19.0f); }

void AmyLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                       bool isSeparator, bool isActive, bool isHighlighted,
                                       bool isTicked, bool hasSubMenu, const juce::String& text,
                                       const juce::String& shortcutKeyText,
                                       const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        auto r = area.reduced(8, 0).toFloat();
        g.setColour(col::hairline);
        g.fillRect(r.withHeight(1.0f).withY(r.getCentreY()));
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRect(area);
    }
    g.setColour(isActive ? (isHighlighted ? findColour(juce::PopupMenu::highlightedTextColourId)
                                          : findColour(juce::PopupMenu::textColourId))
                         : col::textFaint);
    if (textColour != nullptr) g.setColour(*textColour);
    g.setFont(getPopupMenuFont());
    auto r = area.reduced(10, 0);
    if (isTicked)
    {
        g.fillEllipse((float) r.getX(), (float) r.getCentreY() - 2.0f, 4.0f, 4.0f);
    }
    g.drawFittedText(text, r.withTrimmedLeft(10), juce::Justification::centredLeft, 1);
    juce::ignoreUnused(shortcutKeyText, hasSubMenu, icon);
}

// ===========================================================================
// Buttons
// ===========================================================================
void AmyLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                          const juce::Colour& backgroundColour,
                                          bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    // Treat low-saturation button colours as "use the neutral raised panel"; our
    // accent buttons (PANIC amber/red) keep their vivid colour + a soft glow.
    const bool accented = backgroundColour.getSaturation() > 0.25f
                       && backgroundColour.getBrightness() > 0.25f;
    auto fill = accented ? backgroundColour : col::panelRaised;
    if (down) fill = fill.darker(0.15f);
    else if (over) fill = fill.brighter(0.08f);

    if (accented)
    {
        g.setColour(backgroundColour.withAlpha(0.35f));
        g.fillRoundedRectangle(r.expanded(1.5f), 5.0f);      // soft glow
    }
    g.setColour(fill);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(accented ? col::panicBorder : col::comboBorder);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

juce::Font AmyLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return fonts::label(17.5f); }

void AmyLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour(b.findColour(b.getToggleState() ? juce::TextButton::textColourOnId
                                                : juce::TextButton::textColourOffId)
                 .withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.4f));
    g.setFont(getTextButtonFont(b, b.getHeight()));
    auto txt = b.getButtonText().toUpperCase();
    g.drawFittedText(txt, b.getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
}

// ===========================================================================
// Labels (control labels + LCD readouts)
// ===========================================================================
void AmyLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    auto b = label.getLocalBounds().toFloat();

    // A slider's value box is a Label whose parent is the Slider — style those as
    // the segmented LCD readout (JUCE builds the value box with the default LAF in
    // the Slider ctor, so styling it here rather than via createSliderTextBox is
    // what actually takes effect).
    const bool isReadout = dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr;

    if (isReadout)
    {
        g.setColour(col::lcdFill);
        g.fillRoundedRectangle(b.reduced(0.5f), 2.5f);
        g.setColour(col::lcdBorder);
        g.drawRoundedRectangle(b.reduced(0.5f), 2.5f, 1.0f);
    }
    else
    {
        const auto bg = label.findColour(juce::Label::backgroundColourId);
        if (! bg.isTransparent())
        {
            g.setColour(bg);
            g.fillRoundedRectangle(b.reduced(0.5f), 2.5f);
        }
    }

    if (! label.isBeingEdited())
    {
        juce::String text = label.getText();
        // APVTS attachments render the value via the parameter's own formatter (often
        // full float precision, e.g. "440.000305"). For the LCD readout, reformat a
        // purely-numeric value to a clean integer / 2-decimal string (display only).
        if (isReadout && text.isNotEmpty() && text.containsOnly("0123456789.+-eE "))
        {
            // Display-only tidy-up (the parameter value is untouched): large Hz-range
            // values (e.g. filter cutoff 1200.49) drop the decimals — unreadable in the
            // narrow LCD; small values keep 2 decimals; integers stay integer.
            const double d = text.getDoubleValue();
            const double ad = d < 0.0 ? -d : d;
            if (ad >= 100.0)
                text = juce::String(juce::roundToInt(d));
            else if (d == (double) (juce::int64) d)
                text = juce::String((juce::int64) d);
            else
                text = juce::String(d, 2);
        }

        g.setColour((isReadout ? col::lcdText : label.findColour(juce::Label::textColourId))
                     .withMultipliedAlpha(label.isEnabled() ? 1.0f : 0.5f));
        g.setFont(isReadout ? fonts::lcd(10.0f) : label.getFont());
        g.drawFittedText(text, label.getLocalBounds().reduced(isReadout ? 4 : 2, 0),
                         juce::Justification::centred, 1, isReadout ? 0.7f : 0.9f);
    }
}

// ===========================================================================
// Tabs
// ===========================================================================
int AmyLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& b, int)
{
    return (int) juce::GlyphArrangement::getStringWidth(fonts::header(13.0f),
                                                        b.getButtonText().toUpperCase()) + 30;
}

void AmyLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) {}

void AmyLookAndFeel::drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    const bool active = b.isFrontTab();
    auto r = b.getLocalBounds().toFloat();
    if (active) r = r.withTrimmedBottom(0);
    else        r = r.withTrimmedTop(1.0f);   // inactive sit 1px lower

    // Top-rounded tab body.
    juce::Path p;
    p.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight() + 6.0f,
                          4.0f, 4.0f, true, true, false, false);
    g.setColour(active ? col::tabActive : (over ? col::tabInactive.brighter(0.15f) : col::tabInactive));
    g.fillPath(p);

    if (active)   // neutral grey top indicator, inset 2px
    {
        g.setColour(col::tabIndicator);
        g.fillRect(r.getX() + 2.0f, r.getY(), r.getWidth() - 4.0f, 2.0f);
    }

    g.setColour(active ? col::tabTextOn : col::tabTextOff);
    g.setFont(fonts::header(13.0f).withExtraKerningFactor(0.06f));
    g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(),
               juce::Justification::centred, false);
}
} // namespace amyplug
