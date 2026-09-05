// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyLookAndFeel.h"
#include "AmyColours.h"
#include "AmyFonts.h"
#include "ScaledContent.h"
#include <cmath>

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

    // Popup menus. headerTextColourId is easy to miss — section headings otherwise draw in
    // a stock default that happens to be legible, so the miss survives review.
    setColour(juce::PopupMenu::backgroundColourId,            col::panel);
    setColour(juce::PopupMenu::textColourId,                  col::textPrimary);
    setColour(juce::PopupMenu::headerTextColourId,            col::textDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col::engineCyan.withAlpha(0.20f));
    setColour(juce::PopupMenu::highlightedTextColourId,       col::textPrimary);

    // Dialogs.
    setColour(juce::AlertWindow::backgroundColourId, col::shellTop);
    setColour(juce::AlertWindow::textColourId,       col::textPrimary);
    setColour(juce::AlertWindow::outlineColourId,    col::hairline);

    // Text entry (the Save-patch name field, and any editable value box).
    setColour(juce::TextEditor::backgroundColourId,      col::groove);
    setColour(juce::TextEditor::textColourId,            col::textPrimary);
    setColour(juce::TextEditor::outlineColourId,         col::comboBorder);
    setColour(juce::TextEditor::focusedOutlineColourId,  col::engineCyan);
    setColour(juce::TextEditor::highlightColourId,       col::engineCyan.withAlpha(0.30f));
    setColour(juce::TextEditor::highlightedTextColourId, col::textPrimary);
    setColour(juce::TextEditor::shadowColourId,          juce::Colours::transparentBlack);
    setColour(juce::CaretComponent::caretColourId,       col::engineCyan);

    setColour(juce::Label::textColourId, col::textDim);

    // Tooltips. We draw them ourselves below, but the ids are set anyway so anything that
    // reads them (a stock component, a future override) lands on the same palette.
    setColour(juce::TooltipWindow::backgroundColourId, col::panelRaised);
    setColour(juce::TooltipWindow::textColourId,       col::textPrimary);
    setColour(juce::TooltipWindow::outlineColourId,    col::hairline);

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

// ===========================================================================
// Popup menus
// ===========================================================================
// Metrics live here rather than in each hook so an item, a section header and the
// ideal-size calculation can't drift apart.
namespace
{
constexpr int   kMenuRowH     = 26;    // minimum row height (top-bar combos ask for 24)
constexpr float kMenuRadius   = 5.0f;  // menu card corner
constexpr float kMenuMaxText  = 19.0f; // cap on the item font

// Item font, shrunk to fit a short row so text never spills out of its own cell.
juce::Font menuItemFont(int rowHeight)
{
    return fonts::label(juce::jmin(kMenuMaxText, (float) rowHeight * 0.74f));
}
} // namespace

juce::Font AmyLookAndFeel::getPopupMenuFont() { return fonts::label(kMenuMaxText); }

int AmyLookAndFeel::getPopupMenuBorderSize() { return 5; }

// A PopupMenu defaults to its OWN DESKTOP WINDOW: it is not in the editor's component
// hierarchy, so it doesn't move, hide or die with the plugin window — drag the window in
// a DAW with a menu open and the menu stays floating where it opened. Parenting it to the
// editor fixes that and keeps it inside the plugin's own frame. `withTargetComponent`
// (already set by the base) is what makes clicking the combo a second time CLOSE the menu.
juce::PopupMenu::Options AmyLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                        juce::Label& label)
{
    auto opts = LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label)
                    .withStandardItemHeight(juce::jmax(kMenuRowH, label.getHeight()));

    // Parent to the SCALED CONTENT, not to the editor. PopupMenu::MenuWindow skips its own
    // auto-scale path entirely once options.getParentComponent() is set, so a menu parented to
    // the (unscaled) editor would come up at 100% over a 150% panel. As a child of the
    // transform it scales with everything else.
    if (auto* content = box.findParentComponentOfClass<ScaledContent>())
        return opts.withParentComponent(content);

    // No scaled content (a plain editor, or a box not yet in a hierarchy — getTopLevelComponent
    // returns the box itself then, and parenting a menu to the 26px control that opened it
    // would be worse than leaving it on the desktop).
    auto* top = box.getTopLevelComponent();
    return top != &box ? opts.withParentComponent(top) : opts;
}

