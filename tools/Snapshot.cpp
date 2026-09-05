// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Headless editor snapshot: renders the AMYplug editor (a given tab) to a PNG
// without opening a window — deterministic and immune to Spaces / compositor
// quirks that block screenshotting the Standalone. Used to iterate on the UI.
//
//   amyplug_snapshot <out.png> [tabIndex] [algo] [scalePercent]
//     tabIndex: 0 Presets · 1 Juno · 2-5 DX7 1-4 · 6 FX-MASTER · 7 AMYboard   (default 0)
//     algo:     1-32 DX7 algorithm, 0 = leave alone
//     scale:    editor size in percent (60-150); the editor opens at whatever the
//               processor has stored, which is what the size picker persists.
//   amyplug_snapshot <out.png> chrome
//     the modal chrome — a popup menu in every item state, both dialogs, and the hover
//     tooltip at its short and its wrapping length. These are
//     modal objects the editor never contains, so they can't appear in a tab snapshot;
//     this renders the LookAndFeel hooks JUCE itself calls. It proves the DRAWING only —
//     parenting, click-to-close and dismissal need a real host (see JUCE-UI-LnF__15).

#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "gui/AmyColours.h"
#include "gui/AmyFonts.h"
#include "gui/AmyLookAndFeel.h"
#include "state/Parameters.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <memory>
#include <vector>

namespace
{
void writePng(const juce::Image& img, const juce::String& outPath)
{
    juce::File outFile = outPath.startsWithChar('/')
                       ? juce::File(outPath)
                       : juce::File::getCurrentWorkingDirectory().getChildFile(outPath);
    outFile.deleteFile();
    if (auto os = outFile.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream(img, *os);
    }
    juce::Logger::writeToLog("wrote " + outFile.getFullPathName());
}

// One menu row, as PopupMenu::ItemComponent would hand it to the LookAndFeel.
struct MenuRow
{
    juce::String text;
    bool header = false, separator = false, ticked = false, highlighted = false;
    bool active = true, subMenu = false;
    juce::String shortcut;
};

// Reproduce what PopupMenu::MenuWindow does: size every row through
// getIdealPopupMenuItemSize, then paint the background once and each row into its own
// slice. Same hooks, same order — so what this draws is what a real menu draws.
void drawMenu(juce::Graphics& g, amyplug::AmyLookAndFeel& lnf,
              juce::Rectangle<int> at, const std::vector<MenuRow>& rows, int standardRowH)
{
    const int border = lnf.getPopupMenuBorderSize();
    int w = 0, h = 0;
    std::vector<int> heights;
    for (const auto& r : rows)
    {
        int iw = 0, ih = 0;
        if (r.header) lnf.getIdealPopupMenuSectionHeaderSizeWithOptions(r.text, standardRowH, iw, ih, {});
        else          lnf.getIdealPopupMenuItemSize(r.text, r.separator, standardRowH, iw, ih);
        w = juce::jmax(w, iw);
        heights.push_back(ih);
        h += ih;
    }
    at = at.withSize(juce::jmax(at.getWidth(), w + border * 2), h + border * 2);

    juce::Graphics::ScopedSaveState save(g);
    // A real MenuWindow paints into its own bounds, so drawPopupMenuBackground's fillAll is
    // clipped for free. Here it is not — clip, or one menu erases the whole sheet.
    g.reduceClipRegion(at);
    g.setOrigin(at.getPosition());
    lnf.drawPopupMenuBackground(g, at.getWidth(), at.getHeight());

    int y = border;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        juce::Rectangle<int> cell(border, y, at.getWidth() - border * 2, heights[i]);
        if (r.header)
            lnf.drawPopupMenuSectionHeader(g, cell, r.text);
        else
            lnf.drawPopupMenuItem(g, cell, r.separator, r.active, r.highlighted, r.ticked,
                                  r.subMenu, r.text, r.shortcut, nullptr, nullptr);
        y += heights[i];
    }
}

// Paint a real AlertWindow offscreen. It is a top-level window that is never made
// visible, so nothing appears on screen — but it lays itself out and paints through
// exactly the hooks a live dialog uses.
void drawDialog(juce::Graphics& g, juce::AlertWindow& w, juce::Point<int> at)
{
    juce::Graphics::ScopedSaveState save(g);
    g.setOrigin(at);
    w.paintEntireComponent(g, false);
}

// A tooltip, sized and painted through the same two hooks TooltipWindow calls. The real
// window is a child of the scaled content and only appears after a hover dwell, so this is
// the only way to see it without a host. `at` is the mouse position it is pointing at.
void drawTip(juce::Graphics& g, amyplug::AmyLookAndFeel& lnf,
             const juce::String& text, juce::Point<int> at, juce::Rectangle<int> parentArea)
{
    const auto box = lnf.getTooltipBounds(text, at, parentArea);
    juce::Graphics::ScopedSaveState save(g);
    g.setOrigin(box.getPosition());
    lnf.drawTooltip(g, text, box.getWidth(), box.getHeight());
}

