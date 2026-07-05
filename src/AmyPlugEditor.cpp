// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugEditor.h"
#include "engine/HardwareBackend.h"
#include "state/Parameters.h"
#include "state/FmAlgorithms.h"
#include "state/Dx7Envelope.h"
#include "gui/AmyFonts.h"
#include "gui/AmyColours.h"
#include "BuiltinPatchNames.h"
#include <array>

namespace amyplug
{
namespace col = amyplug::colours;
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

EnvelopeDisplay::EnvelopeDisplay(juce::AudioProcessorValueTreeState& s, PitchTag) : pitch(true)
{
    for (int e = 0; e < 4; ++e)
    {
        rateP[e]  = s.getRawParameterValue(params::id::fmPitchEg('r', e + 1));
        levelP[e] = s.getRawParameterValue(params::id::fmPitchEg('l', e + 1));
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
    g.setColour(juce::Colour(0xff0a0f14));         // dark inset panel
    g.fillRoundedRectangle(b, 3.0f);
    g.setColour(col::hairline);
    g.drawRoundedRectangle(b, 3.0f, 1.0f);

    const auto plot = b.reduced(6.0f);
    float L[4], R[4];
    for (int e = 0; e < 4; ++e)
    {
        L[e] = levelP[e] ? levelP[e]->load() : (pitch ? 50.0f : 0.0f);
        R[e] = rateP[e]  ? rateP[e]->load()  : 99.0f;
    }
    // Segment durations (sqrt-compressed so long tails stay visible but bounded), plus
    // a fixed sustain hold. Shape: L4 -> L1 -> L2 -> L3 -> (hold) -> L4. The pitch EG
    // uses its own rate curve (pitchSegSeconds); the operator EG uses the amp curve.
    using namespace amyplug::dx7env;
    auto seg = [this] (double r, double from, double to, bool rel)
    { return pitch ? pitchSegSeconds(r, from, to, rel) : segSeconds(r, from, to, rel); };
    const double t0 = std::sqrt(seg(R[0], L[3], L[0], false));
    const double t1 = std::sqrt(seg(R[1], L[0], L[1], false));
    const double t2 = std::sqrt(seg(R[2], L[1], L[2], false));
    const double t3 = std::sqrt(seg(R[3], L[2], L[3], true));
    const double sus = 0.4 * (t0 + t1 + t2 + t3 + 0.001);
    const double total = t0 + t1 + t2 + t3 + sus + 1e-6;

    auto X = [&] (double acc) { return plot.getX() + (float) (acc / total) * plot.getWidth(); };
    auto Y = [&] (float lvl)  { return plot.getBottom() - (lvl / 99.0f) * plot.getHeight(); };

    // Pitch EG: draw a faint centre line at level 50 (= no pitch shift) for reference.
    if (pitch)
    {
        g.setColour(col::hairline);
        const float yc = Y(50.0f);
        g.drawLine(plot.getX(), yc, plot.getRight(), yc, 1.0f);
    }

    juce::Path p;
    double acc = 0.0;
    p.startNewSubPath(X(acc), Y(L[3]));               // start at release floor L4
    acc += t0; p.lineTo(X(acc), Y(L[0]));             // attack -> L1
    acc += t1; p.lineTo(X(acc), Y(L[1]));             // decay  -> L2
    acc += t2; p.lineTo(X(acc), Y(L[2]));             // decay  -> L3 (sustain)
    acc += sus; p.lineTo(X(acc), Y(L[2]));            // sustain hold
    acc += t3; p.lineTo(X(acc), Y(L[3]));             // release -> L4
    g.setColour(col::engineCyan);
    g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
}
namespace
{
const juce::Colour kBg     = col::shellTop;      // window body (gradient top)
const juce::Colour kPanel  = col::tabActive;     // tab / panel-area fill

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
void ControlPanel::addSection(const juce::String& title, juce::Colour accent)
{
    sectionTitles.add(title);
    sectionAccents.push_back(accent);
}

namespace
{
juce::Colour accentOf(const std::vector<juce::Colour>& accents, int sec)
{
    return (sec >= 0 && sec < (int) accents.size()) ? accents[(size_t) sec] : col::engineCyan;
}
void styleControlLabel(juce::Label& l, const juce::String& name)
{
    l.setText(name.toUpperCase(), juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(fonts::label(13.0f).withExtraKerningFactor(0.06f));
    l.setColour(juce::Label::textColourId, col::textDim);
}
} // namespace

void ControlPanel::addKnob(const juce::String& paramId, const juce::String& name)
{
    auto c = std::make_unique<Control>();
    c->section = juce::jmax(0, sectionTitles.size() - 1);
    c->knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                             juce::Slider::TextBoxBelow);
    c->knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 22);
    // 270° sweep with a 90° gap at the bottom (matches the LookAndFeel arc drawing).
    c->knob->setRotaryParameters(juce::degreesToRadians(225.0f),
                                 juce::degreesToRadians(495.0f), true);
    c->knob->setColour(juce::Slider::rotarySliderFillColourId,
                       accentOf(sectionAccents, c->section));
    // Stepped params (coarse/fine/detune/level/R/L…) read as integers; continuous
    // ones (freq/duty/env…) get 2 decimals — avoids the raw 7-digit float readout.
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(paramId)))
        c->knob->setNumDecimalPlacesToDisplay(rp->getNormalisableRange().interval >= 1.0f ? 0 : 2);
    addAndMakeVisible(*c->knob);
    styleControlLabel(c->label, name);
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
    styleControlLabel(c->label, name);
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
    // Content height only (title + body per section, gaps between). sectionBoxes()
    // compares this against the already-reduced bounds, so the reduced(4) margin must
    // NOT be included here or every panel under-fills by 8px.
    const int n = sectionTitles.size();
    int h = 0;
    for (int sec = 0; sec < n; ++sec)
        h += kTitleH + baseBodyHeight(sec);       // taller body if it has a viewer
    if (n > 0) h += (n - 1) * kGap;               // gaps BETWEEN sections only
    return h;
}

