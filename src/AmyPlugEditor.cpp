// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugEditor.h"
#include "engine/HardwareBackend.h"
#include "state/Parameters.h"
#include "state/FmAlgorithms.h"
#include "state/Dx7Envelope.h"
#include "BuiltinPatchNames.h"
#include <array>

namespace amyplug
{
// ===========================================================================
// EnvelopeDisplay
// ===========================================================================
EnvelopeDisplay::EnvelopeDisplay(juce::AudioProcessorValueTreeState& s, int op)
{
    for (int e = 0; e < 4; ++e)
    {
        rateP[e]  = s.getRawParameterValue(params::id::fmOp(op, ("r" + juce::String(e + 1)).toRawUTF8()));
        levelP[e] = s.getRawParameterValue(params::id::fmOp(op, ("l" + juce::String(e + 1)).toRawUTF8()));
    }
    startTimerHz(12);
}

void EnvelopeDisplay::timerCallback()
{
    bool changed = false;
    for (int e = 0; e < 4; ++e)
    {
        const float r = rateP[e]  ? rateP[e]->load()  : 99.0f;
        const float l = levelP[e] ? levelP[e]->load() : 0.0f;
        if (r != lastR[e] || l != lastL[e]) { lastR[e] = r; lastL[e] = l; changed = true; }
    }
    if (changed) repaint();
}

void EnvelopeDisplay::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(3.0f);
    g.setColour(juce::Colour(0xff101418));
    g.fillRoundedRectangle(b, 3.0f);
    g.setColour(juce::Colour(0xff2a3138));
    g.drawRoundedRectangle(b, 3.0f, 1.0f);

    const auto plot = b.reduced(6.0f);
    float L[4], R[4];
    for (int e = 0; e < 4; ++e)
    {
        L[e] = levelP[e] ? levelP[e]->load() : 0.0f;
        R[e] = rateP[e]  ? rateP[e]->load()  : 99.0f;
    }
    // Segment durations (sqrt-compressed so long tails stay visible but bounded), plus
    // a fixed sustain hold. Shape: L4 -> L1 -> L2 -> L3 -> (hold) -> L4.
    using namespace amyplug::dx7env;
    const double t0 = std::sqrt(segSeconds(R[0], L[3], L[0], false));
    const double t1 = std::sqrt(segSeconds(R[1], L[0], L[1], false));
    const double t2 = std::sqrt(segSeconds(R[2], L[1], L[2], false));
    const double t3 = std::sqrt(segSeconds(R[3], L[2], L[3], true));
    const double sus = 0.4 * (t0 + t1 + t2 + t3 + 0.001);
    const double total = t0 + t1 + t2 + t3 + sus + 1e-6;

    auto X = [&] (double acc) { return plot.getX() + (float) (acc / total) * plot.getWidth(); };
    auto Y = [&] (float lvl)  { return plot.getBottom() - (lvl / 99.0f) * plot.getHeight(); };

    juce::Path p;
    double acc = 0.0;
    p.startNewSubPath(X(acc), Y(L[3]));               // start at release floor L4
    acc += t0; p.lineTo(X(acc), Y(L[0]));             // attack -> L1
    acc += t1; p.lineTo(X(acc), Y(L[1]));             // decay  -> L2
    acc += t2; p.lineTo(X(acc), Y(L[2]));             // decay  -> L3 (sustain)
    acc += sus; p.lineTo(X(acc), Y(L[2]));            // sustain hold
    acc += t3; p.lineTo(X(acc), Y(L[3]));             // release -> L4
    g.setColour(juce::Colour(0xff35a0d0));
    g.strokePath(p, juce::PathStrokeType(1.8f));
}
namespace
{
const juce::Colour kBg     { 0xff1e2327 };
const juce::Colour kPanel  { 0xff262d33 };
const juce::Colour kTitle  { 0xff0e1113 };

const char* bankOf(int p)
{
    if (p <= 127) return "Juno";
    if (p <= 255) return "DX7";
    if (p == 256) return "Piano";
    return "AMYboard";
}
} // namespace

// ===========================================================================
// ControlPanel
// ===========================================================================
void ControlPanel::addSection(const juce::String& title) { sectionTitles.add(title); }

void ControlPanel::addKnob(const juce::String& paramId, const juce::String& name)
{
    auto c = std::make_unique<Control>();
    c->section = juce::jmax(0, sectionTitles.size() - 1);
    c->knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                             juce::Slider::TextBoxBelow);
    c->knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 15);
    addAndMakeVisible(*c->knob);
    c->label.setText(name, juce::dontSendNotification);
    c->label.setJustificationType(juce::Justification::centred);
    c->label.setFont(juce::FontOptions(11.0f));
    c->label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(c->label);
    c->ka = std::make_unique<Apvts::SliderAttachment>(apvts, paramId, *c->knob);
    controls.push_back(std::move(c));
}

void ControlPanel::addChoice(const juce::String& paramId, const juce::String& name)
{
    auto c = std::make_unique<Control>();
    c->section = juce::jmax(0, sectionTitles.size() - 1);
    c->combo = std::make_unique<juce::ComboBox>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId)))
        c->combo->addItemList(p->choices, 1);
    addAndMakeVisible(*c->combo);
    c->label.setText(name, juce::dontSendNotification);
    c->label.setJustificationType(juce::Justification::centred);
    c->label.setFont(juce::FontOptions(11.0f));
    c->label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(c->label);
    c->ca = std::make_unique<Apvts::ComboBoxAttachment>(apvts, paramId, *c->combo);
    controls.push_back(std::move(c));
}

void ControlPanel::addGraph(juce::Component& gr)
{
    graphForSection[juce::jmax(0, sectionTitles.size() - 1)] = &gr;
    addAndMakeVisible(gr);
}

int ControlPanel::preferredHeight() const
{
    int h = 8;
    for (int sec = 0; sec < sectionTitles.size(); ++sec)
        h += kTitleH + (graphForSection.count(sec) ? kGraphH : 0) + rowH + kGap;
    return h;
}

void ControlPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().reduced(4);
    for (int sec = 0; sec < sectionTitles.size(); ++sec)
    {
        auto box = r.removeFromTop(kTitleH + rowH);
        g.setColour(kPanel);   g.fillRoundedRectangle(box.toFloat(), 5.0f);
        auto tb = box.removeFromTop(kTitleH);
        g.setColour(kTitle);   g.fillRoundedRectangle(tb.toFloat(), 5.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(sectionTitles[sec], tb, juce::Justification::centred);
        r.removeFromTop(kGap);
    }
}

void ControlPanel::resized()
{
    auto r = getLocalBounds().reduced(4);
    for (int sec = 0; sec < sectionTitles.size(); ++sec)
    {
        const bool hasGraph = graphForSection.count(sec) > 0;
        auto box = r.removeFromTop(kTitleH + (hasGraph ? kGraphH : 0) + rowH);
        box.removeFromTop(kTitleH);
        if (hasGraph)
            graphForSection[sec]->setBounds(box.removeFromTop(kGraphH)
                                               .withSizeKeepingCentre(200, kGraphH - 6));

        int count = 0;
        for (auto& c : controls) if (c->section == sec) ++count;
        if (count > 0)
        {
            // Spread the section's controls evenly across its full width, each
            // centred in its own equal slot (so a 3-knob row isn't bunched left).
            const int slotW = box.getWidth() / count;
            int i = 0;
            for (auto& c : controls)
            {
                if (c->section != sec) continue;
                juce::Rectangle<int> slot(box.getX() + i * slotW, box.getY() + 4, slotW, rowH - 8);
                // Combos fill their slot (menus like the FM algorithm need the width);
                // knobs stay capped at cellW so they don't balloon when alone.
                const int cw = c->combo ? (slotW - 4) : juce::jmin(cellW, slotW - 4);
                auto cell = slot.withSizeKeepingCentre(cw, rowH - 8);
                c->label.setBounds(cell.removeFromTop(16));
                if (c->combo) c->combo->setBounds(cell.removeFromTop(28).reduced(0, 1));
                else if (c->knob) c->knob->setBounds(cell.reduced(2, 0));
                ++i;
            }
        }
        r.removeFromTop(kGap);
    }
}

void PlaceholderPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(15.0f));
    g.drawFittedText(text, getLocalBounds().reduced(20), juce::Justification::centred, 3);
}

// ===========================================================================
// HardwarePanel — the AMYboard tab
// ===========================================================================
HardwarePanel::HardwarePanel(AmyPlugProcessor& p) : proc(p)
{
    title.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title);
    devLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(devLabel);
    addAndMakeVisible(status);
    addAndMakeVisible(deviceBox);
    for (auto* b : { &refreshBtn, &connectBtn, &disconnectBtn, &sendBtn }) addAndMakeVisible(*b);

    auto setMode = [this] (float hardware)   // 0 = Software, 1 = Hardware
    { if (auto* p = proc.apvts().getParameter(params::id::mode)) p->setValueNotifyingHost(hardware); };

    refreshBtn.onClick    = [this] { refreshDevices(); };
    // Connecting a board switches the engine to Hardware (plugin goes silent, the
    // board makes the sound) and pushes the current patch; disconnecting reverts.
    connectBtn.onClick    = [this, setMode] {
        // Switching to Hardware already re-sends the patch (rebuildEngineFromModel),
        // so don't also call sendPatchToHardware — that doubled the SysEx burst.
        if (auto* hw = proc.hardwareBackend())
            if (hw->openOutput(deviceBox.getText())) setMode(1.0f);
    };
    disconnectBtn.onClick = [this, setMode] {
        if (auto* hw = proc.hardwareBackend()) hw->closeOutput();
        setMode(0.0f);
    };
    sendBtn.onClick       = [this] { proc.sendPatchToHardware(); };

    refreshDevices();
    startTimerHz(3);   // status + button enablement
}

void HardwarePanel::refreshDevices()
{
    const auto keep = deviceBox.getText();
    deviceBox.clear(juce::dontSendNotification);
    if (auto* hw = proc.hardwareBackend())
    {
        int id = 1;
        for (const auto& n : hw->availableOutputs()) deviceBox.addItem(n, id++);
    }
    // Re-select the previously chosen port if it's still present, else the first.
    for (int i = 0; i < deviceBox.getNumItems(); ++i)
        if (deviceBox.getItemText(i) == keep) { deviceBox.setSelectedId(deviceBox.getItemId(i), juce::dontSendNotification); return; }
    if (deviceBox.getNumItems() > 0) deviceBox.setSelectedId(1, juce::dontSendNotification);
}

void HardwarePanel::timerCallback()
{
    auto* hw = proc.hardwareBackend();
    const bool conn = hw && hw->isConnected();
    const bool hwMode = proc.currentMode() == IAmyBackend::Kind::Hardware;
    juce::String s;
    if (hwMode) s = conn ? ("HARDWARE - board is sounding (" + hw->connectedName() + "); plugin is silent")
                         : "HARDWARE - but no board connected (silent!)";
    else        s = conn ? ("SOFTWARE - plugin is sounding; board connected but idle")
                         : "SOFTWARE - plugin is sounding";
    status.setText("Mode: " + s, juce::dontSendNotification);
    status.setColour(juce::Label::textColourId,
                     hwMode ? (conn ? juce::Colours::lightgreen : juce::Colours::red) : juce::Colours::lightgrey);
    connectBtn.setEnabled(! conn && deviceBox.getNumItems() > 0);
    disconnectBtn.setEnabled(conn);
    sendBtn.setEnabled(conn);
}

void HardwarePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour { 0xff20262b });
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText("Connect the AMYboard's MIDI port to play the sound ON THE BOARD - the plugin goes\n"
                     "silent and pushes the current patch. Disconnect to return to the plugin's own sound.",
                     getLocalBounds().removeFromBottom(56).reduced(20, 8), juce::Justification::topLeft, 3);
}

