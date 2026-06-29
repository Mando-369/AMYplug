// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugEditor.h"
#include "state/Parameters.h"
#include "BuiltinPatchNames.h"

namespace amyplug
{
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

void ControlPanel::paint(juce::Graphics& g)
{
    const int titleH = 18, rowH = 84;
    auto r = getLocalBounds().reduced(4);
    for (int sec = 0; sec < sectionTitles.size(); ++sec)
    {
        auto box = r.removeFromTop(titleH + rowH);
        g.setColour(kPanel);   g.fillRoundedRectangle(box.toFloat(), 4.0f);
        auto tb = box.removeFromTop(titleH);
        g.setColour(kTitle);   g.fillRoundedRectangle(tb.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(sectionTitles[sec], tb, juce::Justification::centred);
        r.removeFromTop(6);
    }
}

void ControlPanel::resized()
{
    const int titleH = 18, rowH = 84, cellW = 70;
    auto r = getLocalBounds().reduced(4);
    for (int sec = 0; sec < sectionTitles.size(); ++sec)
    {
        auto box = r.removeFromTop(titleH + rowH);
        box.removeFromTop(titleH);
        int x = box.getX() + 4;
        for (auto& c : controls)
        {
            if (c->section != sec) continue;
            juce::Rectangle<int> cell(x, box.getY() + 2, cellW, rowH - 4);
            c->label.setBounds(cell.removeFromTop(15));
            if (c->combo) c->combo->setBounds(cell.removeFromTop(26).reduced(2, 1));
            else if (c->knob) c->knob->setBounds(cell.reduced(3, 0));
            x += cellW;
        }
        r.removeFromTop(6);
    }
}

void PlaceholderPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(15.0f));
    g.drawFittedText(text, getLocalBounds().reduced(20), juce::Justification::centred, 3);
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
            if (auto* e = proc.apvts().getParameter(params::id::engine))
                e->setValueNotifyingHost(0.0f);   // selecting a factory preset
            selectPatch(id - 1);
        }
    };
    addAndMakeVisible(patchBox);
    prevButton.onClick = [this] { stepPatch(-1); };
    nextButton.onClick = [this] { stepPatch(+1); };
    addAndMakeVisible(prevButton); addAndMakeVisible(nextButton);

    userBox.setTextWhenNothingSelected("User patches");
    userBox.onChange = [this] { const auto n = userBox.getText(); if (n.isNotEmpty()) proc.loadUserPatch(n); };
    addAndMakeVisible(userBox); refreshUserBox();
    saveButton.onClick   = [this] { showSaveDialog(); };
    deleteButton.onClick = [this] { const auto n = userBox.getText(); if (n.isNotEmpty()) { proc.patchLibrary().remove(n); refreshUserBox(); } };
    addAndMakeVisible(saveButton); addAndMakeVisible(deleteButton);

    engineAtt = std::make_unique<Apvts::ButtonAttachment>(s, params::id::engine, engineToggle);
    engineToggle.setTooltip("Switch synth 1 to the editable Analog engine (Juno tab)");
    addAndMakeVisible(engineToggle);

    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    panicButton.onClick = [this] { proc.requestPanic(); };
    addAndMakeVisible(panicButton);

    // --- Juno tab panel ---------------------------------------------------
    junoPanel.addSection("OSC A");
    junoPanel.addChoice(params::id::oscAWave, "Wave");
    junoPanel.addKnob(params::id::oscADuty, "Duty");
    junoPanel.addKnob(params::id::oscALevel, "Level");
    junoPanel.addSection("OSC B");
    junoPanel.addChoice(params::id::oscBWave, "Wave");
    junoPanel.addKnob(params::id::oscBDuty, "Duty");
    junoPanel.addKnob(params::id::oscBLevel, "Level");
    junoPanel.addSection("LFO");
    junoPanel.addChoice(params::id::lfoWave, "Wave");
    junoPanel.addKnob(params::id::lfoFreq, "Freq");
    junoPanel.addKnob(params::id::lfoToPitch, "Pitch");
    junoPanel.addKnob(params::id::lfoToPwm, "PWM");
    junoPanel.addKnob(params::id::lfoToFilter, "Filter");
    junoPanel.addSection("VCF");
    junoPanel.addKnob(params::id::filterCutoff, "Freq");
    junoPanel.addKnob(params::id::filterReso, "Reso");
    junoPanel.addKnob(params::id::vcfKbd, "Kbd");
    junoPanel.addKnob(params::id::vcfEnv, "Env");
    junoPanel.addChoice(params::id::vcfType, "Type");
    junoPanel.addSection("VCF ENV");
    junoPanel.addKnob(params::id::vcfAttack, "A");
    junoPanel.addKnob(params::id::vcfDecay, "D");
    junoPanel.addKnob(params::id::vcfSustain, "S");
    junoPanel.addKnob(params::id::vcfRelease, "R");
    junoPanel.addSection("AMP ENV");
    junoPanel.addKnob(params::id::ampAttack, "A");
    junoPanel.addKnob(params::id::ampDecay, "D");
    junoPanel.addKnob(params::id::ampSustain, "S");
    junoPanel.addKnob(params::id::ampRelease, "R");

    // --- global FX rack ---------------------------------------------------
    fxPanel.addSection("EQ");
    fxPanel.addKnob(params::id::eqLow, "Low");
    fxPanel.addKnob(params::id::eqMid, "Mid");
    fxPanel.addKnob(params::id::eqHigh, "High");
    fxPanel.addSection("CHORUS"); fxPanel.addKnob(params::id::chorus, "Level");
    fxPanel.addSection("REVERB"); fxPanel.addKnob(params::id::reverb, "Level");
    fxPanel.addSection("ECHO");   fxPanel.addKnob(params::id::echo, "Level");
    addAndMakeVisible(fxPanel);

    // --- tabs -------------------------------------------------------------
    tabs.setOutline(0);
    tabs.addTab("Juno",     kPanel, &junoPanel, false);
    tabs.addTab("DX7",      kPanel, new PlaceholderPanel("DX7 FM operator editor\n(coming in M3c)"), true);
    tabs.addTab("AMYboard", kPanel, new PlaceholderPanel("Hardware control\n(coming in M4)"), true);
    addAndMakeVisible(tabs);

    setSize(720, 560);
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
    const auto sel = userBox.getText();
    userBox.clear(juce::dontSendNotification);
    int id = 1;
    for (const auto& name : proc.patchLibrary().names()) userBox.addItem(name, id++);
    if (sel.isNotEmpty()) userBox.setText(sel, juce::dontSendNotification);
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

