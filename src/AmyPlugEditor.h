// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AmyPlugProcessor.h"
#include "gui/AmyLookAndFeel.h"
#include "gui/AmyColours.h"
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

    void addSection(const juce::String& title, juce::Colour accent = amyplug::colours::engineCyan);
    void addKnob(const juce::String& paramId, const juce::String& name);
    void addChoice(const juce::String& paramId, const juce::String& name);
    void addGraph(juce::Component& g);   // reserve a viewer at the LEFT of the current section's row

    void setCellSize(int w, int h) { cellW = w; rowH = h; }
    void setControlHeight(int h) { ctrlH = h; }   // band height (label+knob+readout)
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

    // Section boxes (full card rects incl. title), with any slack above preferredHeight
    // distributed evenly so the sections fill the panel height. Shared by paint/resized.
    std::vector<juce::Rectangle<int>> sectionBoxes() const;
    int baseBodyHeight(int sec) const { return graphForSection.count(sec) ? kGraphH : rowH; }

    juce::AudioProcessorValueTreeState& apvts;
    juce::StringArray sectionTitles;
    std::vector<juce::Colour> sectionAccents;
    std::vector<std::unique_ptr<Control>> controls;
    std::map<int, juce::Component*> graphForSection;   // section index -> optional graph
    int cellW = 88, rowH = 100, ctrlH = kCtrlH;
    // A section with a viewer gets a taller body (kGraphH) with the viewer in a
    // kGraphW-wide slot on the left and the knobs (vertically centred) to its right.
    // kCtrlH is the fixed height of a control composite (label + knob + LCD readout);
    // the body may be taller (slack), leaving even top/bottom margin around the knobs.
    static constexpr int kTitleH = 22, kGap = 8, kGraphH = 132, kGraphW = 220, kCtrlH = 104;
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

// The DX7 1 tab: a top row with the algorithm diagram (left), the Algorithm +
// Feedback card (middle), and a "DX7 / OPERATOR TUNING" watermark card (right);
// the per-operator controls fill below.
class Dx7TabComponent : public juce::Component
{
public:
    Dx7TabComponent(juce::AudioProcessorValueTreeState& apvts,
                    AlgorithmDiagram& diagram, juce::Component& controls);
    void resized() override;
    void paint(juce::Graphics&) override;
    static constexpr int kTopH = 200, kWatermarkW = 300, kSelectorW = 150;
private:
    using Apvts = juce::AudioProcessorValueTreeState;
    AlgorithmDiagram& diagram;
    juce::Component&   controlsView;
    juce::ComboBox     algoBox;
    juce::Slider       fbKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label        algoLabel, fbLabel;
    std::unique_ptr<Apvts::ComboBoxAttachment> algoAtt;
    std::unique_ptr<Apvts::SliderAttachment>   fbAtt;
    juce::Rectangle<int> selectorCard, watermarkCard;   // painted card backgrounds
};

// A tab page with a centered two-tone page title ("DX7" grey + "GLOBAL" accent) and
// a subtitle, above a content component that fills the rest. Replaces the per-tab
// viewport (the editor is fixed-size, so the sections fill without scrolling).
class TabPage : public juce::Component
{
public:
    TabPage(juce::Component& body, juce::String greyWord, juce::String accentWord,
            juce::String subtitle)
        : content(body), grey(std::move(greyWord)), accent(std::move(accentWord)),
          sub(std::move(subtitle))
    { addAndMakeVisible(content); }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop(kTitleH);
        content.setBounds(r);
    }
    void paint(juce::Graphics&) override;
    static constexpr int kTitleH = 48;
private:
    juce::Component& content;
    juce::String grey, accent, sub;
};

// The JUNO tab: a top row with the "JUNO" title (left) and the VOICE card (right),
// then the two synth columns below.
class JunoPage : public juce::Component
{
public:
    JunoPage(juce::Component& voice, juce::Component& left, juce::Component& right)
        : voiceC(voice), leftC(left), rightC(right)
    { addAndMakeVisible(voiceC); addAndMakeVisible(leftC); addAndMakeVisible(rightC); }
    void resized() override;
    void paint(juce::Graphics&) override;
    static constexpr int kTopH = 130;   // VOICE/title row ≈ one column section tall
private:
    juce::Rectangle<int> titleArea;
    juce::Component& voiceC;
    juce::Component& leftC;
    juce::Component& rightC;
};

class AmyPlugEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit AmyPlugEditor(AmyPlugProcessor&);
    ~AmyPlugEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Select a tab by index (0 Juno · 1-4 DX7 · 5 AMYboard). Used by the headless
    // snapshot tool to render a specific tab.
    void selectTab(int index);

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
    AmyLookAndFeel   lnf;   // the AMYplug visual identity (must outlive all children)

    // Global top bar.
    juce::ComboBox   patchBox, userBox;
    juce::Slider     outGainKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label      outGainLabel { {}, "OUT GAIN" };
    std::unique_ptr<Apvts::SliderAttachment> outGainAtt;
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
    // JUNO tab: two synth columns, plus a VOICE card + "JUNO" title in the top row.
    ControlPanel     junoPanelL { proc.apvts() };  // OSC A, OSC C, VCF, LFO
    ControlPanel     junoPanelR { proc.apvts() };  // OSC B, OSC D, VCF ENV, AMP ENV
    ControlPanel     voicePanel { proc.apvts() };  // VOICE (top row, right of the title)
    JunoPage         junoPage { voicePanel, junoPanelL, junoPanelR };

    AlgorithmDiagram algoDiagram;                    // operator graph (DX7 1)
    // The DX7 editor is split across 4 tabs, grouped musically:
    //   DX7 1 = algorithm + oscillators, DX7 2/3 = operator envelopes,
    //   DX7 4 = pitch EG + LFO + routing + transpose.
    ControlPanel     fmOscA { proc.apvts() }, fmOscB { proc.apvts() }, fmOscC { proc.apvts() };
    ColumnPanels     fmOscCols { { &fmOscA, &fmOscB, &fmOscC } };
    Dx7TabComponent  dx7Tab1 { proc.apvts(), algoDiagram, fmOscCols };
    // DX7 2 / DX7 3 — operator envelopes, split OP1-3 and OP4-6.
    ControlPanel     fmEnv1Panel { proc.apvts() }, fmEnv2Panel { proc.apvts() };
    EnvelopeDisplay  fmEnvGraph[6] { { proc.apvts(), 1 }, { proc.apvts(), 2 }, { proc.apvts(), 3 },
                                     { proc.apvts(), 4 }, { proc.apvts(), 5 }, { proc.apvts(), 6 } };
    TabPage          dx7Tab2 { fmEnv1Panel, "DX7", "ENVELOPES", juce::String::fromUTF8("OP 1 \xC2\xB7 OP 2 \xC2\xB7 OP 3") };
    TabPage          dx7Tab3 { fmEnv2Panel, "DX7", "ENVELOPES", juce::String::fromUTF8("OP 4 \xC2\xB7 OP 5 \xC2\xB7 OP 6") };
    // DX7 4 — pitch & global mod. The pitch EG gets its own viewer (centre = no shift).
    ControlPanel     fmModPanel { proc.apvts() };
    EnvelopeDisplay  fmPitchGraph { proc.apvts(), EnvelopeDisplay::PitchTag {} };
    TabPage          dx7Tab4 { fmModPanel, "DX7", "GLOBAL", juce::String::fromUTF8("PITCH EG \xC2\xB7 LFO \xC2\xB7 ROUTING \xC2\xB7 TRANSPOSE") };
    // FX-MASTER tab: two columns of effect cards (EQ/ECHO/BIT CRUSHER on the left,
    // CHORUS/REVERB/DISTORTION on the right) — the global FX rack + host MASTER stage.
    ControlPanel     fxPanelL  { proc.apvts() };
    ControlPanel     fxPanelR  { proc.apvts() };
    TwoColumnPanels  fxCols    { fxPanelL, fxPanelR };
    TabPage          fxPage { fxCols, "FX-", "MASTER", juce::String::fromUTF8("EQ \xC2\xB7 CHORUS \xC2\xB7 ECHO \xC2\xB7 REVERB \xC2\xB7 CRUSH \xC2\xB7 DIST") };
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