void HardwarePanel::resized()
{
    auto r = getLocalBounds().reduced(20);
    title.setBounds(r.removeFromTop(30));
    r.removeFromTop(18);
    { auto line = r.removeFromTop(28); devLabel.setBounds(line.removeFromLeft(70));
      refreshBtn.setBounds(line.removeFromRight(90)); line.removeFromRight(8);
      deviceBox.setBounds(line.removeFromLeft(280)); r.removeFromTop(12); }
    { auto line = r.removeFromTop(28); line.removeFromLeft(70);
      connectBtn.setBounds(line.removeFromLeft(120)); line.removeFromLeft(12);
      disconnectBtn.setBounds(line.removeFromLeft(120)); r.removeFromTop(12); }
    { auto line = r.removeFromTop(28); line.removeFromLeft(70);
      sendBtn.setBounds(line.removeFromLeft(200)); r.removeFromTop(18); }
    status.setBounds(r.removeFromTop(26));
}

// ===========================================================================
// AlgorithmDiagram
// ===========================================================================
void AlgorithmDiagram::paint(juce::Graphics& g)
{
    const juce::Colour kAccent { 0xff5ec8d8 };       // carriers / output
    const juce::Colour kMod    { 0xff39424a };       // modulator fill
    g.setColour(juce::Colour { 0xff20262b });
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    const auto topo = fm::algorithmTopology(algo);
    if (topo.carriers.isEmpty()) return;

    // modulates[op] = the operators that `op` modulates (children point downward).
    std::array<juce::Array<int>, 7> modulates;
    for (int op = 1; op <= 6; ++op)
        for (int m : topo.modulators[op]) modulates[(size_t) m].add(op);

    // depth from the output (carriers = 0); higher = further up the stack.
    std::array<int, 7> depth; depth.fill(-1);
    for (int op = 1; op <= 6; ++op) if (topo.carriers.contains(op)) depth[(size_t) op] = 0;
    for (int iter = 0; iter < 7; ++iter)
        for (int op = 1; op <= 6; ++op)
            if (depth[(size_t) op] < 0)
            {
                int d = -1; bool ready = true;
                for (int t : modulates[(size_t) op])
                {
                    if (depth[(size_t) t] < 0) { ready = false; break; }
                    d = juce::jmax(d, depth[(size_t) t]);
                }
                if (ready) depth[(size_t) op] = modulates[(size_t) op].isEmpty() ? 0 : d + 1;
            }
    for (int op = 1; op <= 6; ++op) if (depth[(size_t) op] < 0) depth[(size_t) op] = 0;

    // x: carriers spread left->right; each modulator centres over what it feeds.
    std::array<float, 7> xpos; xpos.fill(0.0f);
    for (int i = 0; i < topo.carriers.size(); ++i) xpos[(size_t) topo.carriers[i]] = (float) i;
    int maxd = 0; for (int op = 1; op <= 6; ++op) maxd = juce::jmax(maxd, depth[(size_t) op]);
    for (int d = 1; d <= maxd; ++d)
        for (int op = 1; op <= 6; ++op)
            if (depth[(size_t) op] == d)
            {
                float s = 0; int n = 0;
                for (int t : modulates[(size_t) op]) { s += xpos[(size_t) t]; ++n; }
                xpos[(size_t) op] = n ? s / (float) n : 0.0f;
            }

    auto area = getLocalBounds().reduced(12);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("ALGORITHM " + juce::String(algo), area.removeFromTop(14), juce::Justification::centredLeft);
    auto outBar = area.removeFromBottom(16);

    float minx = 1e9f, maxx = -1e9f;
    for (int op = 1; op <= 6; ++op) { minx = juce::jmin(minx, xpos[(size_t) op]); maxx = juce::jmax(maxx, xpos[(size_t) op]); }
    const int boxW = 40, boxH = 26;
    const int rowGap = (area.getHeight() - boxH) / juce::jmax(1, maxd);
    // Fixed horizontal spacing between operator columns, with the whole graph
    // centred in the panel (rather than stretched edge-to-edge).
    const float colSpacing = boxW + 30.0f;
    const float usedW   = (maxx - minx) * colSpacing + boxW;
    const float originX = area.getX() + (area.getWidth() - usedW) * 0.5f - minx * colSpacing;
    auto boxOf = [&] (int op)
    {
        const int x = juce::roundToInt(originX + xpos[(size_t) op] * colSpacing);
        const int y = area.getBottom() - boxH - depth[(size_t) op] * rowGap;
        return juce::Rectangle<int> { x, y, boxW, boxH };
    };

    // Connections (modulator bottom -> target top).
    g.setColour(juce::Colour { 0xff6b7780 });
    for (int op = 1; op <= 6; ++op)
        for (int t : modulates[(size_t) op])
        {
            auto a = boxOf(op).toFloat(); auto b = boxOf(t).toFloat();
            g.drawLine(a.getCentreX(), a.getBottom(), b.getCentreX(), b.getY(), 1.6f);
        }
    // Carrier -> output bar (the rail spans only the carrier cluster).
    g.setColour(kAccent);
    int barL = area.getRight(), barR = area.getX();
    for (int c : topo.carriers)
    {
        auto bx = boxOf(c).toFloat();
        g.drawLine(bx.getCentreX(), bx.getBottom(), bx.getCentreX(), (float) outBar.getCentreY(), 1.6f);
        barL = juce::jmin(barL, (int) bx.getCentreX()); barR = juce::jmax(barR, (int) bx.getCentreX());
    }
    g.drawLine((float) barL, (float) outBar.getCentreY(), (float) barR, (float) outBar.getCentreY(), 1.6f);
    g.setFont(juce::FontOptions(9.0f));
    g.drawText("output", juce::Rectangle<int> { barR + 4, outBar.getY(), 48, outBar.getHeight() },
               juce::Justification::centredLeft);

    // Operator boxes.
    for (int op = 1; op <= 6; ++op)
    {
        auto bx = boxOf(op);
        const bool carrier = topo.carriers.contains(op);
        g.setColour(carrier ? kAccent.withAlpha(0.22f) : kMod);
        g.fillRoundedRectangle(bx.toFloat(), 4.0f);
        g.setColour(carrier ? kAccent : juce::Colour { 0xff7d8993 });
        g.drawRoundedRectangle(bx.toFloat(), 4.0f, carrier ? 1.8f : 1.2f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(juce::String(op), bx, juce::Justification::centred);

        if (topo.feedback[(size_t) op])   // mark the feedback operator with an "FB" tag
        {
            juce::Rectangle<int> tag { bx.getRight() - 15, bx.getY() - 9, 20, 13 };
            g.setColour(juce::Colours::orange);
            g.fillRoundedRectangle(tag.toFloat(), 3.0f);
            g.setColour(juce::Colours::black);
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText("FB", tag, juce::Justification::centred);
        }
    }
}

// ===========================================================================
// Dx7TabComponent
// ===========================================================================
Dx7TabComponent::Dx7TabComponent(Apvts& apvts, AlgorithmDiagram& d, juce::Component& controls)
    : diagram(d), controlsView(controls)
{
    addAndMakeVisible(diagram);
    addAndMakeVisible(controlsView);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(params::id::fmAlgorithm)))
        algoBox.addItemList(p->choices, 1);
    algoAtt = std::make_unique<Apvts::ComboBoxAttachment>(apvts, params::id::fmAlgorithm, algoBox);
    addAndMakeVisible(algoBox);

    fbKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 15);
    fbAtt = std::make_unique<Apvts::SliderAttachment>(apvts, params::id::fmFeedback, fbKnob);
    addAndMakeVisible(fbKnob);

    for (auto* l : { &algoLabel, &fbLabel })
    {
        l->setJustificationType(juce::Justification::centred);
        l->setFont(juce::FontOptions(11.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(*l);
    }
    algoLabel.setText("ALGORITHM", juce::dontSendNotification);
    fbLabel.setText("FEEDBACK", juce::dontSendNotification);
}

void Dx7TabComponent::resized()
{
    auto r = getLocalBounds();
    auto top   = r.removeFromTop(kTopH);
    auto right = top.removeFromRight(150).reduced(10, 8);   // selector + feedback column
    diagram.setBounds(top);

    algoLabel.setBounds(right.removeFromTop(16));
    algoBox.setBounds(right.removeFromTop(26));
    right.removeFromTop(12);
    fbLabel.setBounds(right.removeFromTop(16));
    fbKnob.setBounds(right.removeFromTop(juce::jmin(80, right.getHeight())));

    controlsView.setBounds(r);
}

// ===========================================================================
// AmyPlugEditor
// ===========================================================================
AmyPlugEditor::AmyPlugEditor(AmyPlugProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p)
{
    auto& s = proc.apvts();

    // --- top bar ----------------------------------------------------------
    for (auto* l : { &browserLabel, &userLabel })
    {
        l->setFont(juce::FontOptions(11.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(*l);
    }
    buildPatchBox();
    patchBox.onChange = [this]
    {
        const int id = patchBox.getSelectedId();
        if (id > 0)
        {
            setEngineIndex(0);                    // selecting a factory preset
            selectPatch(id - 1);
        }
    };
    addAndMakeVisible(patchBox);
    prevButton.onClick = [this] { stepPatch(-1); };
    nextButton.onClick = [this] { stepPatch(+1); };
    addAndMakeVisible(prevButton); addAndMakeVisible(nextButton);

    userBox.setTextWhenNothingSelected("User patches");
    userBox.onChange = [this]
    {
        const int id = userBox.getSelectedId();
        if (id > 0 && id <= (int) userEntries.size())
        {
            const auto& e = userEntries[(size_t) (id - 1)];
            proc.loadUserPatch(e.group, e.name);
        }
    };
    addAndMakeVisible(userBox); refreshUserBox();
    saveButton.onClick   = [this] { showSaveDialog(); };
    deleteButton.onClick = [this]
    {
        const int id = userBox.getSelectedId();
        if (id > 0 && id <= (int) userEntries.size())
        {
            const auto& e = userEntries[(size_t) (id - 1)];
            proc.patchLibrary().remove(e.group, e.name);
            refreshUserBox();
        }
    };
    addAndMakeVisible(saveButton); addAndMakeVisible(deleteButton);
    importButton.onClick = [this] { importDx7(); };
    importButton.setTooltip("Import a DX7 .syx cartridge as named FM user patches");
    addAndMakeVisible(importButton);

    // "To Editor": decode the selected factory DX7 preset into the editable FM tab.
    // Enabled only for DX7 presets (Juno analog patches need the wider 4-osc editor).
    toEditorButton.onClick = [this]
    {
        proc.loadFactoryPatchIntoEditor(lastPatch);   // sets engine -> timer switches the tab
    };
    toEditorButton.setTooltip("Load this factory preset's settings into the editable Juno / DX7 tab");
    addAndMakeVisible(toEditorButton);

    if (auto* ep = dynamic_cast<juce::AudioParameterChoice*>(s.getParameter(params::id::engine)))
        engineBox.addItemList(ep->choices, 1);
    engineBox.setTooltip("Which engine drives synth 1: Factory preset, Analog (Juno tab), or FM (DX7 tab)");
    engineAtt = std::make_unique<Apvts::ComboBoxAttachment>(s, params::id::engine, engineBox);
    addAndMakeVisible(engineBox);
    engineLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    engineLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(engineLabel);

    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    panicButton.onClick = [this] { proc.requestPanic(); };
    addAndMakeVisible(panicButton);

    // Always-on engine status readout (text + colour set each tick in timerCallback).
    engineStatusLabel.setJustificationType(juce::Justification::centredRight);
    engineStatusLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addAndMakeVisible(engineStatusLabel);
    // The take-over button appears only when another instance holds the engine.
    takeoverButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffc06000));
    takeoverButton.onClick = [this] { proc.takeOverSoftwareEngine(); };
    addChildComponent(takeoverButton);

    // --- Juno tab: two columns. OSC A|OSC B / VCF|VCF ENV / LFO|AMP ENV --------
    junoPanelL.addSection("OSC A");
    junoPanelL.addChoice(params::id::oscAWave, "Wave");
    junoPanelL.addKnob(params::id::oscAFreq, "Freq");
    junoPanelL.addKnob(params::id::oscACoarse, "Coarse");
    junoPanelL.addKnob(params::id::oscAFine, "Fine");
    junoPanelL.addKnob(params::id::oscADuty, "Duty");
    junoPanelL.addKnob(params::id::oscALevel, "Level");
    junoPanelL.addSection("OSC C");
    junoPanelL.addChoice(params::id::oscCWave, "Wave");
    junoPanelL.addKnob(params::id::oscCFreq, "Freq");
    junoPanelL.addKnob(params::id::oscCCoarse, "Coarse");
    junoPanelL.addKnob(params::id::oscCFine, "Fine");
    junoPanelL.addKnob(params::id::oscCDuty, "Duty");
    junoPanelL.addKnob(params::id::oscCLevel, "Level");
    junoPanelL.addSection("VCF");
    junoPanelL.addKnob(params::id::filterCutoff, "Freq");
    junoPanelL.addKnob(params::id::filterReso, "Reso");
    junoPanelL.addKnob(params::id::vcfKbd, "Kbd");
    junoPanelL.addKnob(params::id::vcfEnv, "Env");
    junoPanelL.addChoice(params::id::vcfType, "Type");
    junoPanelL.addSection("LFO");
    junoPanelL.addChoice(params::id::lfoWave, "Wave");
    junoPanelL.addKnob(params::id::lfoFreq, "Freq");
    junoPanelL.addKnob(params::id::lfoToPitch, "Pitch");
    junoPanelL.addKnob(params::id::lfoToPwm, "PWM");
    junoPanelL.addKnob(params::id::lfoToFilter, "Filter");

    junoPanelR.addSection("OSC B");
    junoPanelR.addChoice(params::id::oscBWave, "Wave");
    junoPanelR.addKnob(params::id::oscBFreq, "Freq");
    junoPanelR.addKnob(params::id::oscBCoarse, "Coarse");
    junoPanelR.addKnob(params::id::oscBFine, "Fine");
    junoPanelR.addKnob(params::id::oscBDuty, "Duty");
    junoPanelR.addKnob(params::id::oscBLevel, "Level");
    junoPanelR.addSection("OSC D");
    junoPanelR.addChoice(params::id::oscDWave, "Wave");
    junoPanelR.addKnob(params::id::oscDFreq, "Freq");
    junoPanelR.addKnob(params::id::oscDCoarse, "Coarse");
    junoPanelR.addKnob(params::id::oscDFine, "Fine");
    junoPanelR.addKnob(params::id::oscDDuty, "Duty");
    junoPanelR.addKnob(params::id::oscDLevel, "Level");
    junoPanelR.addSection("VCF ENV");
    junoPanelR.addKnob(params::id::vcfAttack, "A");
    junoPanelR.addKnob(params::id::vcfDecay, "D");
    junoPanelR.addKnob(params::id::vcfSustain, "S");
    junoPanelR.addKnob(params::id::vcfRelease, "R");
    junoPanelR.addSection("AMP ENV");
    junoPanelR.addKnob(params::id::ampAttack, "A");
    junoPanelR.addKnob(params::id::ampDecay, "D");
    junoPanelR.addKnob(params::id::ampSustain, "S");
    junoPanelR.addKnob(params::id::ampRelease, "R");
    // Voicing lives with the synth, not the FX rack. Mode + Glide are global (they
    // also affect DX7); Unison + Detune are analog-only.
    junoPanelR.addSection("VOICE");
    junoPanelR.addChoice(params::id::voiceMode, "Mode");   // Poly / Mono / Legato
    junoPanelR.addKnob(params::id::glide, "Glide");        // portamento (ms)
    junoPanelR.addKnob(params::id::unisonVoices, "Unison"); // stacked detuned copies
    junoPanelR.addKnob(params::id::unisonDetune, "Detune"); // unison spread, cents

    junoPanelL.setCellSize(86, 94);
    junoPanelR.setCellSize(86, 94);

    // --- DX7 (FM) operator controls, two columns: OP1|OP2 / OP3|OP4 / OP5|OP6.
    //     (Algorithm + feedback live in the tab's top row alongside the diagram.) -
    // DX7 1 — per-operator frequency + level, 3 columns of 2 ops (OP1/4, OP2/5, OP3/6).
    auto addOsc = [] (ControlPanel& p, int op)
    {
        p.addSection("OP " + juce::String(op));
        p.addKnob(params::id::fmOp(op, "ratio"),   "Ratio");
        p.addKnob(params::id::fmOp(op, "level"),   "Level");
        p.addChoice(params::id::fmOp(op, "fixed"), "Mode");   // Ratio / Fixed
        p.addKnob(params::id::fmOp(op, "fixedhz"), "Fix Hz");
    };
    addOsc(fmOscA, 1); addOsc(fmOscA, 4);
    addOsc(fmOscB, 2); addOsc(fmOscB, 5);
    addOsc(fmOscC, 3); addOsc(fmOscC, 6);
    for (auto* p : { &fmOscA, &fmOscB, &fmOscC }) p->setCellSize(96, 92);
    // DX7 2 / DX7 3 — per-operator DX7 4-rate / 4-level envelope, OP1-3 and OP4-6. The
    // viewers sit in a labelled row at the top (EnvGraphRow); here we add only knobs.
    auto addEnv = [] (ControlPanel& p, int op)
    {
        p.addSection("OP " + juce::String(op));
        for (int e = 1; e <= 4; ++e)
            p.addKnob(params::id::fmOp(op, ("r" + juce::String(e)).toRawUTF8()), "R" + juce::String(e));
        for (int e = 1; e <= 4; ++e)
            p.addKnob(params::id::fmOp(op, ("l" + juce::String(e)).toRawUTF8()), "L" + juce::String(e));
    };
    addEnv(fmEnv1Panel, 1); addEnv(fmEnv1Panel, 2); addEnv(fmEnv1Panel, 3);
    addEnv(fmEnv2Panel, 4); addEnv(fmEnv2Panel, 5); addEnv(fmEnv2Panel, 6);
    fmEnv1Panel.setCellSize(96, 96); fmEnv2Panel.setCellSize(96, 96);
    // DX7 4 — pitch & global mod: pitch EG, the LFO, and the per-op tremolo routing.
    fmModPanel.addSection("PITCH EG");
    for (int e = 1; e <= 4; ++e) fmModPanel.addKnob(params::id::fmPitchEg('r', e), "R" + juce::String(e));
    for (int e = 1; e <= 4; ++e) fmModPanel.addKnob(params::id::fmPitchEg('l', e), "L" + juce::String(e));
    // LFO: speed + waveform, vibrato (pitch depth + sensitivity), tremolo (amp depth).
    fmModPanel.addSection("LFO");
    fmModPanel.addKnob(params::id::fmLfoSpeed, "Speed");
    fmModPanel.addChoice(params::id::fmLfoWave, "Wave");
    fmModPanel.addKnob(params::id::fmLfoPmd, "Vibrato");
    fmModPanel.addKnob(params::id::fmLfoPms, "Vib Sens");
    fmModPanel.addKnob(params::id::fmLfoAmd, "Tremolo");
    // AMS: which operators the LFO tremolo reaches (per-op amp mod sensitivity).
    fmModPanel.addSection("LFO -> OP (Tremolo)");
    for (int op = 1; op <= 6; ++op)
        fmModPanel.addChoice(params::id::fmOp(op, "ams"), "OP " + juce::String(op));
    fmModPanel.setCellSize(110, 100);

    // --- global FX rack, top-to-bottom in AMY's actual processing order:
    //     EQ -> Chorus -> Echo -> Reverb -> Synth Vol (all inside AMY), then the
    //     host-side MASTER stage on the output buffer: bitcrusher (Freq, Bit) ->
    //     WDF saturator (Drive) -> Out Gain. ("Synth Vol" is AMY's volume, applied
    //     upstream — it can't move to the end, so Out Gain is the true final gain.)
    fxPanel.setCellSize(78, 84);
    fxPanel.addSection("EQ");
    fxPanel.addKnob(params::id::eqLow, "Low");
    fxPanel.addKnob(params::id::eqMid, "Mid");
    fxPanel.addKnob(params::id::eqHigh, "High");
    fxPanel.addSection("CHORUS");
    fxPanel.addKnob(params::id::chorus, "Level");
    fxPanel.addKnob(params::id::chorusRate, "Rate");
    fxPanel.addKnob(params::id::chorusDepth, "Depth");
    fxPanel.addSection("ECHO");
    fxPanel.addKnob(params::id::echo, "Level");
    fxPanel.addKnob(params::id::echoTime, "Time");
    fxPanel.addKnob(params::id::echoFeedback, "F.back");
    fxPanel.addKnob(params::id::echoTone, "Tone");
    fxPanel.addSection("REVERB");
    fxPanel.addKnob(params::id::reverb, "Level");
    fxPanel.addKnob(params::id::reverbSize, "Size");
    fxPanel.addKnob(params::id::reverbDamping, "Damp");
    fxPanel.addSection("MASTER");
    fxPanel.addKnob(params::id::bcFreq, "Freq");           // bitcrusher: downsample rate
    fxPanel.addKnob(params::id::bcBits, "Bit");            // bitcrusher: bit depth
    fxPanel.addKnob(params::id::clipDrive, "Drive");       // WDF diode saturator (analog warmth)
    fxPanel.addKnob(params::id::masterVolume, "Synth Vol"); // AMY engine volume (upstream)
    fxPanel.addKnob(params::id::outputGain, "Out Gain");   // true final gain (end of chain)
    addAndMakeVisible(fxPanel);

    // --- tabs -------------------------------------------------------------
    junoViewport.setViewedComponent(&junoCols, false);
    junoViewport.setScrollBarsShown(true, false);
    fmOscViewport.setViewedComponent(&fmOscCols, false);
    fmOscViewport.setScrollBarsShown(true, false);
    fmEnv1Viewport.setViewedComponent(&fmEnv1Tab, false);
    fmEnv1Viewport.setScrollBarsShown(true, false);
    fmEnv2Viewport.setViewedComponent(&fmEnv2Tab, false);
    fmEnv2Viewport.setScrollBarsShown(true, false);
    fmModViewport.setViewedComponent(&fmModPanel, false);
    fmModViewport.setScrollBarsShown(true, false);
    tabs.setOutline(0);
    tabs.addTab("Juno",     kPanel, &junoViewport,   false);
    tabs.addTab("DX7 1",    kPanel, &dx7Tab1,        false);   // algorithm + oscillators
    tabs.addTab("DX7 2",    kPanel, &fmEnv1Viewport, false);   // operator envelopes OP1-3
    tabs.addTab("DX7 3",    kPanel, &fmEnv2Viewport, false);   // operator envelopes OP4-6
    tabs.addTab("DX7 4",    kPanel, &fmModViewport,  false);   // pitch & mod
    tabs.addTab("AMYboard", kPanel, &hwPanel,        false);   // MIDI-out select + connect + send patch
    addAndMakeVisible(tabs);

    // Open on the tab matching the loaded engine, and seed lastTab so the first
    // timer tick doesn't read this as a user click (lastEngine stays -1 so the
    // initial dimming still applies).
    {
        int eng0 = 0;
        if (auto* e = s.getRawParameterValue(params::id::engine))
            eng0 = juce::jlimit(0, 2, (int) std::lround(e->load()));
        // Reopen on the AMYboard tab (5) if the session was in Hardware mode; else the
        // tab that matches the loaded engine (Analog->Juno 0, FM->DX7 1 = tab 1).
        const int tab0 = (proc.currentMode() == IAmyBackend::Kind::Hardware) ? 5
                       : (eng0 == 1) ? 0 : (eng0 == 2) ? 1 : tabs.getCurrentTabIndex();
        tabs.setCurrentTabIndex(tab0, false);
        lastTab = tab0;
    }

    // Height fits the tallest scrolled tab body without clipping: the Juno right
    // column has 5 sections (…VOICE) ~618px; window overhead (title + 2 top rows +
    // insets + tab bar) is ~156px, so >= 774px keeps VOICE fully visible.
    setSize(1280, 800);
    startTimerHz(15);
}

AmyPlugEditor::~AmyPlugEditor() { stopTimer(); }

void AmyPlugEditor::buildPatchBox()
{
    patchBox.clear(juce::dontSendNotification);
    juce::String bank;
    for (int i = 0; i < kBuiltinPatchCount; ++i)
    {
        const juce::String b = bankOf(i);
        if (b != bank) { patchBox.addSectionHeading(b); bank = b; }
        patchBox.addItem(kBuiltinPatchNames[i], i + 1);
    }
}

void AmyPlugEditor::refreshUserBox()
{
    userBox.clear(juce::dontSendNotification);
    userEntries = proc.patchLibrary().entries();
    int id = 1;
    juce::String curGroup = "\x01";                  // sentinel != any real group name
    for (const auto& e : userEntries)
    {
        if (e.group != curGroup)                     // new group -> section heading
        {
            curGroup = e.group;
            userBox.addSectionHeading(e.group.isEmpty() ? "My Patches" : e.group);
        }
        userBox.addItem(e.name, id++);
    }
}

void AmyPlugEditor::selectPatch(int patchNumber)
{
    if (auto* param = proc.apvts().getParameter(params::id::patchA))
        param->setValueNotifyingHost(param->convertTo0to1((float) patchNumber));
}

void AmyPlugEditor::stepPatch(int delta)
{
    selectPatch(juce::jlimit(0, kBuiltinPatchCount - 1, (lastPatch >= 0 ? lastPatch : 0) + delta));
}

void AmyPlugEditor::showSaveDialog()
{
    auto* w = new juce::AlertWindow("Save User Patch", "Patch name:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", "My Patch");
    w->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w] (int result)
    {
        if (result == 1)
        {
            const auto name = w->getTextEditorContents("name").trim();
            if (name.isNotEmpty()) { proc.saveUserPatch(name); refreshUserBox();
                                     userBox.setText(name, juce::dontSendNotification); }
        }
    }), true);
}

