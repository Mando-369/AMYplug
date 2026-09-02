// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AmyPlugProcessor.h"
#include "gui/AmyLookAndFeel.h"
#include "gui/AmyColours.h"
#include "gui/ScaledContent.h"
#include "state/PresetRef.h"
#include "state/PresetCatalog.h"
#include "gui/PresetsPage.h"
#include "engine/FirmwareCheck.h"
#include <functional>
#include <memory>
#include <vector>
#include <map>

namespace amyplug
{
// AlertWindow builds its own fields and hands back no pointer to them, and neither text
// indents nor a per-button colour are reachable from a LookAndFeel — so they are applied
// here, to every dialog, right before it is shown. Free function so the headless snapshot
// tool renders exactly what the plugin shows.
void styleDialogChrome(juce::AlertWindow& dialog);

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
// A power switch drawn as the universal glyph, sized for a section-header bar. Ink is
// the bar's cut-out colour; OFF is the same glyph at low alpha, so a card reads as
// "present but silent" rather than as missing a control.
class PowerButton final : public juce::Button
{
public:
    PowerButton() : juce::Button("power") { setClickingTogglesState(true); }
    void setInk(juce::Colour c) { ink = c; }
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    juce::Colour ink = juce::Colours::black;
};

// The header's preset read-out: "Bank · Name", plus " *" once anything has moved. A
// Button, not a Label with a mouseUp: Button::mouseUp gates on wasDown && wasOver, which
// is what lets clicking it a second time CLOSE the menu it opened (JUCE-UI-LnF__15 §7).
class PresetField final : public juce::Button
{
public:
    PresetField() : juce::Button("preset") { setTriggeredOnMouseDown(true); }
    void setText(juce::String bank, juce::String name, bool dirty);
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    juce::String bank, name;
    bool dirty = false;
};

// A panel of labelled controls (rotaries + choice combos) grouped into titled
// sections laid out in columns — used for the Juno engine tab and the FX rack.
class ControlPanel : public juce::Component
{
public:
    explicit ControlPanel(juce::AudioProcessorValueTreeState& s) : apvts(s) {}

    // `toggleParamId`: a bool parameter that switches the section on and off. It gets a
    // PowerButton in the header bar, and the card dims while it is off.
    void addSection(const juce::String& title, juce::Colour accent = amyplug::colours::engineCyan,
                    const juce::String& toggleParamId = {});
    void addKnob(const juce::String& paramId, const juce::String& name);
    void addChoice(const juce::String& paramId, const juce::String& name);
    void addGraph(juce::Component& g);   // reserve a viewer at the LEFT of the current section's row

    void setCellSize(int w, int h) { cellW = w; rowH = h; }
    void setControlHeight(int h) { ctrlH = h; }   // band height (label+knob+readout)
    int  preferredHeight() const;     // total height needed for all sections

    // Dim a section whose controls currently do nothing (e.g. a DX7 operator at level 0):
    // its card, title bar and controls fade, but everything stays usable so you can bring
    // it back in. Matched by title, since callers think in names ("OP 3"), not indices.
    void setSectionDimmed(const juce::String& title, bool dimmed);
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
    void setSectionDimmedAt(int sec, bool dimmed);
    juce::StringArray sectionTitles;
    std::vector<juce::Colour> sectionAccents;
    std::vector<bool> sectionDimmed;                   // parallel to sectionTitles
    std::vector<std::unique_ptr<PowerButton>>               sectionPower;      // null where no toggle
    std::vector<std::unique_ptr<Apvts::ButtonAttachment>>   sectionPowerAtt;
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
    void selectSerialPort(const juce::String& port);   // select in serialBox, adding if missing
    void startFirmwareCheck();                          // read board version + fetch latest online
    void refreshFirmwareLabel();                        // compose the result once both halves land