void AmyPlugEditor::timerCallback()
{
    if (auto* raw = proc.apvts().getRawParameterValue(params::id::patchA))
    {
        const int n = juce::jlimit(0, kBuiltinPatchCount - 1, (int) std::lround(raw->load()));
        if (n != lastPatch) { lastPatch = n; patchBox.setSelectedId(n + 1, juce::dontSendNotification); }
    }
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
    auto r = getLocalBounds().reduced(12);
    r.removeFromTop(34);   // title

    // Top bar: two rows (patch browser, user + engine + panic).
    auto row1 = r.removeFromTop(26);
    browserLabel.setBounds(row1.removeFromLeft(40));
    nextButton.setBounds(row1.removeFromRight(28));
    prevButton.setBounds(row1.removeFromRight(28));
    row1.removeFromRight(4);
    patchBox.setBounds(row1);
    r.removeFromTop(6);

    auto row2 = r.removeFromTop(26);
    userLabel.setBounds(row2.removeFromLeft(40));
    panicButton.setBounds(row2.removeFromRight(72));
    row2.removeFromRight(8);
    engineToggle.setBounds(row2.removeFromRight(80));
    deleteButton.setBounds(row2.removeFromRight(64));
    saveButton.setBounds(row2.removeFromRight(66));
    row2.removeFromRight(6);
    userBox.setBounds(row2);
    r.removeFromTop(10);

    // Tabs (left) + FX rack (right).
    auto fx = r.removeFromRight(180);
    fxPanel.setBounds(fx);
    r.removeFromRight(8);
    tabs.setBounds(r);
}
} // namespace amyplug