void AmyPlugEditor::importDx7()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Import a DX7 .syx cartridge", juce::File {}, "*.syx;*.SYX");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File {}) return;            // cancelled
        const int n = proc.importDx7Cartridge(file);
        refreshUserBox();
        juce::AlertWindow::showMessageBoxAsync(
            n > 0 ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
            "DX7 Import",
            n > 0 ? juce::String(n) + (n == 1 ? " voice" : " voices")
                        + " imported into your USER patches."
                  : "No DX7 voices found. Expected a .syx cartridge (32-voice bulk "
                    "dump or a single-voice dump).");
    });
}

void AmyPlugEditor::timerCallback()
{
    // Always-on readout of what's actually making sound, and the take-over button only
    // when another instance holds the single global AMY engine.
    {
        const bool hardware = (proc.currentMode() == IAmyBackend::Kind::Hardware);
        const bool owns     = proc.ownsSoftwareEngine();
        const bool busy     = ! hardware && ! owns;

        static constexpr const char* kEngineName[] = { "Factory", "Analog", "FM" };
        juce::String text;
        juce::Colour colour;
        if (hardware)
        {
            auto* hw = proc.hardwareBackend();
            const juce::String dev = (hw && hw->isConnected()) ? hw->connectedName()
                                                               : juce::String("no device");
            text   = juce::String::fromUTF8("\xE2\x97\x8F HARDWARE \xC2\xB7 ") + dev;
            colour = juce::Colour(0xff35a0d0);   // cyan-ish
        }
        else if (busy)
        {
            text   = juce::String::fromUTF8("\xE2\x97\x8B SILENT \xC2\xB7 engine in use by another instance");
            colour = juce::Colours::orange;
        }
        else
        {
            int eng = 0;
            if (auto* e = proc.apvts().getRawParameterValue(params::id::engine))
                eng = juce::jlimit(0, 2, (int) std::lround(e->load()));
            text   = juce::String::fromUTF8("\xE2\x97\x8F SOFTWARE \xC2\xB7 ") + kEngineName[eng];
            colour = juce::Colour(0xff4caf50);   // green
        }

        if (text != lastStatusText)
        {
            lastStatusText = text;
            engineStatusLabel.setText(text, juce::dontSendNotification);
            engineStatusLabel.setColour(juce::Label::textColourId, colour);
        }
        if (busy != lastBusy)
        {
            lastBusy = busy;
            takeoverButton.setVisible(busy);
        }
    }

    if (auto* raw = proc.apvts().getRawParameterValue(params::id::patchA))
    {
        const int n = juce::jlimit(0, kBuiltinPatchCount - 1, (int) std::lround(raw->load()));
        if (n != lastPatch)
        {
            lastPatch = n;
            patchBox.setSelectedId(n + 1, juce::dontSendNotification);
            // "To Editor" decodes factory Juno (0..127) and DX7 (128..255) presets;
            // piano/amyboard (256..257) have no editable structure.
            toEditorButton.setEnabled(n >= 0 && n <= 255);
        }
    }

    // Keep the FM algorithm diagram in sync with the selected algorithm.
    if (auto* raw = proc.apvts().getRawParameterValue(params::id::fmAlgorithm))
    {
        const int a = juce::jlimit(1, 32, (int) std::lround(raw->load()) + 1);
        if (a != lastAlgo) { lastAlgo = a; algoDiagram.setAlgorithm(a); }
    }

    // Engine ↔ tab sync + dim the inactive engines so it's clear what's live.
    // 0 = Factory (PATCH browser), 1 = Analog (Juno tab), 2 = FM (DX7 tab).
    if (auto* e = proc.apvts().getRawParameterValue(params::id::engine))
    {
        int eng = juce::jlimit(0, 2, (int) std::lround(e->load()));

        // A user tab click activates that tab's engine. Tabs: 0 Juno, 1-3 DX7, 4 AMYboard.
        const int curTab = tabs.getCurrentTabIndex();
        if (curTab != lastTab)
        {
            lastTab = curTab;
            const int tabEng = (curTab == 0) ? 1 : (curTab >= 1 && curTab <= 4) ? 2 : eng;
            if (tabEng != eng) { setEngineIndex(tabEng); eng = tabEng; }
        }

        if (eng != lastEngine)
        {
            lastEngine = eng;
            // Analog -> Juno (0); FM -> keep the current DX7 tab (1-4) or default to DX7 1.
            const int wantTab = (eng == 1) ? 0
                              : (eng == 2) ? ((curTab >= 1 && curTab <= 4) ? curTab : 1)
                              : curTab;
            if (wantTab != tabs.getCurrentTabIndex())
            { tabs.setCurrentTabIndex(wantTab, false); lastTab = wantTab; }

            const bool analog = (eng == 1), fm = (eng == 2), factory = (eng == 0);
            junoCols.setEnabled(analog); junoCols.setAlpha(analog ? 1.0f : 0.4f);
            for (auto* p : { (juce::Component*) &fmOscA, (juce::Component*) &fmOscB,
                             (juce::Component*) &fmOscC, (juce::Component*) &fmEnv1Panel,
                             (juce::Component*) &fmEnv2Panel, (juce::Component*) &fmModPanel })
            { p->setEnabled(fm); p->setAlpha(fm ? 1.0f : 0.4f); }
            for (auto* c : { (juce::Component*) &patchBox, (juce::Component*) &prevButton,
                             (juce::Component*) &nextButton, (juce::Component*) &browserLabel })
                c->setAlpha(factory ? 1.0f : 0.4f);
        }
    }
}