    AmyPlugProcessor& proc;
    bool             awaitingFirmware = false;   // board version read in flight
    bool             onlinePending    = false;   // GitHub "latest" fetch in flight
    int              firmwareWaitTicks = 0;      // timer ticks since the check was requested
    juce::String     boardFirmware;              // "" until the board read resolves
    FirmwareInfo     latestFirmware;             // resolved by the background fetch
    juce::Label      title  { {}, "AMYboard - Hardware Control" };
    juce::Label      devLabel { {}, "MIDI Out" };
    juce::Label      serialLabel { {}, "Serial" };
    juce::Label      status;
    juce::Label      firmwareLabel;
    juce::HyperlinkButton flashLink { "amyboard.com/editor",
                                      juce::URL("https://amyboard.com/editor") };  // shown on "update available"
    juce::ComboBox   deviceBox;      // USB-MIDI port (notes)
    juce::ComboBox   serialBox;      // USB-serial REPL port (patch/param edits)
    juce::TextButton refreshBtn    { "Refresh" };
    juce::TextButton detectBtn     { "Detect" };
    juce::TextButton connectBtn    { "Connect" };
    juce::TextButton disconnectBtn { "Disconnect" };
    juce::TextButton sendBtn       { "Send Patch to Board" };
    juce::TextButton firmwareBtn   { "Check for Firmware Update" };
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
    // Operators at Level 0 are wired by the algorithm but contribute nothing. Fade them
    // here too, so the diagram tells the same story as the dimmed operator cards (an INIT
    // voice is algorithm 1 with only OP 1 audible — wired ≠ sounding). Bit n = op n+1.
    void setSilentOps(int mask) { if (mask != silentMask) { silentMask = mask; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    bool isSilent(int op) const { return (silentMask & (1 << (op - 1))) != 0; }
    int algo = 1;
    int silentMask = 0;
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
                      public DialogHost,
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
    void loadPreset(const PresetRef&);
    void stepPreset(int delta);           // ‹ › over the flat catalogue, wrapping
    void showPresetMenu();                // the field's menu: FACTORY banks, USER banks, Presets tab
    void showSaveDialog();
    void importDx7();

    // --- editor size ------------------------------------------------------
    // The design surface. Everything is laid out at these numbers and scaled as a whole by
    // `content`; no layout code below knows the window can be any other size.
    static constexpr int kBaseWidth  = 1280;
    static constexpr int kBaseHeight = 830;
    void layoutContent();          // lays the editor out at the DESIGN size
    void paintContent(juce::Graphics&);
    void setUiScalePercent(int percent);
    void showSizeMenu();

    // Dialogs. An AlertWindow is a TOP-LEVEL window, not a child of the editor, so it
    // inherits neither our LookAndFeel nor our lifetime — and its text layout bakes in
    // whichever fonts the LookAndFeel had when the layout was built, so the LookAndFeel
    // has to be set before any field or button is added. One pair of helpers so a prompt
    // added later cannot forget either. See Code Repo/JUCE-UI-LnF__14 and __15.
    juce::AlertWindow* beginDialog(const juce::String& title, const juce::String& message,
                                   juce::MessageBoxIconType icon) override;
    void showDialog(std::function<void (juce::AlertWindow&, int)> onResult) override;
    void dismissDialog();

    // Tabs by name. PRESETS is first, so a fresh Factory-engine instance opens on the
    // browser; the engine tabs decide the engine, PRESETS/FX/AMYBOARD leave it alone.
    enum Tab { Presets = 0, Juno, Dx7_1, Dx7_2, Dx7_3, Dx7_4, Fx, Hardware };

    AmyPlugProcessor& proc;
    AmyLookAndFeel   lnf;   // the AMYplug visual identity (must outlive all children)
    // Everything visible is a child of `content`, which carries the UI-scale transform.
    // Declared before the controls so it is destroyed after them.
    ScaledContent    content { [this] (juce::Graphics& g) { paintContent(g); },
                               [this] { layoutContent(); } };
    juce::TextButton sizeButton { "100%" };   // size picker + live readout of the real size

    // Global top bar.
    PresetField      presetField;
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
    juce::TextButton importButton { "Import DX7..." };
    juce::TextButton toEditorButton { "To Editor" };   // factory DX7 preset -> FM tab
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::AlertWindow> dialog;   // the one live prompt, if any
    juce::ComboBox   engineBox;                    // Factory / Analog / FM
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
    PresetsPage      presetsPage { proc, *this, [this] { importDx7(); } };   // PRESETS tab

    void setEngineIndex(int idx);   // 0 Factory, 1 Analog, 2 FM


    int  lastPatch  = -1;
    juce::String lastFieldShown;                    // what the preset field currently says
    int  lastEngine = -1;   // tri-state so the first tick always applies the dim
    int  lastTab    = -1;   // detect user tab clicks to drive the engine
    int  lastAlgo   = -1;   // refresh the algorithm diagram when it changes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmyPlugEditor)
};
} // namespace amyplug
