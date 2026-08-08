// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Headless AMYplugFX editor snapshot: renders the FX plugin's editor to a PNG
// without opening a window. Separate target from amyplug_snapshot because both
// plugins define createPluginFilter() — linking them together would be a
// duplicate symbol.
//
//   amyplugfx_snapshot <out.png>

#include "fx/FxProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <memory>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // Background process: no Dock icon, never steals focus (we only draw offscreen).
    juce::Process::setDockIconVisible(false);

    const juce::String outPath = (argc > 1) ? juce::String(argv[1]) : juce::String("amyplugfx_snapshot.png");

    amyplug::FxProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    editor->setBounds(0, 0, editor->getWidth(), editor->getHeight());   // the editor's own size

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