std::vector<juce::Rectangle<int>> ControlPanel::sectionBoxes() const
{
    std::vector<juce::Rectangle<int>> boxes;
    const int n = sectionTitles.size();
    if (n == 0) return boxes;

    auto r = getLocalBounds().reduced(4);
    // Distribute any height above the preferred minimum evenly across the section
    // bodies so the cards fill the panel and the knobs get breathing room. No trailing
    // gap, so a single-section panel (VOICE) ends flush like a column's last section.
    const int slack = juce::jmax(0, r.getHeight() - preferredHeight());
    for (int sec = 0; sec < n; ++sec)
    {
        const int extra = slack / n + (sec < slack % n ? 1 : 0);
        boxes.push_back(r.removeFromTop(kTitleH + baseBodyHeight(sec) + extra));
        if (sec < n - 1) r.removeFromTop(kGap);
    }
    return boxes;
}

void ControlPanel::paint(juce::Graphics& g)
{
    const auto boxes = sectionBoxes();
    for (int sec = 0; sec < (int) boxes.size(); ++sec)
    {
        auto box = boxes[(size_t) sec];
        // Card: panel fill + hairline border.
        g.setColour(col::panel);
        g.fillRoundedRectangle(box.toFloat(), 6.0f);
        g.setColour(col::hairline);
        g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 6.0f, 1.0f);

        // Accent header bar (top corners rounded to match the card).
        auto tb = box.removeFromTop(kTitleH).toFloat();
        const auto accent = accentOf(sectionAccents, sec);
        juce::Path hp;
        hp.addRoundedRectangle(tb.getX(), tb.getY(), tb.getWidth(), tb.getHeight() + 6.0f,
                               6.0f, 6.0f, true, true, false, false);
        g.setColour(accent);   // full-alpha bar so the dark title text stays readable
        g.fillPath(hp);
        g.setColour(col::headerTextOn(accent));
        g.setFont(fonts::header(13.0f).withExtraKerningFactor(0.14f));
        g.drawText(sectionTitles[sec].toUpperCase(), tb, juce::Justification::centred);
    }
}

void ControlPanel::resized()
{
    const auto boxes = sectionBoxes();
    for (int sec = 0; sec < (int) boxes.size(); ++sec)
    {
        auto box = boxes[(size_t) sec];
        box.removeFromTop(kTitleH);
        box.removeFromTop(4);   // padding between the header bar and the knob labels

        // Viewer (if any) sits in a fixed-width slot on the LEFT, vertically centred.
        if (graphForSection.count(sec))
        {
            auto gcell = box.removeFromLeft(kGraphW);
            graphForSection[sec]->setBounds(gcell.reduced(6).withSizeKeepingCentre(
                juce::jmin(200, gcell.getWidth() - 12), juce::jmin(112, gcell.getHeight() - 12)));
            box.removeFromLeft(kGap);
        }

        int count = 0;
        for (auto& c : controls) if (c->section == sec) ++count;
        if (count == 0) continue;

        // A fixed-height control band, vertically centred in the (possibly taller) body
        // so every knob keeps its size with even top/bottom margin. Labels stay aligned.
        auto band = box.withSizeKeepingCentre(box.getWidth(), juce::jmin(ctrlH, box.getHeight()));
        const int slotW = band.getWidth() / count;
        int i = 0;
        for (auto& c : controls)
        {
            if (c->section != sec) continue;
            juce::Rectangle<int> slot(band.getX() + i * slotW, band.getY(), slotW, band.getHeight());
            // Combos fill their slot (some menus need the width); knobs stay capped.
            const int cw = c->combo ? (slotW - 6) : juce::jmin(cellW, slotW - 4);
            auto cell = slot.withSizeKeepingCentre(cw, band.getHeight());
            c->label.setBounds(cell.removeFromTop(16));
            cell.removeFromTop(6);   // breathing room between the label and the knob/selector
            if (c->combo)      c->combo->setBounds(cell.removeFromTop(28));
            else if (c->knob)  c->knob->setBounds(cell);
            ++i;
        }
    }
}

void PlaceholderPanel::paint(juce::Graphics& g)
{
    g.setColour(col::textFaint);
    g.setFont(fonts::label(15.0f).withExtraKerningFactor(0.04f));
    g.drawFittedText(text, getLocalBounds().reduced(20), juce::Justification::centred, 3);
}