int renderChrome(const juce::String& outPath)
{
    namespace col = amyplug::colours;
    amyplug::AmyLookAndFeel lnf;

    constexpr int kW = 1280, kH = 700;
    juce::Image img(juce::Image::ARGB, kW, kH, true);
    juce::Graphics g(img);
    g.setGradientFill({ col::shellTop, 0.0f, 0.0f, col::shellBottom, 0.0f, (float) kH, false });
    g.fillAll();

    // --- patch-browser menu: bank headings, a ticked selection, a lit row -------
    drawMenu(g, lnf, { 40, 40, 260, 0 },
             { { "Juno-6", true }, { "Brass Ensemble" }, { "Strings 1", false, false, true },
               { "Fat Bass", false, false, false, true }, { "Hollow Pad" },
               { "DX7", true }, { "E.Piano 1" }, { "Tubular Bells" },
               { "", false, true }, { "Disabled entry", false, false, false, false, false },
               { "More engines", false, false, false, false, true, true },
               { "Copy", false, false, false, false, true, false, "Cmd+C" } }, 26);

    // --- a control combo's menu: short list, current value ticked ---------------
    drawMenu(g, lnf, { 360, 40, 150, 0 },
             { { "Sine" }, { "Pulse", false, false, true }, { "Saw Down" },
               { "Triangle", false, false, false, true }, { "Noise" } }, 28);

    // --- the two dialogs --------------------------------------------------------
    juce::AlertWindow save("Save User Patch", "Patch name:", juce::MessageBoxIconType::NoIcon);
    save.setLookAndFeel(&lnf);
    save.addTextEditor("name", "My Patch");
    save.addButton("Save",   1);
    save.addButton("Cancel", 0);
    amyplug::styleDialogChrome(save);
    drawDialog(g, save, { 620, 40 });

    juce::AlertWindow info("DX7 Import", "32 voices imported into your USER patches.",
                           juce::MessageBoxIconType::InfoIcon);
    info.setLookAndFeel(&lnf);
    info.addButton("OK", 1);
    amyplug::styleDialogChrome(info);
    drawDialog(g, info, { 620, 300 });

    save.setLookAndFeel(nullptr);
    info.setLookAndFeel(nullptr);

    // --- tooltips: one that fits on a line, one long enough to wrap -------------
    const juce::Rectangle<int> sheet(0, 0, kW, kH);
    drawTip(g, lnf, "Reverb on or off", { 60, 500 }, sheet);
    drawTip(g, lnf, "This oscillator's pitch at A4 (note 69); it tracks the keyboard from "
                    "there. 440 is normal, 220 an octave down.", { 360, 500 }, sheet);

    g.setColour(col::textFaint);
    g.setFont(amyplug::fonts::mono(13.0f));
    g.drawText(juce::String::fromUTF8("popup menus + dialogs + tooltips \xe2\x80\x94 LookAndFeel hooks, rendered offscreen"),
               12, kH - 24, 900, 16, juce::Justification::centredLeft);

    writePng(img, outPath);
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // Run as a background process: no Dock icon, never steals focus or foregrounds a window.
    // We only ever render offscreen (paintEntireComponent), so nothing should appear on screen.
    juce::Process::setDockIconVisible(false);

    const juce::String outPath = (argc > 1) ? juce::String(argv[1]) : juce::String("amyplug_snapshot.png");
    const juce::String tabArg = (argc > 2) ? juce::String(argv[2]) : juce::String("0");
    if (tabArg == "chrome")
        return renderChrome(outPath);

    const int tab = tabArg.getIntValue();
    const int algo = (argc > 3) ? juce::String(argv[3]).getIntValue() : 0;   // optional DX7 algorithm
    const int scale = (argc > 4) ? juce::String(argv[4]).getIntValue() : 0;  // optional editor size %

    amyplug::AmyPlugProcessor proc;
    proc.prepareToPlay(48000.0, 512);   // acquire the engine so the status reads "SOFTWARE"
    // Set BEFORE the editor is built: the editor reads the stored size during construction,
    // which is exactly the path the size picker's regression lives in.
    if (scale > 0) proc.setUiScalePercent(scale);

    // Open the tab's matching engine so the panel reads as active (Juno->Analog,
    // DX7->FM). Set before constructing the editor, which picks its tab from this.
    if (auto* e = proc.apvts().getParameter(amyplug::params::id::engine))
    {
        const int eng = (tab == 1) ? 1 : (tab >= 2 && tab <= 5) ? 2 : 0;
        e->setValueNotifyingHost(e->convertTo0to1((float) eng));
    }
    if (algo >= 1 && algo <= 32)
        if (auto* a = proc.apvts().getParameter(amyplug::params::id::fmAlgorithm))
            a->setValueNotifyingHost(a->convertTo0to1((float) (algo - 1)));   // 0-based choice index

    auto editor = std::make_unique<amyplug::AmyPlugEditor>(proc);
    editor->setBounds(0, 0, editor->getWidth(), editor->getHeight());   // the editor's own size
    editor->selectTab(tab);

    // Let any async layout / repaint settle.
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(120);

    juce::Image img(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    {
        juce::Graphics g(img);
        editor->paintEntireComponent(g, false);
    }

    writePng(img, outPath);
    return 0;
}
