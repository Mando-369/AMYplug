// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Hover help. Two failure modes are guarded here, and the first one is the reason these
// tests exist at all:
//
//   1. `setTooltip` on its own does NOTHING. A tip is only ever shown by a live
//      TooltipWindow, and until the "?" work there was none anywhere in the plugin — every
//      setTooltip call in the editor was dead code that read as a working feature. So the
//      assertion that matters is "a TooltipWindow exists, inside the editor", not "the
//      button has a tooltip string".
//   2. A Slider copies its owner's tooltip into its value box when the box is BUILT and
//      never again (juce_Slider.cpp:606), so the knob can carry a tip while its LCD
//      read-out shows nothing. That is what setTooltipDeep exists for; it is checked here
//      by looking at the CHILDREN of a knob, not the knob itself.
//
// Needs a real editor, so it lives in the ui_tests target.
#include <catch2/catch_test_macros.hpp>

#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "gui/Tooltips.h"
#include "state/Parameters.h"
#include "gui/AmyLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace
{
namespace tips = amyplug::tips;

template <typename T, typename Fn>
T* findComponent(juce::Component& root, Fn&& pred)
{
    for (auto* child : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(child); t != nullptr && pred(*t)) return t;
        if (auto* nested = findComponent<T>(*child, pred)) return nested;
    }
    return nullptr;
}

// Every component in the tree that could show a tip, and how many actually carry one.
struct TipCensus { int clients = 0, withTip = 0; };
void census(juce::Component& root, TipCensus& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* c = dynamic_cast<juce::SettableTooltipClient*>(child))
        {
            ++out.clients;
            if (c->getTooltip().isNotEmpty()) ++out.withTip;
        }
        census(*child, out);
    }
}

void pump(int ms)
{
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(ms);
}
} // namespace

// ---------------------------------------------------------------------------
// The id -> text table. The generated ids (fm_op<N>_<field>, osc_<x>_<field>) are the
// ones that can silently stop matching, because nothing else in the build spells them out.
// ---------------------------------------------------------------------------
TEST_CASE("tooltip table resolves generated parameter ids", "[ui]")
{
    // Per-operator FM: every operator, every field, must land on the same text.
    for (int op = 1; op <= amyplug::params::kFmOps; ++op)
    {
        INFO("operator " << op);
        CHECK(tips::forParam(amyplug::params::id::fmOp(op, "coarse")).startsWith("Frequency ratio"));
        CHECK(tips::forParam(amyplug::params::id::fmOp(op, "outlvl")).isNotEmpty());
        CHECK(tips::forParam(amyplug::params::id::fmOp(op, "ams")).isNotEmpty());
        CHECK(tips::forParam(amyplug::params::id::fmOp(op, "r3")).isNotEmpty());
        CHECK(tips::forParam(amyplug::params::id::fmOp(op, "l4")).isNotEmpty());
    }

    // Pitch EG: same 4R/4L shape, different meaning (50 = no shift), so it must NOT fall
    // through to the operator table.
    CHECK(tips::forParam(amyplug::params::id::fmPitchEg('l', 3)).contains("50"));
    CHECK(tips::forParam(amyplug::params::id::fmPitchEg('r', 1)).startsWith("Pitch envelope"));
    CHECK(tips::forParam(amyplug::params::id::fmPitchEg('l', 3))
          != tips::forParam(amyplug::params::id::fmOp(3, "l3")));

    // Analog oscillators: the four of them share one set of texts, matched on the suffix.
    for (const char* id : { "osc_a_freq", "osc_b_freq", "osc_c_freq", "osc_d_freq" })
        CHECK(tips::forParam(id) == tips::forParam("osc_a_freq"));
    CHECK(tips::forParam("osc_c_duty").contains("Pulse"));

    // A parameter with no row gets an empty string, not a placeholder — JUCE then shows
    // nothing at all, which is the right answer for a self-explanatory control.
    CHECK(tips::forParam("no_such_parameter").isEmpty());
}

// ---------------------------------------------------------------------------
// Width. The box is capped at 200px in the LookAndFeel, and a tip that overflows it would
// be clipped rather than wrapped — so this walks EVERY parameter the plugin has plus the
// chrome strings, which is the whole table by construction. A tip added later that is too
// long for one word to fit fails here rather than in a screenshot.
// ---------------------------------------------------------------------------
TEST_CASE("no tooltip is drawn wider than the cap", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    amyplug::AmyLookAndFeel lnf;
    const juce::Rectangle<int> surface(0, 0, 1280, 830);   // the design surface
    const juce::Point<int> at(200, 200);                   // top-left quadrant: no edge flip

    auto widthOf = [&] (const juce::String& tip)
    { return lnf.getTooltipBounds(tip, at, surface).getWidth(); };

    amyplug::AmyPlugProcessor proc;
    int checked = 0;
    for (auto* p : proc.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (rp == nullptr) continue;
        const auto tip = tips::forParam(rp->paramID);
        if (tip.isEmpty()) continue;
        INFO(rp->paramID << ": " << tip);
        CHECK(widthOf(tip) <= 200);
        ++checked;
    }
    CHECK(checked > 80);   // it really did walk the table, not an empty list

    for (const char* tip : { tips::help, tips::size, tips::preset, tips::prev, tips::next,
                             tips::save, tips::import, tips::toEditor, tips::engine,
                             tips::panic, tips::outGain, tips::status, tips::takeover,
                             tips::pSearch, tips::pTree, tips::pList, tips::pSave,
                             tips::pSaveAs, tips::pRename, tips::pMove, tips::pDelete,
                             tips::pNewBank, tips::pReveal, tips::hwMidi, tips::hwSerial,
                             tips::hwRefresh, tips::hwDetect, tips::hwConnect,
                             tips::hwDisconn, tips::hwSend, tips::hwFirmware, tips::hwFlash })
    {
        INFO(tip);
        CHECK(widthOf(tip) <= 200);
    }
    for (const char* tip : tips::tabs)
    {
        INFO(tip);
        CHECK(widthOf(tip) <= 200);
    }
}

