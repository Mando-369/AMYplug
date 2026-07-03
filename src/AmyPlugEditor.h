// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AmyPlugProcessor.h"
#include <memory>
#include <vector>
#include <map>

namespace amyplug
{
// Draws a DX7 4-rate/4-level envelope (attack -> L1, decays -> L2/L3, sustain hold,
// release -> L4) as a small graph. Polls the params and repaints on change so it
// tracks live edits and preset loads. Two flavours: an operator AMP envelope (level
// 0 = silent, at the bottom) or the global PITCH envelope (level 50 = no shift, drawn
// around a centre line, with pitch-EG segment timing).
class EnvelopeDisplay : public juce::Component, private juce::Timer
{
public:
    EnvelopeDisplay(juce::AudioProcessorValueTreeState& s, int op);   // operator amp EG
    struct PitchTag {};
    EnvelopeDisplay(juce::AudioProcessorValueTreeState& s, PitchTag); // global pitch EG
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    std::atomic<float>* rateP[4] {};
    std::atomic<float>* levelP[4] {};
    float lastR[4] { -1, -1, -1, -1 }, lastL[4] { -1, -1, -1, -1 };
    bool pitch = false;   // pitch EG (centre at 50) vs operator amp EG (floor at 0)
};
// A panel of labelled controls (rotaries + choice combos) grouped into titled
// sections laid out in columns — used for the Juno engine tab and the FX rack.
class ControlPanel : public juce::Component
{
public:
    explicit ControlPanel(juce::AudioProcessorValueTreeState& s) : apvts(s) {}

    void addSection(const juce::String& title);
    void addKnob(const juce::String& paramId, const juce::String& name);
    void addChoice(const juce::String& paramId, const juce::String& name);
    void addGraph(juce::Component& g);   // reserve a viewer at the LEFT of the current section's row

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
    std::map<int, juce::Component*> graphForSection;   // section index -> optional graph
    int cellW = 88, rowH = 100;
    // A section with a viewer gets a taller body (kGraphH) with the viewer in a
    // kGraphW-wide slot on the left and the knobs (vertically centred) to its right.
    static constexpr int kTitleH = 20, kGap = 8, kGraphH = 130, kGraphW = 220;
};

// The AMYboard (Hardware) tab: pick the board's MIDI-out, connect, toggle
// Software/Hardware mode, and push the current patch to the board as SysEx.
class HardwarePanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit HardwarePanel(AmyPlugProcessor& p);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void refreshDevices();

