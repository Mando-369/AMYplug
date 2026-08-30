// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Editor size picker. The regression these guard is in CONSTRUCTION, not in the menu:
// setResizeLimits clamps the editor's current bounds — 0x0 while it is being built — to the
// minimum, which fires resized(), which writes the size back to the processor. Read the
// stored size after that and the plugin opens at 60% forever, while every menu item still
// works perfectly. So the assertion that matters is the width the editor OPENED at.
// See Code Repo/JUCE-UI-LnF__13 §5 and §6.
//
// Everything here needs a real editor, so this is its own target (it links the processor,
// libamy and the bundled fonts). One TEST_CASE, no SECTIONs: Catch2 re-runs a case body per
// section, and that would cycle JUCE's GUI init/shutdown repeatedly.
#include <catch2/catch_test_macros.hpp>

#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace
{
constexpr int kBaseW = 1280;   // must match AmyPlugEditor::kBaseWidth / kBaseHeight
constexpr int kBaseH = 830;

bool hasCornerResizer(juce::Component& editor)
{
    for (auto* child : editor.getChildren())
        if (dynamic_cast<juce::ResizableCornerComponent*>(child) != nullptr)
            return true;
    return false;
}

// The picker button doubles as the readout, so find it by what it shows rather than by
// reaching into the editor's privates.
juce::String percentReadout(juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (auto* b = dynamic_cast<juce::TextButton*>(child))
            if (b->getButtonText().endsWithChar('%'))
                return b->getButtonText();
        const auto nested = percentReadout(*child);
        if (nested.isNotEmpty()) return nested;
    }
    return {};
}
} // namespace

TEST_CASE("editor size picker", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // --- the setter clamps, so a corrupt session can't produce an unusable window ---
    {
        amyplug::AmyPlugProcessor proc;
        CHECK(proc.uiScalePercent() == 100);          // default, for pre-picker sessions
        proc.setUiScalePercent(5000);
        CHECK(proc.uiScalePercent() == amyplug::AmyPlugProcessor::kUiScaleMax);
        proc.setUiScalePercent(-3);
        CHECK(proc.uiScalePercent() == amyplug::AmyPlugProcessor::kUiScaleMin);
    }

    // --- opens at the DESIGN size when nothing has been stored ---------------------
    {
        amyplug::AmyPlugProcessor proc;
        auto editor = std::make_unique<amyplug::AmyPlugEditor>(proc);
        const int openedWidth  = editor->getWidth();     // capture before anything resizes it
        const int openedHeight = editor->getHeight();
        CHECK(openedWidth  == kBaseW);
        CHECK(openedHeight == kBaseH);
        CHECK(hasCornerResizer(*editor));                // the menu is presets, not a cage
    }

    // --- opens at the STORED size (the §5 trap) ------------------------------------
    {
        amyplug::AmyPlugProcessor proc;
        proc.setUiScalePercent(125);
        auto editor = std::make_unique<amyplug::AmyPlugEditor>(proc);
        const int openedWidth = editor->getWidth();
        CHECK(openedWidth == kBaseW * 125 / 100);
        CHECK(proc.uiScalePercent() == 125);             // construction must not clobber it
    }

    // --- every offered preset is inside the resize limits -------------------------
    {
        amyplug::AmyPlugProcessor proc;
        auto editor = std::make_unique<amyplug::AmyPlugEditor>(proc);
        for (int percent : { 75, 100, 125, 150 })
        {
            editor->setSize(kBaseW * percent / 100, kBaseH * percent / 100);
            INFO("preset " << percent << "% was clamped by the constrainer");
            CHECK(proc.uiScalePercent() == percent);
        }
    }

    // --- a CUSTOM (corner-dragged) size takes a different path in than the menu ----
    {
        amyplug::AmyPlugProcessor proc;
        auto editor = std::make_unique<amyplug::AmyPlugEditor>(proc);
        editor->setSize(kBaseW * 115 / 100, kBaseH * 115 / 100);   // as if dragged
        CHECK(proc.uiScalePercent() == 115);
        CHECK(percentReadout(*editor) == "115%");                  // readout follows the drag

        juce::MemoryBlock state;
        proc.getStateInformation(state);

        amyplug::AmyPlugProcessor restored;
        restored.setStateInformation(state.getData(), (int) state.getSize());
        CHECK(restored.uiScalePercent() == 115);

        auto reopened = std::make_unique<amyplug::AmyPlugEditor>(restored);
        CHECK(reopened->getWidth() == kBaseW * 115 / 100);
    }
}
