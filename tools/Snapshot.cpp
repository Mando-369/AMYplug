// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Headless editor snapshot: renders the AMYplug editor (a given tab) to a PNG
// without opening a window — deterministic and immune to Spaces / compositor
// quirks that block screenshotting the Standalone. Used to iterate on the UI.
//
//   amyplug_snapshot <out.png> [tabIndex]
//     tabIndex: 0 Juno · 1-4 DX7 1-4 · 5 FX-MASTER · 6 AMYboard   (default 0)

#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "state/Parameters.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <memory>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // Run as a background process: no Dock icon, never steals focus or foregrounds a window.
    // We only ever render offscreen (paintEntireComponent), so nothing should appear on screen.
    juce::Process::setDockIconVisible(false);

    const juce::String outPath = (argc > 1) ? juce::String(argv[1]) : juce::String("amyplug_snapshot.png");
    const int tab = (argc > 2) ? juce::String(argv[2]).getIntValue() : 0;
    const int algo = (argc > 3) ? juce::String(argv[3]).getIntValue() : 0;   // optional DX7 algorithm

    amyplug::AmyPlugProcessor proc;
    proc.prepareToPlay(48000.0, 512);   // acquire the engine so the status reads "SOFTWARE"

    // Open the tab's matching engine so the panel reads as active (Juno->Analog,
    // DX7->FM). Set before constructing the editor, which picks its tab from this.
    if (auto* e = proc.apvts().getParameter(amyplug::params::id::engine))
    {
        const int eng = (tab == 0) ? 1 : (tab >= 1 && tab <= 4) ? 2 : 0;
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
    return 0;
}