    AmyPlugProcessor& proc;
    juce::Label      title  { {}, "AMYboard - Hardware Control" };
    juce::Label      devLabel { {}, "MIDI Out" };
    juce::Label      status;
    juce::ComboBox   deviceBox;
    juce::TextButton refreshBtn    { "Refresh" };
    juce::TextButton connectBtn    { "Connect" };
    juce::TextButton disconnectBtn { "Disconnect" };
    juce::TextButton sendBtn       { "Send Patch to Board" };
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

// Lays two child components side by side (equal halves) — used to pair the FM
// operator panels (OP1|OP2 / OP3|OP4 / OP5|OP6).
class TwoColumnPanels : public juce::Component
{
public:
    TwoColumnPanels(juce::Component& l, juce::Component& r) : left(l), right(r)
    { addAndMakeVisible(left); addAndMakeVisible(right); }
    void resized() override
    {
        auto r = getLocalBounds();
        left.setBounds(r.removeFromLeft(r.getWidth() / 2));
        right.setBounds(r);
    }
private:
    juce::Component& left;
    juce::Component& right;
};

// Lays out N child components side by side in equal columns.
class ColumnPanels : public juce::Component
{
public:
    explicit ColumnPanels(std::vector<juce::Component*> cols) : columns(std::move(cols))
    { for (auto* c : columns) addAndMakeVisible(*c); }
    void resized() override
    {
        auto r = getLocalBounds();
        const int n = (int) columns.size();
        if (n == 0) return;
        const int w = r.getWidth() / n;
        for (int i = 0; i < n; ++i)
            columns[i]->setBounds(i < n - 1 ? r.removeFromLeft(w) : r);
    }
private:
    std::vector<juce::Component*> columns;
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
    // Always-on readout of what's actually making sound (Software+engine / silent /
    // Hardware+device). The "take over" button appears only when another instance owns
    // the single global AMY engine (e.g. a duplicated track). See EngineOwnership.h.
    juce::Label      engineStatusLabel;
    juce::TextButton takeoverButton { "USE ENGINE HERE" };
    juce::String     lastStatusText;
    bool             lastBusy = false;
    juce::TextButton prevButton   { "<" };
    juce::TextButton nextButton   { ">" };
    juce::TextButton saveButton   { "Save..." };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton importButton { "Import DX7..." };
    juce::TextButton toEditorButton { "To Editor" };   // factory DX7 preset -> FM tab
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ComboBox   engineBox;                    // Factory / Analog / FM
    juce::Label      browserLabel { {}, "PATCH" };
    juce::Label      userLabel    { {}, "USER" };
    juce::Label      engineLabel  { {}, "ENGINE" };
    std::unique_ptr<Apvts::ComboBoxAttachment> engineAtt;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::Viewport   junoViewport;                 // scrolls the Juno columns if tall
    ControlPanel     junoPanelL { proc.apvts() };  // OSC A, LFO, VCF ENV (left)
    ControlPanel     junoPanelR { proc.apvts() };  // OSC B, VCF, AMP ENV (right)
    TwoColumnPanels  junoCols { junoPanelL, junoPanelR };
    AlgorithmDiagram algoDiagram;                    // operator graph (DX7 1)
    // The DX7 editor is split across 3 tabs for readability, grouped musically:
    //   DX7 1 = algorithm + oscillators (per-op ratio/level/fixed),
    //   DX7 2 = operator envelopes (per-op R1-4 / L1-4),
    //   DX7 3 = pitch & global modulation (pitch EG; LFO/transpose to come).
    // DX7 1 — algorithm + oscillators, in 3 columns of 2 ops (short + tight).
    ControlPanel     fmOscA { proc.apvts() }, fmOscB { proc.apvts() }, fmOscC { proc.apvts() };
    ColumnPanels     fmOscCols { { &fmOscA, &fmOscB, &fmOscC } };
    juce::Viewport   fmOscViewport;
    Dx7TabComponent  dx7Tab1 { proc.apvts(), algoDiagram, fmOscViewport };
    // DX7 2 / DX7 3 — operator envelopes, split OP1-3 and OP4-6. Each operator is one
    // row: its viewer on the left, R1-4/L1-4 knobs to the right, under a single title.
    ControlPanel     fmEnv1Panel { proc.apvts() }, fmEnv2Panel { proc.apvts() };
    EnvelopeDisplay  fmEnvGraph[6] { { proc.apvts(), 1 }, { proc.apvts(), 2 }, { proc.apvts(), 3 },
                                     { proc.apvts(), 4 }, { proc.apvts(), 5 }, { proc.apvts(), 6 } };
    juce::Viewport   fmEnv1Viewport, fmEnv2Viewport;
    // DX7 4 — pitch & global mod. The pitch EG gets its own viewer (centre = no shift).
    ControlPanel     fmModPanel { proc.apvts() };
    EnvelopeDisplay  fmPitchGraph { proc.apvts(), EnvelopeDisplay::PitchTag {} };
    juce::Viewport   fmModViewport;
    ControlPanel     fxPanel   { proc.apvts() };   // global FX rack (right column)
    HardwarePanel    hwPanel   { proc };            // AMYboard tab

    void setEngineIndex(int idx);   // 0 Factory, 1 Analog, 2 FM

    std::vector<PatchLibrary::Entry> userEntries;   // mirrors the USER combo items

    int  lastPatch  = -1;
    int  lastEngine = -1;   // tri-state so the first tick always applies the dim
    int  lastTab    = -1;   // detect user tab clicks to drive the engine
    int  lastAlgo   = -1;   // refresh the algorithm diagram when it changes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugEditor)
};
} // namespace amyplug