// ===========================================================================
// HardwarePanel — the AMYboard tab
// ===========================================================================
HardwarePanel::HardwarePanel(AmyPlugProcessor& p) : proc(p)
{
    title.setFont(fonts::header(18.0f).withExtraKerningFactor(0.04f));
    title.setColour(juce::Label::textColourId, col::textPrimary);
    addAndMakeVisible(title);
    devLabel.setFont(fonts::label(12.0f).withExtraKerningFactor(0.06f));
    devLabel.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(devLabel);
    status.setFont(fonts::mono(12.5f));
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
                     hwMode ? (conn ? col::statusGreen : col::panicRed) : col::statusGreen);
    connectBtn.setEnabled(! conn && deviceBox.getNumItems() > 0);
    disconnectBtn.setEnabled(conn);
    sendBtn.setEnabled(conn);
}

void HardwarePanel::paint(juce::Graphics& g)
{
    // Single card filling the tab body.
    auto b = getLocalBounds().toFloat().reduced(6.0f);
    g.setColour(col::panel);
    g.fillRoundedRectangle(b, 6.0f);
    g.setColour(col::hairline);
    g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

    g.setColour(col::textFaint);
    g.setFont(fonts::mono(11.5f));
    g.drawFittedText("Connect the AMYboard's MIDI port to play the sound ON THE BOARD - the plugin goes\n"
                     "silent and pushes the current patch. Disconnect to return to the plugin's own sound.",
                     getLocalBounds().removeFromBottom(56).reduced(24, 8), juce::Justification::topLeft, 3);
}

void HardwarePanel::resized()
{
    auto r = getLocalBounds().reduced(20);
    title.setBounds(r.removeFromTop(30));
    r.removeFromTop(18);

    const int kLabelW = 70, kGap = 12, kRowH = 30;
    // MIDI Out selector — the buttons below span its width, aligned to its left edge.
    int selX = 0, selW = 340;
    { auto line = r.removeFromTop(kRowH); devLabel.setBounds(line.removeFromLeft(kLabelW));
      refreshBtn.setBounds(line.removeFromRight(90));
      auto box = line.removeFromLeft(selW);
      deviceBox.setBounds(box); selX = box.getX(); selW = box.getWidth();
      r.removeFromTop(12); }
    // Connect + Disconnect share the selector width; Send Patch spans the whole width.
    { auto line = r.removeFromTop(kRowH);
      juce::Rectangle<int> row(selX, line.getY(), selW, line.getHeight());
      connectBtn.setBounds(row.removeFromLeft((selW - kGap) / 2));
      row.removeFromLeft(kGap);
      disconnectBtn.setBounds(row);
      r.removeFromTop(12); }
    { auto line = r.removeFromTop(kRowH);
      sendBtn.setBounds(selX, line.getY(), selW, line.getHeight());
      r.removeFromTop(18); }
    { auto line = r.removeFromTop(26);
      status.setBounds(selX, line.getY(), line.getRight() - selX, 26); }
}

// ===========================================================================
// AlgorithmDiagram
// ===========================================================================
void AlgorithmDiagram::paint(juce::Graphics& g)
{
    const juce::Colour kAccent = col::engineCyan;    // carriers / output
    const juce::Colour kMod    { 0xff1a222a };        // modulator fill
    g.setColour(col::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(col::hairline);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);

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
    g.setColour(col::textDim);
    g.setFont(fonts::header(12.0f).withExtraKerningFactor(0.1f));
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
    g.setColour(juce::Colour { 0xff3a4550 });
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
    g.setColour(kAccent);
    g.setFont(fonts::label(10.0f).withExtraKerningFactor(0.06f));
    g.drawText("output", juce::Rectangle<int> { barR + 4, outBar.getY(), 48, outBar.getHeight() },
               juce::Justification::centredLeft);

    // Operator boxes. Carriers (feed the output) get a cyan fill/border + glow; the
    // number is cyan. Modulators are neutral dark cards.
    for (int op = 1; op <= 6; ++op)
    {
        auto bx = boxOf(op);
        const bool carrier = topo.carriers.contains(op);
        if (carrier)
        {
            g.setColour(kAccent.withAlpha(0.28f));
            g.fillRoundedRectangle(bx.toFloat().expanded(2.0f), 5.0f);   // soft glow
        }
        g.setColour(carrier ? juce::Colour { 0xff0d2a30 } : kMod);
        g.fillRoundedRectangle(bx.toFloat(), 4.0f);
        g.setColour(carrier ? kAccent : juce::Colour { 0xff38434e });
        g.drawRoundedRectangle(bx.toFloat(), 4.0f, carrier ? 2.0f : 1.2f);
        g.setColour(carrier ? kAccent : juce::Colour { 0xffc3ccd6 });
        g.setFont(fonts::header(13.0f));
        g.drawText(juce::String(op), bx, juce::Justification::centred);
    }

    // Feedback "FB" tags LAST, so they always sit in front of the operator boxes —
    // in some algorithms (8, 17, 18) the tag overlaps a neighbouring box that would
    // otherwise be painted over it if drawn inside the box loop above.
    for (int op = 1; op <= 6; ++op)
        if (topo.feedback[(size_t) op])
        {
            auto bx = boxOf(op);
            juce::Rectangle<int> tag { bx.getRight() - 15, bx.getY() - 9, 20, 13 };
            g.setColour(col::amber);
            g.fillRoundedRectangle(tag.toFloat(), 3.0f);
            g.setColour(juce::Colour { 0xff231605 });
            g.setFont(fonts::label(9.0f));
            g.drawText("FB", tag, juce::Justification::centred);
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
        l->setFont(amyplug::fonts::label(12.0f).withExtraKerningFactor(0.06f));
        l->setColour(juce::Label::textColourId, amyplug::colours::textDim);
        addAndMakeVisible(*l);
    }
    algoLabel.setText("ALGORITHM", juce::dontSendNotification);
    fbLabel.setText("FEEDBACK", juce::dontSendNotification);
}

