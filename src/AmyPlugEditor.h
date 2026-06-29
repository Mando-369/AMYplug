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

    void setCellSize(int w, int h) { cellW = w; rowH = h; }
    int  preferredHeight() const;     // total height needed for all sections
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
    int cellW = 88, rowH = 100;
    static constexpr int kTitleH = 20, kGap = 8;
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

// Draws the selected FM algorithm's operator graph (modulators stacked above the
// carriers they feed, feedback marked) so you can see which OP does what.
class AlgorithmDiagram : public juce::Component
{
public:
    void setAlgorithm(int a) { if (a != algo) { algo = a; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    int algo = 1;
};

// The DX7 tab: a top row with the algorithm diagram + selector + feedback knob,
// and the scrolling per-operator controls below.
class Dx7TabComponent : public juce::Component
{
public:
    Dx7TabComponent(juce::AudioProcessorValueTreeState& apvts,
                    AlgorithmDiagram& diagram, juce::Component& controls);
    void resized() override;
    static constexpr int kTopH = 188;
private:
    using Apvts = juce::AudioProcessorValueTreeState;
    AlgorithmDiagram& diagram;
    juce::Component&   controlsView;
    juce::ComboBox     algoBox;
    juce::Slider       fbKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label        algoLabel, fbLabel;
    std::unique_ptr<Apvts::ComboBoxAttachment> algoAtt;
    std::unique_ptr<Apvts::SliderAttachment>   fbAtt;
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
    void importDx7();

    AmyPlugProcessor& proc;

    // Global top bar.
    juce::ComboBox   patchBox, userBox;
    juce::TextButton panicButton  { "PANIC" };
    juce::TextButton prevButton   { "<" };
    juce::TextButton nextButton   { ">" };
    juce::TextButton saveButton   { "Save..." };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton importButton { "Import DX7..." };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ComboBox   engineBox;                    // Factory / Analog / FM
    juce::Label      browserLabel { {}, "PATCH" };
    juce::Label      userLabel    { {}, "USER" };
    juce::Label      engineLabel  { {}, "ENGINE" };
    std::unique_ptr<Apvts::ComboBoxAttachment> engineAtt;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::Viewport   junoViewport;                 // scrolls the Juno panel if tall
    ControlPanel     junoPanel { proc.apvts() };   // viewed by junoViewport
    juce::Viewport   fmViewport;                    // scrolls the FM (DX7) panel
    ControlPanel     fmPanel   { proc.apvts() };    // viewed by fmViewport
    AlgorithmDiagram algoDiagram;                    // operator graph for the DX7 tab
    Dx7TabComponent  dx7Tab { proc.apvts(), algoDiagram, fmViewport };
    ControlPanel     fxPanel   { proc.apvts() };   // global FX rack (right column)

    void setEngineIndex(int idx);   // 0 Factory, 1 Analog, 2 FM

    std::vector<PatchLibrary::Entry> userEntries;   // mirrors the USER combo items

    int  lastPatch  = -1;
    int  lastEngine = -1;   // tri-state so the first tick always applies the dim
    int  lastTab    = -1;   // detect user tab clicks to drive the engine
    int  lastAlgo   = -1;   // refresh the algorithm diagram when it changes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugEditor)
};
} // namespace amyplug