void AmyPlugEditor::setEngineIndex(int idx)
{
    if (auto* e = proc.apvts().getParameter(params::id::engine))
        e->setValueNotifyingHost(e->convertTo0to1((float) juce::jlimit(0, 2, idx)));
}

void AmyPlugEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("AMYplug", 16, 8, 200, 24, juce::Justification::left);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(juce::String::fromUTF8("AMY for your DAW · editor v2"), 16, 30, 300, 14, juce::Justification::left);
}

void AmyPlugEditor::resized()
{
    // Engine status readout (+ optional take-over button) in the title band, right
    // of the "AMYplug" title.
    {
        auto band = getLocalBounds().removeFromTop(46).reduced(12, 10);
        band.removeFromLeft(300);   // clear the title text
        takeoverButton.setBounds(band.removeFromRight(140));
        band.removeFromRight(10);
        engineStatusLabel.setBounds(band);
    }

    auto r = getLocalBounds().reduced(12);
    r.removeFromTop(34);   // title

    // Top bar: two rows (patch browser, user + engine + panic).
    auto row1 = r.removeFromTop(26);
    browserLabel.setBounds(row1.removeFromLeft(40));
    importButton.setBounds(row1.removeFromRight(104));
    row1.removeFromRight(6);
    toEditorButton.setBounds(row1.removeFromRight(84));
    row1.removeFromRight(10);
    nextButton.setBounds(row1.removeFromRight(28));
    prevButton.setBounds(row1.removeFromRight(28));
    row1.removeFromRight(4);
    patchBox.setBounds(row1);
    r.removeFromTop(6);

    auto row2 = r.removeFromTop(26);
    userLabel.setBounds(row2.removeFromLeft(40));
    panicButton.setBounds(row2.removeFromRight(72));
    row2.removeFromRight(8);
    engineBox.setBounds(row2.removeFromRight(90));
    engineLabel.setBounds(row2.removeFromRight(48));
    row2.removeFromRight(8);
    deleteButton.setBounds(row2.removeFromRight(60));
    saveButton.setBounds(row2.removeFromRight(62));
    row2.removeFromRight(6);
    userBox.setBounds(row2);
    r.removeFromTop(10);

    // Tabs (left) + FX rack (right). The rack is wide enough for the 5-knob MASTER
    // row (Freq, Bit, Drive, Synth Vol, Out Gain).
    auto fx = r.removeFromRight(330);
    fxPanel.setBounds(fx);
    r.removeFromRight(10);
    tabs.setBounds(r);

    // Size the scrolled Juno/FM panels to the tab body width (minus insets + a
    // scrollbar). Using the tab width rather than each viewport keeps the FM panel
    // correct even before the DX7 tab is first shown.
    const int contentW = juce::jmax(200, r.getWidth() - 18);
    // Both tabs use two columns; each container is as tall as its taller column.
    const int junoH = juce::jmax(junoPanelL.preferredHeight(), junoPanelR.preferredHeight());
    junoCols.setSize(contentW, junoH);
    // DX7 1 oscillators: 3 columns, container as tall as its tallest column (2 ops).
    const int oscH = juce::jmax(fmOscA.preferredHeight(),
                                juce::jmax(fmOscB.preferredHeight(), fmOscC.preferredHeight()));
    fmOscCols.setSize(contentW, oscH);
    // DX7 2/3: a row of viewers atop the knob rows (EnvGraphRow sizes its inner panel).
    fmEnv1Tab.setSize(contentW, fmEnv1Tab.preferredHeight());
    fmEnv2Tab.setSize(contentW, fmEnv2Tab.preferredHeight());
    // DX7 4 pitch/mod: single full-width column.
    fmModPanel.setSize(contentW, fmModPanel.preferredHeight());
}
} // namespace amyplug
