// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AmyPlugProcessor.h"
#include <memory>
#include <vector>

namespace amyplug
{
// Editor v1: a named patch browser (built-in banks + user patches), save/load, and
// a labelled macro panel. Grows into per-engine panels (FM / PCM / modular) later.
class AmyPlugEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit AmyPlugEditor(AmyPlugProcessor&);
    ~AmyPlugEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    void timerCallback() override;
    void buildPatchBox();        // fill the built-in browser (grouped, named)
    void refreshUserBox();       // fill the user-patch list
    void stepPatch(int delta);   // prev/next built-in
    void selectPatch(int patchNumber);
    void showSaveDialog();

    // A labelled rotary bound to an APVTS parameter.
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<Apvts::SliderAttachment> attachment;
    };
    Knob& addKnob(const juce::String& paramId, const juce::String& text);

    AmyPlugProcessor& proc;

    juce::ComboBox   modeBox, patchBox, userBox;
    juce::TextButton panicButton  { "PANIC" };
    juce::TextButton prevButton   { "<" };
    juce::TextButton nextButton   { ">" };
    juce::TextButton saveButton   { "Save..." };
    juce::TextButton deleteButton { "Delete" };
    juce::Label      browserLabel { {}, "PATCH" };
    juce::Label      userLabel    { {}, "USER" };

    std::unique_ptr<Apvts::ComboBoxAttachment> modeAtt;
    std::vector<std::unique_ptr<Knob>>         knobs;

    int lastPatch = -1;          // last patchA value reflected into patchBox

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugEditor)
};
} // namespace amyplug
