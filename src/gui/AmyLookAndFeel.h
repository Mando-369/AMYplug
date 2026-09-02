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

    // Popup menus ------------------------------------------------------------
    // A PopupMenu is NOT a child of the editor, so none of this is inherited: the
    // menu takes the LookAndFeel it is handed, and defaults to its own desktop
    // window unless parented. Every hook below has to be overridden explicitly or
    // the menu comes out in stock JUCE metrics on our faceplate. See
    // `Code Repo/JUCE-UI-LnF__15-PopupMenu-in-a-Plugin-Editor.md`.
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                   int standardMenuItemHeight,
                                   int& idealWidth, int& idealHeight) override;
    void drawPopupMenuUpDownArrow(juce::Graphics&, int width, int height,
                                  bool isScrollUpArrow) override;
    int  getPopupMenuBorderSize() override;
    juce::Font getPopupMenuFont() override;
    // A parented menu gets a stock frame painted over its border; we own that edge.
    void drawResizableFrame(juce::Graphics&, int w, int h, const juce::BorderSize<int>&) override;

    // Tree view (the Presets tab's directory) ------------------------------
    void drawTreeviewPlusMinusBox(juce::Graphics&, const juce::Rectangle<float>& area,
                                  juce::Colour background, bool isOpen, bool isMouseOver) override;

    // Corner grip -----------------------------------------------------------
    void drawCornerResizer(juce::Graphics&, int w, int h, bool over, bool dragging) override;

    // Dialogs (AlertWindow) --------------------------------------------------
    // Same story as popup menus: an AlertWindow is a top-level window, so it only
    // gets this LookAndFeel if it is handed it or parented into the editor.
    void drawAlertBox(juce::Graphics&, juce::AlertWindow&,
                      const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int  getAlertWindowButtonHeight() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Array<int> getWidthsForTextButtons(juce::AlertWindow&,
                                             const juce::Array<juce::TextButton*>&) override;

    // Text entry (the Save-patch name field) ---------------------------------
    void fillTextEditorBackground(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;

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