void Dx7TabComponent::resized()
{
    auto r = getLocalBounds().reduced(4);
    auto top = r.removeFromTop(kTopH);
    r.removeFromTop(4);                                    // gap to the operator grid

    // The operator grid below is 3 equal columns; make the watermark card the same
    // size as (and aligned with) the rightmost — the OP3 cell.
    const int colW = top.getWidth() / 3;
    watermarkCard = top.removeFromRight(colW).reduced(4);  // "DX7 / OPERATOR TUNING"
    selectorCard  = top.removeFromRight(kSelectorW).reduced(4);
    diagram.setBounds(top.reduced(4));                     // algorithm diagram (left)

    auto sel = selectorCard.reduced(14, 16);
    algoLabel.setBounds(sel.removeFromTop(16));
    sel.removeFromTop(2);
    algoBox.setBounds(sel.removeFromTop(26));
    sel.removeFromTop(16);
    fbLabel.setBounds(sel.removeFromTop(16));
    sel.removeFromTop(2);
    fbKnob.setBounds(sel.removeFromTop(juce::jmin(96, sel.getHeight())));

    controlsView.setBounds(r);
}

void Dx7TabComponent::paint(juce::Graphics& g)
{
    // Selector card + watermark card backgrounds (the algorithm diagram paints its own).
    for (auto card : { selectorCard, watermarkCard })
    {
        if (card.isEmpty()) continue;
        g.setColour(col::panel);
        g.fillRoundedRectangle(card.toFloat(), 6.0f);
        g.setColour(col::hairline);
        g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 6.0f, 1.0f);
    }

    // Watermark: big "DX7" (neutral grey) + "OPERATOR TUNING" (cyan) + subtitle.
    if (! watermarkCard.isEmpty())
    {
        auto w = watermarkCard.reduced(16);
        g.setColour(col::tabIndicator);
        g.setFont(fonts::logo(40.0f));
        g.drawText("DX7", w.removeFromTop(46), juce::Justification::centred);
        g.setColour(col::engineCyan);
        g.setFont(fonts::header(15.0f).withExtraKerningFactor(0.12f));
        g.drawText("OPERATOR TUNING", w.removeFromTop(20), juce::Justification::centred);
        g.setColour(col::textFaint);
        g.setFont(fonts::label(11.0f).withExtraKerningFactor(0.06f));
        g.drawText(juce::String::fromUTF8("6-OPERATOR FM \xC2\xB7 32 ALGORITHMS"),
                   w.removeFromTop(16), juce::Justification::centred);
    }
}

// ===========================================================================
// TabPage — centered two-tone page title + content
// ===========================================================================
void TabPage::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().removeFromTop(kTitleH).reduced(0, 4);
    auto titleFont = fonts::header(20.0f).withExtraKerningFactor(0.05f);
    g.setFont(titleFont);
    // "DX7" (grey) + "GLOBAL" (accent) laid on one baseline; join tightly after a '-'.
    const bool joined = grey.endsWithChar('-');
    const int gw = (int) juce::GlyphArrangement::getStringWidth(titleFont, grey);
    const int aw = (int) juce::GlyphArrangement::getStringWidth(titleFont, accent);
    const int gap = joined ? 0 : (int) juce::GlyphArrangement::getStringWidth(titleFont, " ");
    auto row = area.removeFromTop(24);
    const int startX = row.getCentreX() - (gw + gap + aw) / 2;
    g.setColour(col::tabIndicator);
    g.drawText(grey, juce::Rectangle<int>(startX, row.getY(), gw + 8, row.getHeight()),
               juce::Justification::centredLeft, false);
    g.setColour(col::engineCyan);
    g.drawText(accent, juce::Rectangle<int>(startX + gw + gap, row.getY(), aw + 8, row.getHeight()),
               juce::Justification::centredLeft, false);

    // Subtitle: bigger + bold + brighter so it's clearly readable under the title.
    g.setColour(col::textDim);
    g.setFont(fonts::header(13.0f).withExtraKerningFactor(0.16f));
    g.drawText(sub, area, juce::Justification::centredTop);
}

// ===========================================================================
// JunoPage — "JUNO" title (left) + VOICE card (right) over the two synth columns
// ===========================================================================
void JunoPage::resized()
{
    auto r = getLocalBounds().reduced(4);
    // One shared section height across the VOICE row (1 section) and the two columns
    // (4 sections each), so every JUNO card is exactly the same size. The vertical
    // budget holds 5 stacked sections plus their card insets (see ControlPanel).
    const int s = juce::jmax(80, (r.getHeight() - 40) / 5);
    auto top = r.removeFromTop(s + 8);   // VOICE row = one section + the panel's insets
    titleArea = top.removeFromLeft(top.getWidth() / 2 - 4);
    top.removeFromLeft(8);
    voiceC.setBounds(top);
    // No extra gap here: the VOICE card's bottom inset + the columns' top inset already
    // give the same visual gap as between the column sections (even spacing).
    const int colGap = 8;
    const int colW = (r.getWidth() - colGap) / 2;
    leftC.setBounds(r.removeFromLeft(colW));
    r.removeFromLeft(colGap);
    rightC.setBounds(r);
}

