// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AmyPlugProcessor.h"
#include <memory>
#include <vector>

namespace amyplug
{
// A panel of labelled controls (rotaries + choice combos) grouped into titled
// sections laid out in columns — used for the Juno engine tab and the FX rack.
class ControlPanel : public juce::Component
{
public:
    explicit ControlPanel(juce::AudioProcessorValueTreeState& s) : apvts(s) {}

    void addSection(const juce::String& title);
    void addKnob(const juce::String& paramId, const juce::String& name);
    void addChoice(const juce::String& paramId, const juce::String& name);

    void setColumns(int n) { columns = juce::jmax(1, n); }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;
    struct Control
    {
        int          section = 0;
        juce::Label  label;
        std::unique_ptr<juce::Slider>   knob;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<Apvts::SliderAttachment>   ka;
        std::unique_ptr<Apvts::ComboBoxAttachment> ca;
    };

    juce::AudioProcessorValueTreeState& apvts;
    juce::StringArray sectionTitles;
    std::vector<std::unique_ptr<Control>> controls;
    int columns = 4;
};

// Centered message for not-yet-built tabs.
class PlaceholderPanel : public juce::Component
{
public:
    explicit PlaceholderPanel(juce::String msg) : text(std::move(msg)) {}
    void paint(juce::Graphics&) override;
private:
    juce::String text;
};

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
    void buildPatchBox();
    void refreshUserBox();
    void stepPatch(int delta);
    void selectPatch(int patchNumber);
    void showSaveDialog();

    AmyPlugProcessor& proc;

    // Global top bar.
    juce::ComboBox   patchBox, userBox;
    juce::TextButton panicButton  { "PANIC" };
    juce::TextButton prevButton   { "<" };
    juce::TextButton nextButton   { ">" };
    juce::TextButton saveButton   { "Save..." };
    juce::TextButton deleteButton { "Delete" };
    juce::ToggleButton engineToggle { "Analog" };
    juce::Label      browserLabel { {}, "PATCH" };
    juce::Label      userLabel    { {}, "USER" };
    std::unique_ptr<Apvts::ButtonAttachment> engineAtt;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    ControlPanel     junoPanel { proc.apvts() };   // owned by the tab once added
    ControlPanel     fxPanel   { proc.apvts() };   // global FX rack (right column)

    int lastPatch = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugEditor)
};
} // namespace amyplug
