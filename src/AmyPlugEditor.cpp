// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugEditor.h"
#include "state/Parameters.h"
#include "BuiltinPatchNames.h"

namespace amyplug
{
namespace
{
const char* bankOf(int patchNumber)
{
    if (patchNumber <= 127) return "Juno";
    if (patchNumber <= 255) return "DX7";
    if (patchNumber == 256) return "Piano";
    return "AMYboard";
}
} // namespace

AmyPlugEditor::AmyPlugEditor(AmyPlugProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p)
{
    auto& s = proc.apvts();

    modeBox.addItemList({ "Software", "Hardware" }, 1);
    addAndMakeVisible(modeBox);
    modeAtt = std::make_unique<Apvts::ComboBoxAttachment>(s, params::id::mode, modeBox);

    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    panicButton.onClick = [this] { proc.requestPanic(); };
    addAndMakeVisible(panicButton);

    // --- Patch browser ----------------------------------------------------
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
        if (id > 0) selectPatch(id - 1);
    };
    addAndMakeVisible(patchBox);

    prevButton.onClick = [this] { stepPatch(-1); };
    nextButton.onClick = [this] { stepPatch(+1); };
    addAndMakeVisible(prevButton);
    addAndMakeVisible(nextButton);

    // --- User patches -----------------------------------------------------
    userBox.setTextWhenNothingSelected("User patches");
    userBox.onChange = [this]
    {
        const auto name = userBox.getText();
        if (name.isNotEmpty()) proc.loadUserPatch(name);
    };
    addAndMakeVisible(userBox);
    refreshUserBox();

    saveButton.onClick   = [this] { showSaveDialog(); };
    deleteButton.onClick = [this]
    {
        const auto name = userBox.getText();
        if (name.isNotEmpty()) { proc.patchLibrary().remove(name); refreshUserBox(); }
    };
    addAndMakeVisible(saveButton);
    addAndMakeVisible(deleteButton);

    // --- Macro knobs ------------------------------------------------------
    addKnob(params::id::filterCutoff, "Cutoff");
    addKnob(params::id::filterReso,   "Reso");
    addKnob(params::id::masterVolume, "Volume");
    addKnob(params::id::ampAttack,    "Attack");
    addKnob(params::id::ampDecay,     "Decay");
    addKnob(params::id::ampSustain,   "Sustain");
    addKnob(params::id::ampRelease,   "Release");
    addKnob(params::id::reverb,       "Reverb");
    addKnob(params::id::chorus,       "Chorus");
    addKnob(params::id::echo,         "Echo");
    addKnob(params::id::numVoices,    "Voices");
    addKnob(params::id::pitchBendRange,"Bend");

    setSize(640, 440);
    startTimerHz(20);
}

AmyPlugEditor::~AmyPlugEditor() { stopTimer(); }

AmyPlugEditor::Knob& AmyPlugEditor::addKnob(const juce::String& paramId, const juce::String& text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible(k->slider);

    k->label.setText(text, juce::dontSendNotification);
    k->label.setJustificationType(juce::Justification::centred);
    k->label.setFont(juce::FontOptions(11.0f));
    k->label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(k->label);

    k->attachment = std::make_unique<Apvts::SliderAttachment>(proc.apvts(), paramId, k->slider);
    knobs.push_back(std::move(k));
    return *knobs.back();
}

void AmyPlugEditor::buildPatchBox()
{
    patchBox.clear(juce::dontSendNotification);
    juce::String currentBank;
    for (int i = 0; i < kBuiltinPatchCount; ++i)
    {
        const juce::String bank = bankOf(i);
        if (bank != currentBank) { patchBox.addSectionHeading(bank); currentBank = bank; }
        patchBox.addItem(kBuiltinPatchNames[i], i + 1);   // id = patchNumber + 1
    }
}

void AmyPlugEditor::refreshUserBox()
{
    const auto sel = userBox.getText();
    userBox.clear(juce::dontSendNotification);
    int id = 1;
    for (const auto& name : proc.patchLibrary().names())
        userBox.addItem(name, id++);
    if (sel.isNotEmpty())
        userBox.setText(sel, juce::dontSendNotification);
}

void AmyPlugEditor::selectPatch(int patchNumber)
{
    if (auto* param = proc.apvts().getParameter(params::id::patchA))
        param->setValueNotifyingHost(param->convertTo0to1((float) patchNumber));
}

void AmyPlugEditor::stepPatch(int delta)
{
    int current = lastPatch >= 0 ? lastPatch : 0;
    selectPatch(juce::jlimit(0, kBuiltinPatchCount - 1, current + delta));
}

void AmyPlugEditor::showSaveDialog()
{
    auto* w = new juce::AlertWindow("Save User Patch", "Patch name:",
                                    juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", "My Patch");
    w->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w] (int result)
    {
        if (result == 1)
        {
            const auto name = w->getTextEditorContents("name").trim();
            if (name.isNotEmpty())
            {
                proc.saveUserPatch(name);
                refreshUserBox();
                userBox.setText(name, juce::dontSendNotification);
            }
        }
    }), true);   // deleteWhenDismissed
}

void AmyPlugEditor::timerCallback()
{
    // Reflect external patchA changes (automation, preset load) into the browser.
    if (auto* raw = proc.apvts().getRawParameterValue(params::id::patchA))
    {
        const int n = juce::jlimit(0, kBuiltinPatchCount - 1, (int) std::lround(raw->load()));
        if (n != lastPatch)
        {
            lastPatch = n;
            patchBox.setSelectedId(n + 1, juce::dontSendNotification);
        }
    }
}

void AmyPlugEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e2327));   // shore-pine dark
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("AMYplug", 16, 10, 200, 26, juce::Justification::left);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(juce::String::fromUTF8("AMY for your DAW · patch editor v1"),
               16, 34, 300, 16, juce::Justification::left);

    // Panel divider above the knobs.
    g.setColour(juce::Colour(0xff2c343a));
    g.fillRect(16, 128, getWidth() - 32, 1);
}

void AmyPlugEditor::resized()
{
    auto r = getLocalBounds().reduced(16);

    // Header row: mode + panic on the right.
    auto header = r.removeFromTop(40);
    panicButton.setBounds(header.removeFromRight(80).reduced(0, 6));
    header.removeFromRight(8);
    modeBox.setBounds(header.removeFromRight(120).reduced(0, 8));

    r.removeFromTop(8);

    // Patch browser row.
    auto browse = r.removeFromTop(26);
    browserLabel.setBounds(browse.removeFromLeft(44));
    nextButton.setBounds(browse.removeFromRight(28));
    prevButton.setBounds(browse.removeFromRight(28));
    browse.removeFromRight(4);
    patchBox.setBounds(browse);

    r.removeFromTop(6);

    // User patch row.
    auto user = r.removeFromTop(26);
    userLabel.setBounds(user.removeFromLeft(44));
    deleteButton.setBounds(user.removeFromRight(70));
    saveButton.setBounds(user.removeFromRight(72));
    user.removeFromRight(6);
    userBox.setBounds(user);

    r.removeFromTop(16);   // divider gap (painted at y=128)

    // Knob grid: 4 columns.
    const int cols = 4;
    const int cellW = r.getWidth() / cols;
    const int cellH = 96;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int col = (int) i % cols;
        const int row = (int) i / cols;
        juce::Rectangle<int> cell (r.getX() + col * cellW, r.getY() + row * cellH, cellW, cellH);
        knobs[i]->label.setBounds(cell.removeFromTop(16));
        knobs[i]->slider.setBounds(cell.reduced(6, 0));
    }
}
} // namespace amyplug