void JunoPage::paint(juce::Graphics& g)
{
    if (titleArea.isEmpty()) return;
    auto a = titleArea.reduced(10, 0);
    auto block = a.withSizeKeepingCentre(a.getWidth(), 74);
    auto titleRow = block.removeFromTop(48);
    auto subRow   = block.removeFromTop(18);

    auto subFont = fonts::header(14.0f).withExtraKerningFactor(0.12f);
    const float subW  = juce::GlyphArrangement::getStringWidth(subFont, "ANALOG ENGINE");
    auto junoFont = fonts::logo(42.0f);
    const float baseW = juce::GlyphArrangement::getStringWidth(junoFont, "JUNO");
    // Space "JUNO" out so its width matches the "ANALOG ENGINE" line below it.
    junoFont = junoFont.withExtraKerningFactor(juce::jmax(0.0f, (subW - baseW) / (42.0f * 4.0f)));

    g.setColour(col::textPrimary);
    g.setFont(junoFont);
    g.drawText("JUNO", titleRow, juce::Justification::centredLeft, false);
    g.setColour(col::junoRed);
    g.setFont(subFont);
    g.drawText("ANALOG ENGINE", subRow, juce::Justification::centredLeft, false);
}

// ===========================================================================
// AmyPlugEditor
// ===========================================================================
AmyPlugEditor::AmyPlugEditor(AmyPlugProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p)
{
    auto& s = proc.apvts();
    setLookAndFeel(&lnf);   // the AMYplug visual identity, inherited by all children

    // --- top bar ----------------------------------------------------------
    for (auto* l : { &browserLabel, &userLabel, &engineLabel })
    {
        l->setFont(fonts::label(12.0f).withExtraKerningFactor(0.06f));
        l->setColour(juce::Label::textColourId, col::textDim);
        l->setJustificationType(juce::Justification::centredLeft);
    }
    for (auto* l : { &browserLabel, &userLabel }) addAndMakeVisible(*l);

    // OUT GAIN rotary (amber) lives in the header, right of the patch block.
    outGainKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
    outGainKnob.setRotaryParameters(juce::degreesToRadians(225.0f),
                                    juce::degreesToRadians(495.0f), true);
    outGainKnob.setColour(juce::Slider::rotarySliderFillColourId, col::amber);
    outGainKnob.setNumDecimalPlacesToDisplay(1);
    outGainAtt = std::make_unique<Apvts::SliderAttachment>(s, params::id::outputGain, outGainKnob);
    addAndMakeVisible(outGainKnob);
    outGainLabel.setFont(fonts::label(12.0f).withExtraKerningFactor(0.06f));
    outGainLabel.setColour(juce::Label::textColourId, col::textDim);
    outGainLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(outGainLabel);
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
    addAndMakeVisible(engineLabel);

    panicButton.setColour(juce::TextButton::buttonColourId, col::panicRed);
    panicButton.onClick = [this] { proc.requestPanic(); };
    addAndMakeVisible(panicButton);

    // Always-on engine status readout (text + colour set each tick in timerCallback).
    engineStatusLabel.setJustificationType(juce::Justification::centredRight);
    engineStatusLabel.setFont(fonts::header(13.0f).withExtraKerningFactor(0.06f));
    addAndMakeVisible(engineStatusLabel);
    // The take-over button appears only when another instance holds the engine.
    takeoverButton.setColour(juce::TextButton::buttonColourId, col::amber);
    takeoverButton.onClick = [this] { proc.takeOverSoftwareEngine(); };
    addChildComponent(takeoverButton);

    // --- Juno tab: two columns. OSC A|OSC B / VCF|VCF ENV / LFO|AMP ENV --------
    junoPanelL.addSection("OSC A", col::junoRed);
    junoPanelL.addChoice(params::id::oscAWave, "Wave");
    junoPanelL.addKnob(params::id::oscAFreq, "Freq");
    junoPanelL.addKnob(params::id::oscACoarse, "Coarse");
    junoPanelL.addKnob(params::id::oscAFine, "Fine");
    junoPanelL.addKnob(params::id::oscADuty, "Duty");
    junoPanelL.addKnob(params::id::oscALevel, "Level");
    junoPanelL.addSection("OSC C", col::junoRed);
    junoPanelL.addChoice(params::id::oscCWave, "Wave");
    junoPanelL.addKnob(params::id::oscCFreq, "Freq");
    junoPanelL.addKnob(params::id::oscCCoarse, "Coarse");
    junoPanelL.addKnob(params::id::oscCFine, "Fine");
    junoPanelL.addKnob(params::id::oscCDuty, "Duty");
    junoPanelL.addKnob(params::id::oscCLevel, "Level");
    junoPanelL.addSection("VCF", col::filterViolet);
    junoPanelL.addKnob(params::id::filterCutoff, "Freq");
    junoPanelL.addKnob(params::id::filterReso, "Reso");
    junoPanelL.addKnob(params::id::vcfKbd, "Kbd");
    junoPanelL.addKnob(params::id::vcfEnv, "Env");
    junoPanelL.addChoice(params::id::vcfType, "Type");
    junoPanelL.addSection("LFO", col::lfoGreen);
    junoPanelL.addChoice(params::id::lfoMode, "Mode");
    junoPanelL.addChoice(params::id::lfoWave, "Wave");
    junoPanelL.addKnob(params::id::lfoFreq, "Freq");
    junoPanelL.addChoice(params::id::lfoSyncRate, "Sync");
    junoPanelL.addKnob(params::id::lfoToPitch, "Pitch");
    junoPanelL.addKnob(params::id::lfoToPwm, "PWM");
    junoPanelL.addKnob(params::id::lfoToFilter, "Filter");

    junoPanelR.addSection("OSC B", col::junoRed);
    junoPanelR.addChoice(params::id::oscBWave, "Wave");
    junoPanelR.addKnob(params::id::oscBFreq, "Freq");
    junoPanelR.addKnob(params::id::oscBCoarse, "Coarse");
    junoPanelR.addKnob(params::id::oscBFine, "Fine");
    junoPanelR.addKnob(params::id::oscBDuty, "Duty");
    junoPanelR.addKnob(params::id::oscBLevel, "Level");
    junoPanelR.addSection("OSC D", col::junoRed);
    junoPanelR.addChoice(params::id::oscDWave, "Wave");
    junoPanelR.addKnob(params::id::oscDFreq, "Freq");
    junoPanelR.addKnob(params::id::oscDCoarse, "Coarse");
    junoPanelR.addKnob(params::id::oscDFine, "Fine");
    junoPanelR.addKnob(params::id::oscDDuty, "Duty");
    junoPanelR.addKnob(params::id::oscDLevel, "Level");
    junoPanelR.addSection("VCF ENV", col::filterViolet);
    junoPanelR.addKnob(params::id::vcfAttack, "A");
    junoPanelR.addKnob(params::id::vcfDecay, "D");
    junoPanelR.addKnob(params::id::vcfSustain, "S");
    junoPanelR.addKnob(params::id::vcfRelease, "R");
    junoPanelR.addSection("AMP ENV", col::junoBlue);
    junoPanelR.addKnob(params::id::ampAttack, "A");
    junoPanelR.addKnob(params::id::ampDecay, "D");
    junoPanelR.addKnob(params::id::ampSustain, "S");
    junoPanelR.addKnob(params::id::ampRelease, "R");
    // Voicing lives with the synth, not the FX rack. Mode + Glide are global (they
    // also affect DX7); Unison + Detune are analog-only. VOICE sits in the JUNO top row.
    voicePanel.addSection("VOICE", col::amber);
    voicePanel.addChoice(params::id::voiceMode, "Mode");   // Poly / Mono / Legato
    voicePanel.addKnob(params::id::glide, "Glide");        // portamento (ms)
    voicePanel.addKnob(params::id::unisonVoices, "Unison"); // stacked detuned copies
    voicePanel.addKnob(params::id::unisonDetune, "Detune"); // unison spread, cents

    junoPanelL.setCellSize(86, 94);
    junoPanelR.setCellSize(86, 94);
    voicePanel.setCellSize(96, 94);
    // Smaller knob band on JUNO so every section fits with margin (no clipped readouts).
    junoPanelL.setControlHeight(84);
    junoPanelR.setControlHeight(84);
    voicePanel.setControlHeight(84);

    // --- DX7 (FM) operator controls, two columns: OP1|OP2 / OP3|OP4 / OP5|OP6.
    //     (Algorithm + feedback live in the tab's top row alongside the diagram.) -
    // DX7 1 — per-operator frequency + level, 3 columns of 2 ops (OP1/4, OP2/5, OP3/6).
    auto addOsc = [] (ControlPanel& p, int op)
    {
        p.addSection("OP " + juce::String(op));
        p.addChoice(params::id::fmOp(op, "fixed"), "Mode");    // Ratio / Fixed
        p.addKnob(params::id::fmOp(op, "coarse"), "Coarse");   // DX7 Coarse 0..31 (0 = 0.5x)
        p.addKnob(params::id::fmOp(op, "fine"),   "Fine");     // DX7 Fine 0..99
        p.addKnob(params::id::fmOp(op, "detune"), "Detune");   // DX7 Detune 0..14 (7 = centre)
        p.addKnob(params::id::fmOp(op, "outlvl"), "Level");    // DX7 Output Level 0..99
        p.addKnob(params::id::fmOp(op, "vel"),    "Vel");      // DX7 Key Velocity Sensitivity 0..7
    };
    addOsc(fmOscA, 1); addOsc(fmOscA, 4);
    addOsc(fmOscB, 2); addOsc(fmOscB, 5);
    addOsc(fmOscC, 3); addOsc(fmOscC, 6);
    for (auto* p : { &fmOscA, &fmOscB, &fmOscC }) p->setCellSize(96, 92);
    // DX7 2 / DX7 3 — per-operator DX7 4-rate / 4-level envelope, OP1-3 and OP4-6. Each
    // operator is one row: its viewer on the left, then the R/L knobs (single title).
    auto addEnv = [this] (ControlPanel& p, int op)
    {
        p.addSection("OP " + juce::String(op));
        p.addGraph(fmEnvGraph[op - 1]);   // viewer on the left of this operator's row
        for (int e = 1; e <= 4; ++e)
            p.addKnob(params::id::fmOp(op, ("r" + juce::String(e)).toRawUTF8()), "R" + juce::String(e));
        for (int e = 1; e <= 4; ++e)
            p.addKnob(params::id::fmOp(op, ("l" + juce::String(e)).toRawUTF8()), "L" + juce::String(e));
    };
    addEnv(fmEnv1Panel, 1); addEnv(fmEnv1Panel, 2); addEnv(fmEnv1Panel, 3);
    addEnv(fmEnv2Panel, 4); addEnv(fmEnv2Panel, 5); addEnv(fmEnv2Panel, 6);
    fmEnv1Panel.setCellSize(96, 96); fmEnv2Panel.setCellSize(96, 96);
    // DX7 4 — pitch & global mod, in design order: GLOBAL PITCH (transpose), then the
    // PITCH EG (with its own viewer), the LFO, and the per-op tremolo routing.
    // GLOBAL PITCH: whole-voice transpose (semitones; ratio operators shift, fixed stay).
    fmModPanel.addSection("GLOBAL PITCH");
    fmModPanel.addKnob(params::id::fmTranspose, "Transpose");
    fmModPanel.addSection("PITCH EG");
    fmModPanel.addGraph(fmPitchGraph);   // pitch-envelope viewer left of the knobs
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

    // --- FX-MASTER tab: two columns of effect cards. AMY's processing order is
    //     EQ -> Chorus -> Echo -> Reverb -> Synth Vol (inside AMY), then the host
    //     MASTER stage on the output: bitcrusher (Freq, Bit) -> WDF saturator (Drive).
    //     Out Gain lives in the header. Laid out as the design's grid:
    //       left  col: EQ, ECHO, BIT CRUSHER
    //       right col: CHORUS, REVERB, DISTORTION
    fxPanelL.setCellSize(84, 90);
    fxPanelL.addSection("EQ", col::junoBlue);
    fxPanelL.addKnob(params::id::eqLow, "Low");
    fxPanelL.addKnob(params::id::eqMid, "Mid");
    fxPanelL.addKnob(params::id::eqHigh, "High");
    fxPanelL.addSection("ECHO", col::filterViolet);
    fxPanelL.addKnob(params::id::echo, "Level");
    fxPanelL.addKnob(params::id::echoTime, "Time");
    fxPanelL.addKnob(params::id::echoFeedback, "F.back");
    fxPanelL.addKnob(params::id::echoTone, "Tone");
    fxPanelL.addSection("BIT CRUSHER", col::amber);
    fxPanelL.addKnob(params::id::bcFreq, "Freq");           // bitcrusher: downsample rate
    fxPanelL.addKnob(params::id::bcBits, "Bit");            // bitcrusher: bit depth

    fxPanelR.setCellSize(84, 90);
    fxPanelR.addSection("CHORUS", col::engineCyan);
    fxPanelR.addKnob(params::id::chorus, "Level");
    fxPanelR.addKnob(params::id::chorusRate, "Rate");
    fxPanelR.addKnob(params::id::chorusDepth, "Depth");
    fxPanelR.addSection("REVERB", col::lfoGreen);
    fxPanelR.addKnob(params::id::reverb, "Level");
    fxPanelR.addKnob(params::id::reverbSize, "Size");
    fxPanelR.addKnob(params::id::reverbDamping, "Damp");
    fxPanelR.addSection("DISTORTION", col::junoRed);
    fxPanelR.addKnob(params::id::clipDrive, "Drive");       // WDF diode saturator (analog warmth)
    fxPanelR.addKnob(params::id::masterVolume, "Synth Vol"); // AMY engine volume (upstream)

    // --- tabs -------------------------------------------------------------
    tabs.setOutline(0);
    tabs.setColour(juce::TabbedComponent::backgroundColourId, kPanel);
    tabs.setColour(juce::TabbedComponent::outlineColourId, col::hairline);
    tabs.setTabBarDepth(30);
    tabs.addTab("Juno",      kPanel, &junoPage, false);   // 2 columns + VOICE/title row
    tabs.addTab("DX7 1",     kPanel, &dx7Tab1,  false);   // algorithm + oscillators
    tabs.addTab("DX7 2",     kPanel, &dx7Tab2,  false);   // operator envelopes OP1-3
    tabs.addTab("DX7 3",     kPanel, &dx7Tab3,  false);   // operator envelopes OP4-6
    tabs.addTab("DX7 4",     kPanel, &dx7Tab4,  false);   // pitch EG + LFO + routing
    tabs.addTab("FX-MASTER", kPanel, &fxPage,   false);   // global FX + host master stage
    tabs.addTab("AMYboard",  kPanel, &hwPanel,  false);   // MIDI-out select + connect + send patch
    addAndMakeVisible(tabs);

    // Open on the tab matching the loaded engine, and seed lastTab so the first
    // timer tick doesn't read this as a user click (lastEngine stays -1 so the
    // initial dimming still applies).
    {
        int eng0 = 0;
        if (auto* e = s.getRawParameterValue(params::id::engine))
            eng0 = juce::jlimit(0, 2, (int) std::lround(e->load()));
        // Reopen on the AMYboard tab (6) if the session was in Hardware mode; else the
        // tab that matches the loaded engine (Analog->Juno 0, FM->DX7 1 = tab 1).
        const int tab0 = (proc.currentMode() == IAmyBackend::Kind::Hardware) ? 6
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

AmyPlugEditor::~AmyPlugEditor() { stopTimer(); setLookAndFeel(nullptr); }

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
            resized();   // re-flow the header to make room for the take-over button
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

        // A user tab click activates that tab's engine. Tabs: 0 Juno, 1-4 DX7,
        // 5 FX-MASTER, 6 AMYboard (FX/AMYboard don't change the engine).
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
            junoPage.setEnabled(analog); junoPage.setAlpha(analog ? 1.0f : 0.4f);
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

void AmyPlugEditor::selectTab(int index)
{
    tabs.setCurrentTabIndex(juce::jlimit(0, tabs.getNumTabs() - 1, index), false);
    lastTab = tabs.getCurrentTabIndex();
    resized();
}

void AmyPlugEditor::setEngineIndex(int idx)
{
    if (auto* e = proc.apvts().getParameter(params::id::engine))
        e->setValueNotifyingHost(e->convertTo0to1((float) juce::jlimit(0, 2, idx)));
}

void AmyPlugEditor::paint(juce::Graphics& g)
{
    // Shell gradient (neutral chrome).
    g.setGradientFill({ col::shellTop, 0.0f, 0.0f,
                        col::shellBottom, 0.0f, (float) getHeight(), false });
    g.fillAll();

    // Brand: "AMY" primary + "plug" neutral grey, one wordmark.
    auto logo = fonts::logo(30.0f);
    g.setFont(logo);
    const int lx = 16, ly = 12;
    const int amyW = (int) juce::GlyphArrangement::getStringWidth(logo, "AMY");
    g.setColour(col::textPrimary);
    g.drawText("AMY", lx, ly, amyW + 4, 30, juce::Justification::left);
    g.setColour(col::tabIndicator);
    g.drawText("plug", lx + amyW, ly, 120, 30, juce::Justification::left);

    // Subtitle.
    g.setColour(col::textFaint);
    g.setFont(fonts::label(11.0f).withExtraKerningFactor(0.04f));
    g.drawText(juce::String::fromUTF8("AMY FOR YOUR DAW · EDITOR V2"),
               lx, ly + 28, 320, 12, juce::Justification::left);
}

void AmyPlugEditor::resized()
{
    auto full = getLocalBounds().reduced(12);

    // --- header: brand | patch browser | OUT GAIN | status / engine·panic ---
    auto header = full.removeFromTop(62);
    header.removeFromLeft(140);                       // brand wordmark (painted in paint())

    // Right cluster: status (top row), engine + PANIC (bottom row).
    auto right = header.removeFromRight(230);
    {
        auto top = right.removeFromTop(26);
        if (takeoverButton.isVisible())
        {
            takeoverButton.setBounds(top.removeFromRight(118));
            top.removeFromRight(6);
        }
        engineStatusLabel.setBounds(top);
        right.removeFromTop(10);
        auto bot = right.removeFromTop(26);
        panicButton.setBounds(bot.removeFromRight(64));
        bot.removeFromRight(8);
        engineBox.setBounds(bot.removeFromRight(84));
        bot.removeFromRight(4);
        engineLabel.setBounds(bot.removeFromRight(46));
    }
    header.removeFromRight(14);

    // OUT GAIN rotary (spans both browser rows), with a divider to its left.
    auto gain = header.removeFromRight(60);
    outGainLabel.setBounds(gain.removeFromTop(14));
    outGainKnob.setBounds(gain.reduced(2, 0));
    header.removeFromRight(16);

    // Patch browser block, left-packed so the trailing buttons sit adjacent to their
    // neighbours (Import DX7 after ‹ ›, To Editor after Delete) — both the same width.
    const int kActionW = 96;
    auto row1 = header.removeFromTop(26);
    browserLabel.setBounds(row1.removeFromLeft(42));
    row1.removeFromLeft(4);
    patchBox.setBounds(row1.removeFromLeft(220));
    row1.removeFromLeft(6);
    prevButton.setBounds(row1.removeFromLeft(30));
    row1.removeFromLeft(3);
    nextButton.setBounds(row1.removeFromLeft(30));
    row1.removeFromLeft(8);
    importButton.setBounds(row1.removeFromLeft(kActionW));
    header.removeFromTop(10);
    auto row2 = header.removeFromTop(26);
    userLabel.setBounds(row2.removeFromLeft(42));
    row2.removeFromLeft(4);
    userBox.setBounds(row2.removeFromLeft(220));
    row2.removeFromLeft(6);
    saveButton.setBounds(row2.removeFromLeft(64));
    row2.removeFromLeft(4);
    deleteButton.setBounds(row2.removeFromLeft(62));
    row2.removeFromLeft(8);
    toEditorButton.setBounds(row2.removeFromLeft(kActionW));

    full.removeFromTop(10);
    auto r = full;

    // Tabs span the full width now that FX is its own tab. Each tab's page component
    // (JunoPage / TabPage / Dx7TabComponent / HardwarePanel) lays out its own content
    // to fill the body, so the section cards distribute the height evenly.
    tabs.setBounds(r);
}
} // namespace amyplug