// ---------------------------------------------------------------------------
// The wiring. One editor, several assertions — Catch2 re-runs a case body per SECTION and
// that would cycle JUCE's GUI init repeatedly.
// ---------------------------------------------------------------------------
TEST_CASE("hover help is wired into a live editor", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    amyplug::AmyPlugProcessor proc;
    auto editor = std::make_unique<amyplug::AmyPlugEditor>(proc);
    editor->setBounds(0, 0, editor->getWidth(), editor->getHeight());
    pump(120);

    // --- 1. a TooltipWindow exists, and is INSIDE the editor --------------------
    // Parented, not on the desktop: an unparented one draws at 100% beside a panel scaled
    // to 150%, escapes the plugin window, and multiplies with every open instance.
    auto* tipWindow = findComponent<amyplug::TipWindow>(*editor, [] (auto&) { return true; });
    REQUIRE(tipWindow != nullptr);
    CHECK(editor->isParentOf(tipWindow));

    // --- 2. the tips actually reached the controls -----------------------------
    // ⚠️ A TabbedComponent only keeps the CURRENT tab's page in the component tree, so a
    // census of the editor as it opens sees the Presets tab and nothing else — which is
    // exactly how this assertion could have passed at 34 while eight tabs went untipped.
    // Walk every tab.
    TipCensus total;
    for (int tab = 0; tab <= 7; ++tab)
    {
        editor->selectTab(tab);
        pump(20);
        TipCensus c;
        census(*editor, c);
        INFO("tab " << tab << ": " << c.withTip << " of " << c.clients << " carry a tip");
        CHECK(c.withTip > 20);            // no tab is left bare
        total.clients += c.clients;
        total.withTip += c.withTip;
    }
    INFO(total.withTip << " of " << total.clients << " tooltip-capable components carry a tip");
    CHECK(total.withTip > 600);   // 722 of 749 at the time of writing; only ever grows

    // --- 3. a knob's LCD read-out carries the tip too ---------------------------
    // The bug this guards: Slider seeds its value box at construction, so a tooltip set
    // afterwards reaches the knob and not the read-out under it.
    editor->selectTab(1);   // Juno — where the filter lives
    pump(20);
    auto* knob = findComponent<juce::Slider>(*editor, [] (juce::Slider& s)
                 { return s.getTooltip() == tips::forParam("filter_cutoff"); });
    REQUIRE(knob != nullptr);
    TipCensus kids;
    census(*knob, kids);
    CHECK(kids.clients > 0);              // the value box is there
    CHECK(kids.withTip == kids.clients);  // ...and every one of them knows the tip

    // --- 4. the "?" toggle actually gates the window ----------------------------
    auto* help = findComponent<juce::TextButton>(*editor, [] (juce::TextButton& b)
                 { return b.getButtonText() == "?"; });
    REQUIRE(help != nullptr);
    CHECK(help->getToggleState());                       // on by default
    CHECK(proc.helpTipsEnabled());

    // ⚠️ Two things that look like the obvious assertions here are NOT usable:
    //   - getTipFor returns nothing unless the process is in the FOREGROUND, and a console
    //     test never is, so it answers the same in both states;
    //   - the window's VISIBILITY is taken down by its own 123 ms poll the moment no
    //     component is under the mouse, so a check after a pump passes either way.
    // Both were tried and both passed with the toggle deleted. The switch's own state is
    // the thing that is actually observable, so that is what is asserted.
    CHECK(tipWindow->tipsEnabled());

    help->triggerClick();
    pump(50);
    CHECK_FALSE(help->getToggleState());
    CHECK_FALSE(proc.helpTipsEnabled());
    CHECK_FALSE(tipWindow->tipsEnabled());

    help->triggerClick();
    pump(50);
    CHECK(help->getToggleState());
    CHECK(proc.helpTipsEnabled());
    CHECK(tipWindow->tipsEnabled());
    editor.reset();
}

// ---------------------------------------------------------------------------
// The preference outlives the editor. A host destroys the editor on every window close, so
// anything the editor alone remembers is lost — the same reason the size lives on the
// processor.
// ---------------------------------------------------------------------------
TEST_CASE("the help preference survives a session round-trip", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::MemoryBlock saved;
    {
        amyplug::AmyPlugProcessor proc;
        CHECK(proc.helpTipsEnabled());          // on by default: AMY is not a familiar synth
        proc.setHelpTipsEnabled(false);
        proc.getStateInformation(saved);
    }
    {
        amyplug::AmyPlugProcessor proc;
        proc.setStateInformation(saved.getData(), (int) saved.getSize());
        CHECK_FALSE(proc.helpTipsEnabled());
    }
    // A session written before the toggle existed has no property at all, and must come
    // back with help available rather than silently suppressed.
    {
        amyplug::AmyPlugProcessor writer, reader;
        juce::MemoryBlock older;
        writer.getStateInformation(older);
        if (auto xml = juce::AudioProcessor::getXmlFromBinary(older.getData(), (int) older.getSize()))
        {
            xml->removeAttribute("helpTips");
            juce::MemoryBlock stripped;
            juce::AudioProcessor::copyXmlToBinary(*xml, stripped);
            reader.setHelpTipsEnabled(false);
            reader.setStateInformation(stripped.getData(), (int) stripped.getSize());
            CHECK(reader.helpTipsEnabled());
        }
    }
}
