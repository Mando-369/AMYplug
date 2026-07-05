// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace amyplug
{
// The AMYplug visual identity as a JUCE LookAndFeel: vector-drawn rotary knobs
// (arc ring + glowing pointer + inset face), LCD-style numeric readouts, inset
// combo boxes, raised buttons, and neutral tabs — all from the design handoff
// (visual/design_handoff_amyplug). Knob/label accent colour comes from each
// control's own colour ids so sections can be colour-coded.
class AmyLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AmyLookAndFeel();

    // Knobs -----------------------------------------------------------------
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;

    // Combo boxes -----------------------------------------------------------
    void drawComboBox(juce::Graphics&, int w, int h, bool isDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;

    // Buttons ---------------------------------------------------------------
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    // Labels ----------------------------------------------------------------
    void drawLabel(juce::Graphics&, juce::Label&) override;

    // Tabs ------------------------------------------------------------------
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyLookAndFeel)
};
} // namespace amyplug