// Once a menu is parented, PopupMenu::MenuWindow paints a stock resizable frame over the
// border region (two translucent black rects) — right on top of our card edge. We own that
// border in drawPopupMenuBackground, so draw nothing. Safe because the editor is fixed-size:
// nothing else in this plugin puts a resizable frame on screen.
void AmyLookAndFeel::drawResizableFrame(juce::Graphics&, int, int, const juce::BorderSize<int>&) {}

void AmyLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);

    // PopupMenu makes its window opaque whenever backgroundColourId is, so every pixel has
    // to be painted: lay the shell colour down first, then the rounded card on top. The
    // corners then read as faceplate rather than as undrawn noise.
    g.fillAll(col::shellBottom);

    auto card = r.reduced(0.5f);
    g.setGradientFill({ col::panelRaised, card.getCentreX(), card.getY(),
                        col::panel,       card.getCentreX(), card.getBottom(), false });
    g.fillRoundedRectangle(card, kMenuRadius);

    g.setColour(col::hairline);
    g.drawRoundedRectangle(card, kMenuRadius, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));      // 1px top highlight
    g.drawLine(card.getX() + kMenuRadius, card.getY() + 1.5f,
               card.getRight() - kMenuRadius, card.getY() + 1.5f, 1.0f);
}

void AmyLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                               int standardMenuItemHeight,
                                               int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth  = 50;
        idealHeight = 9;
        return;
    }
    idealHeight = juce::jmax(kMenuRowH, standardMenuItemHeight);
    idealWidth  = (int) juce::GlyphArrangement::getStringWidth(menuItemFont(idealHeight), text)
                + idealHeight + 34;      // tick gutter + right padding
}

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
        g.fillRect(r.withHeight(1.0f).withY(std::floor(r.getCentreY())));
        return;
    }

    const auto highlight = findColour(juce::PopupMenu::highlightedBackgroundColourId);
    const auto accent    = highlight.withAlpha(1.0f);

    auto row = area.reduced(3, 1);
    if (isHighlighted && isActive)
    {
        g.setColour(highlight);
        g.fillRoundedRectangle(row.toFloat(), 3.0f);
        g.setColour(accent);                                   // accent edge on the lit row
        g.fillRoundedRectangle(row.toFloat().withWidth(2.5f), 1.0f);
    }

    auto colour = isActive ? (isHighlighted ? findColour(juce::PopupMenu::highlightedTextColourId)
                                            : findColour(juce::PopupMenu::textColourId))
                           : col::textFaint;
    if (textColour != nullptr) colour = *textColour;

    // The tick lives in a fixed left gutter so ticked and unticked rows share a text edge.
    row = row.reduced(7, 0);
    auto gutter = row.removeFromLeft(juce::jmax(19, area.getHeight() * 3 / 4));

    if (icon != nullptr)
    {
        icon->drawWithin(g, gutter.toFloat().reduced(2.0f),
                         juce::RectanglePlacement::centred, 1.0f);
    }
    else if (isTicked)
    {
        auto t = gutter.toFloat().withSizeKeepingCentre(9.0f, 7.0f).translated(-1.5f, 0.0f);
        juce::Path tick;
        tick.startNewSubPath(t.getX(), t.getCentreY());
        tick.lineTo(t.getCentreX() - 1.0f, t.getBottom());
        tick.lineTo(t.getRight(), t.getY());
        g.setColour(isActive ? accent : col::textFaint);
        g.strokePath(tick, { 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    if (hasSubMenu)
    {
        auto a = row.removeFromRight(area.getHeight()).toFloat().reduced((float) area.getHeight() * 0.36f);
        juce::Path arrow;
        arrow.startNewSubPath(a.getX(), a.getY());
        arrow.lineTo(a.getRight(), a.getCentreY());
        arrow.lineTo(a.getX(), a.getBottom());
        g.setColour(colour.withMultipliedAlpha(0.7f));
        g.strokePath(arrow, { 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }
    else if (shortcutKeyText.isNotEmpty())
    {
        const auto scFont = fonts::mono((float) area.getHeight() * 0.5f);
        auto sc = row.removeFromRight((int) juce::GlyphArrangement::getStringWidth(scFont, shortcutKeyText) + 12);
        g.setFont(scFont);
        g.setColour(colour.withMultipliedAlpha(0.55f));
        g.drawFittedText(shortcutKeyText, sc, juce::Justification::centredRight, 1);
    }

    g.setColour(colour);
    g.setFont(menuItemFont(area.getHeight()));
    g.drawFittedText(text, row, juce::Justification::centredLeft, 1);
}

// Bank names in the patch browser ("JUNO", "DX7", "PCM", user groups). Small tracked caps
// over a hairline, matching the section-header bars on the faceplate.
void AmyLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                const juce::String& sectionName)
{
    auto r = area.reduced(10, 0);
    g.setColour(findColour(juce::PopupMenu::headerTextColourId));
    g.setFont(fonts::header(juce::jmin(13.0f, (float) area.getHeight() * 0.40f))
                  .withExtraKerningFactor(0.10f));
    g.drawFittedText(sectionName.toUpperCase(), r.withTrimmedBottom(5),
                     juce::Justification::bottomLeft, 1);
    g.setColour(col::hairline);
    g.fillRect(r.getX(), r.getBottom() - 3, r.getWidth(), 1);
}

void AmyLookAndFeel::drawPopupMenuUpDownArrow(juce::Graphics& g, int width, int height,
                                              bool isScrollUpArrow)
{
    // Drawn over the already-painted card, so only the glyph is needed.
    auto a = juce::Rectangle<float>((float) width, (float) height)
                 .withSizeKeepingCentre(11.0f, 6.0f);
    juce::Path p;
    if (isScrollUpArrow) { p.startNewSubPath(a.getX(), a.getBottom());
                           p.lineTo(a.getCentreX(), a.getY());
                           p.lineTo(a.getRight(), a.getBottom()); }
    else                 { p.startNewSubPath(a.getX(), a.getY());
                           p.lineTo(a.getCentreX(), a.getBottom());
                           p.lineTo(a.getRight(), a.getY()); }
    g.setColour(col::textDim);
    g.strokePath(p, { 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
}

// ===========================================================================
// Tree view
// ===========================================================================
// The stock open/close box is a bordered square with a +/-; a chevron reads as a
// disclosure at this size and matches the combo caret.
void AmyLookAndFeel::drawTreeviewPlusMinusBox(juce::Graphics& g, const juce::Rectangle<float>& area,
                                              juce::Colour, bool isOpen, bool isMouseOver)
{
    auto a = area.withSizeKeepingCentre(7.0f, 7.0f);
    juce::Path p;
    if (isOpen) { p.startNewSubPath(a.getX(), a.getY() + 1.5f); p.lineTo(a.getCentreX(), a.getBottom() - 1.0f); p.lineTo(a.getRight(), a.getY() + 1.5f); }
    else        { p.startNewSubPath(a.getX() + 1.5f, a.getY()); p.lineTo(a.getRight() - 1.0f, a.getCentreY()); p.lineTo(a.getX() + 1.5f, a.getBottom()); }
    g.setColour(isMouseOver ? col::textPrimary : col::textDim);
    g.strokePath(p, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ===========================================================================
// Corner grip
// ===========================================================================
// ResizableCornerComponent draws through this, and the default is bright white/grey
// hatching — on a dark faceplate that reads as a scratch, not a handle. Keep it dim: it is
// a hint that the edge is draggable, not a control competing with the designed ones.
void AmyLookAndFeel::drawCornerResizer(juce::Graphics& g, int w, int h,
                                       bool over, bool dragging)
{
    const auto colour = dragging ? col::engineCyan : over ? col::textDim : col::textFaint;
    g.setColour(colour.withAlpha(over || dragging ? 0.95f : 0.5f));

    const float fw = (float) w, fh = (float) h;
    const float step = juce::jmin(fw, fh) * 0.3f;
    for (int i = 1; i <= 3; ++i)
    {
        const float inset = step * (float) i;
        g.drawLine(fw - inset, fh - 2.0f, fw - 2.0f, fh - inset, 1.2f);
    }
}

// ===========================================================================
// Tooltips
// ===========================================================================
// Both hooks share one layout so the box is always exactly the size of the text it is
// about to draw — measuring with one font and drawing with another is how tooltips end
// up clipped on the last word.
static juce::TextLayout layoutTip(const juce::String& text, juce::Colour ink, float maxWidth)
{
    juce::AttributedString as;
    as.setJustification(juce::Justification::centredLeft);
    as.append(text, fonts::label(15.0f), ink);
    juce::TextLayout tl;
    tl.createLayout(as, maxWidth);
    return tl;
}

static constexpr float kTipMaxW = 300.0f, kTipPadX = 10.0f, kTipPadY = 7.0f;

juce::Rectangle<int> AmyLookAndFeel::getTooltipBounds(const juce::String& tipText,
                                                      juce::Point<int> screenPos,
                                                      juce::Rectangle<int> parentArea)
{
    const auto tl = layoutTip(tipText, juce::Colours::black, kTipMaxW);
    const int w = (int) std::ceil(tl.getWidth())  + (int) (kTipPadX * 2.0f);
    const int h = (int) std::ceil(tl.getHeight()) + (int) (kTipPadY * 2.0f);

    // Flip to the other side of the pointer near an edge, then clamp — the tip is parented
    // into the scaled content, so `parentArea` is the editor and it can never escape the
    // window the way a desktop tooltip does.
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12)
                                                                     : screenPos.x + 18,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8)
                                                                     : screenPos.y + 18,
                                w, h)
             .constrainedWithin(parentArea);
}

void AmyLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto r = juce::Rectangle<float>((float) width, (float) height).reduced(0.5f);
    // A parented tooltip has no peer, so there is no window shadow to inherit — a 1px offset
    // scrim under the card does the lifting instead.
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(r.translated(0.0f, 1.0f), 4.0f);
    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    layoutTip(text, findColour(juce::TooltipWindow::textColourId), (float) width - kTipPadX * 2.0f)
        .draw(g, r.reduced(kTipPadX, kTipPadY));
}

// ===========================================================================
// Dialogs (AlertWindow) and text entry
// ===========================================================================
int        AmyLookAndFeel::getAlertWindowButtonHeight()  { return 30; }
juce::Font AmyLookAndFeel::getAlertWindowTitleFont()     { return fonts::header(21.0f).withExtraKerningFactor(0.05f); }
juce::Font AmyLookAndFeel::getAlertWindowMessageFont()   { return fonts::label(18.0f); }
juce::Font AmyLookAndFeel::getAlertWindowFont()          { return fonts::label(15.0f); }

// drawButtonText upper-cases, so measure the string that actually gets drawn — sizing off
// the mixed-case original clips "Cancel" to "CANCE...".
juce::Array<int> AmyLookAndFeel::getWidthsForTextButtons(juce::AlertWindow&,
                                                         const juce::Array<juce::TextButton*>& buttons)
{
    juce::Array<int> widths;
    const int h = getAlertWindowButtonHeight();
    for (auto* b : buttons)
        widths.add(juce::jmax(92, (int) juce::GlyphArrangement::getStringWidth(
                                       getTextButtonFont(*b, h), b->getButtonText().toUpperCase()) + 34));
    return widths;
}

void AmyLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert,
                                  const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    auto full = alert.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = 6.0f;

    // Same vertical gradient as the plugin shell, so a dialog reads as part of AMYplug
    // rather than as the host's own alert.
    g.setGradientFill({ col::shellTop,    full.getCentreX(), full.getY(),
                        col::shellBottom, full.getCentreX(), full.getBottom(), false });
    g.fillRoundedRectangle(full, radius);

    // Kind is carried by a top accent stripe instead of LookAndFeel_V4's 80px glyph, which
    // eats a third of the dialog and belongs to a different design language.
    const auto accent = alert.getAlertType() == juce::MessageBoxIconType::WarningIcon ? col::amber
                      : alert.getAlertType() == juce::MessageBoxIconType::InfoIcon    ? col::engineCyan
                      : alert.getAlertType() == juce::MessageBoxIconType::QuestionIcon? col::junoBlue
                                                                                      : col::hairline;
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(full, radius);
        g.reduceClipRegion(clip);
        g.setColour(accent);
        g.fillRect(full.withHeight(3.0f));
    }

    g.setColour(col::hairline);
    g.drawRoundedRectangle(full, radius, 1.0f);

    // AlertWindow::updateLayout reserves a fixed 80px column on the left for any icon type
    // and sizes the window around it, so an icon dialog has that space whether we use it or
    // not. Fill it with a compact badge rather than LookAndFeel_V4's 80px glyph.
    const bool hasIcon = alert.getAlertType() != juce::MessageBoxIconType::NoIcon;
    const float iconSpace = hasIcon ? 80.0f : 0.0f;
    if (hasIcon)
    {
        auto badge = juce::Rectangle<float>(34.0f, 34.0f)
                         .withCentre({ full.getX() + iconSpace * 0.5f,
                                       (float) textArea.getY() + 32.0f });
        g.setColour(accent.withAlpha(0.14f));
        g.fillEllipse(badge);
        g.setColour(accent.withAlpha(0.55f));
        g.drawEllipse(badge, 1.2f);
        g.setColour(accent);
        g.setFont(fonts::header(20.0f));
        g.drawText(alert.getAlertType() == juce::MessageBoxIconType::WarningIcon  ? "!"
                 : alert.getAlertType() == juce::MessageBoxIconType::QuestionIcon ? "?" : "i",
                   badge, juce::Justification::centred, false);
    }

    // Title + message. The colours were baked into the layout from AlertWindow::textColourId
    // when it was built, so this only positions it.
    layout.draw(g, textArea.toFloat().withTrimmedLeft(iconSpace)
                                     .withTrimmedTop(14.0f).reduced(8.0f, 0.0f));
}

void AmyLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(0.5f);
    g.setColour(ed.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.35f));      // inset top shadow, as the combo well
    g.drawLine(r.getX() + 3.0f, r.getY() + 1.5f, r.getRight() - 3.0f, r.getY() + 1.5f, 1.0f);
}

void AmyLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    if (! ed.isEnabled()) return;
    auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(0.5f);
    const bool focused = ed.hasKeyboardFocus(true) && ! ed.isReadOnly();
    g.setColour(ed.findColour(focused ? juce::TextEditor::focusedOutlineColourId
                                      : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle(r, 3.0f, focused ? 1.6f : 1.0f);
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
    // Lighter rim of the button's own colour — the rule the panicBorder token encodes for
    // PANIC, generalised so any accent (e.g. a dialog's primary action) gets a matching edge.
    g.setColour(accented ? backgroundColour.withMultipliedBrightness(1.14f) : col::comboBorder);
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
        // Space out the segmented LCD digits — they're hard to read butted together.
        g.setFont(isReadout ? fonts::lcd(10.0f).withExtraKerningFactor(0.12f) : label.getFont());
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
