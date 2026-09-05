// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "PresetsPage.h"
#include "AmyColours.h"
#include "AmyFonts.h"
#include "ScaledContent.h"
#include "Tooltips.h"
#include "../AmyPlugProcessor.h"
#include "../state/Parameters.h"

namespace amyplug
{
namespace col = amyplug::colours;

// ===========================================================================
// Tree items: FACTORY ▸ Juno/DX7/Piano/AMYboard, USER ▸ My Patches + each bank
// ===========================================================================
class PresetsPage::TreeItem final : public juce::TreeViewItem
{
public:
    TreeItem(PresetsPage& p, juce::String label, Filter f, bool header, int count)
        : page(p), text(std::move(label)), filter(std::move(f)), isHeader(header), n(count) {}

    bool mightContainSubItems() override { return isHeader; }
    bool canBeSelected() const override  { return ! isHeader; }
    int  getItemHeight() const override  { return 26; }
    juce::String getUniqueName() const override
    { return juce::String((int) filter.source) + "/" + filter.bank + "/" + text; }

    void paintItem(juce::Graphics& g, int w, int h) override
    {
        auto r = juce::Rectangle<int>(0, 0, w, h);
        if (isSelected())
        {
            g.setColour(col::engineCyan.withAlpha(0.18f));
            g.fillRoundedRectangle(r.toFloat().reduced(1.0f), 3.0f);
            g.setColour(col::engineCyan);
            g.fillRoundedRectangle(r.toFloat().withWidth(2.5f), 1.0f);
        }
        if (isHeader)
        {
            g.setColour(col::textDim);
            g.setFont(fonts::header(12.0f).withExtraKerningFactor(0.14f));
            g.drawText(text.toUpperCase(), r.reduced(6, 0), juce::Justification::centredLeft);
            return;
        }
        g.setColour(isSelected() ? col::textPrimary : col::textPrimary.withAlpha(0.85f));
        g.setFont(fonts::label(16.0f));
        auto row = r.reduced(8, 0);
        const juce::String countText = "(" + juce::String(n) + ")";
        g.setColour(col::textFaint);
        g.setFont(fonts::mono(11.0f));
        g.drawText(countText, row.removeFromRight(40), juce::Justification::centredRight);
        g.setColour(col::textPrimary);
        g.setFont(fonts::label(16.0f));
        g.drawFittedText(text, row, juce::Justification::centredLeft, 1);
    }

    void itemSelectionChanged(bool nowSelected) override
    {
        if (nowSelected) page.setFilter(filter);
    }

    void itemClicked(const juce::MouseEvent& e) override
    {
        if (isHeader || ! e.mods.isPopupMenu() || filter.source != PresetRef::Source::User
            || filter.bank == "*" || filter.bank.isEmpty())
            return;
        // A user BANK: rename / delete live on the thing itself, not in a menu elsewhere.
        juce::PopupMenu m;
        m.setLookAndFeel(&page.getLookAndFeel());
        m.addSectionHeader(text);
        m.addItem(1, "Rename Bank...");
        m.addItem(2, "Delete Bank...");
        // Parent to the scaled content (so the menu scales with the panel); anchor at the click.
        auto* parent = page.findParentComponentOfClass<ScaledContent>();
        const juce::String bank = filter.bank;
        m.showMenuAsync(juce::PopupMenu::Options().withParentComponent(parent)
                            .withTargetScreenArea(juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1)),
                        [this, bank] (int r)
                        {
                            if (r == 1) page.renameBank(bank);
                            if (r == 2) page.deleteBank(bank);
                        });
    }

private:
    PresetsPage& page;
    juce::String text;
    Filter       filter;
    bool         isHeader;
    int          n;
};

// ===========================================================================
// Page
// ===========================================================================
PresetsPage::PresetsPage(AmyPlugProcessor& processor, DialogHost& d, std::function<void()> importFn)
    : proc(processor), dialogs(d), importDx7(std::move(importFn))
{
    search.setTextToShowWhenEmpty("Search", col::textFaint);
    search.setFont(fonts::label(16.0f));
    search.setIndents(9, 3);
    search.onTextChange = [this] { rebuildRows(); };
    tips::applyDeep(search, tips::pSearch);
    addAndMakeVisible(search);

    tree.setRootItemVisible(false);
    tree.setDefaultOpenness(true);
    tree.setIndentSize(14);
    tree.setColour(juce::TreeView::backgroundColourId, col::panel);
    tips::applyDeep(tree, tips::pTree);
    addAndMakeVisible(tree);

    list.setRowHeight(26);
    list.setColour(juce::ListBox::backgroundColourId, col::panel);
    list.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    tips::applyDeep(list, tips::pList);
    addAndMakeVisible(list);

    for (auto* b : { &saveBtn, &saveAsBtn, &renameBtn, &moveBtn, &deleteBtn, &newBankBtn, &importBtn, &revealBtn })
        addAndMakeVisible(*b);
    saveBtn.onClick    = [this] { saveOverwrite(); };
    saveAsBtn.onClick  = [this] { saveAs(); };
    renameBtn.onClick  = [this] { renamePreset(); };
    moveBtn.onClick    = [this] { moveToBank(); };
    deleteBtn.onClick  = [this] { deletePreset(); };
    newBankBtn.onClick = [this] { newBank(); };
    importBtn.onClick  = [this] { if (importDx7) importDx7(); };
    revealBtn.onClick  = [this] { revealFolder(); };
    saveBtn.setTooltip(tips::pSave);
    saveAsBtn.setTooltip(tips::pSaveAs);
    renameBtn.setTooltip(tips::pRename);
    moveBtn.setTooltip(tips::pMove);
    deleteBtn.setTooltip(tips::pDelete);
    newBankBtn.setTooltip(tips::pNewBank);
    importBtn.setTooltip(tips::import);
    revealBtn.setTooltip(tips::pReveal);

    refresh();
    startTimerHz(10);
}

PresetsPage::~PresetsPage()
{
    stopTimer();
    tree.setRootItem(nullptr);
}

void PresetsPage::refresh()
{
    proc.patchLibrary().refresh();
    rebuildTree();
    rebuildRows();
    updateInfo();
}

void PresetsPage::rebuildTree()
{
    auto& lib = proc.patchLibrary();
    // Count per bank so a bank reads as "Juno (128)" and an empty one as "(0)".
    auto countUser = [&lib] (const juce::String& bank) {
        int n = 0; for (const auto& e : lib.entries()) if (e.group == bank) ++n; return n; };

    auto root    = std::make_unique<TreeItem>(*this, "root", Filter {}, true, 0);
    auto factory = new TreeItem(*this, "Factory", Filter {}, true, 0);
    factory->addSubItem(new TreeItem(*this, "Juno",     { PresetRef::Source::Factory, "Juno" },     false, 128));
    factory->addSubItem(new TreeItem(*this, "DX7",      { PresetRef::Source::Factory, "DX7" },      false, 128));
    factory->addSubItem(new TreeItem(*this, "Piano",    { PresetRef::Source::Factory, "Piano" },    false, 1));
    factory->addSubItem(new TreeItem(*this, "AMYboard", { PresetRef::Source::Factory, "AMYboard" }, false, kBuiltinPatchCount - 257));
    root->addSubItem(factory);

    auto user = new TreeItem(*this, "User", Filter {}, true, 0);
    user->addSubItem(new TreeItem(*this, "My Patches", { PresetRef::Source::User, "" }, false, countUser("")));
    for (const auto& b : lib.banks())                       // from the FOLDER tree: empty banks show
        user->addSubItem(new TreeItem(*this, b, { PresetRef::Source::User, b }, false, countUser(b)));
    root->addSubItem(user);

    tree.setRootItem(nullptr);
    treeRoot = std::move(root);
    tree.setRootItem(treeRoot.get());
    factory->setOpen(true);
    user->setOpen(true);
}

void PresetsPage::setFilter(Filter f)
{
    filter = std::move(f);
    rebuildRows();
}

void PresetsPage::rebuildRows()
{
    rows.clear();
    const auto catalog = PresetCatalog::build(proc.patchLibrary());
    const auto needle  = search.getText().trim();
    for (const auto& ref : catalog.items)
    {
        if (filter.source != PresetRef::Source::None)
        {
            if (ref.source != filter.source) continue;
            if (filter.bank != "*" && ref.bank != filter.bank) continue;
        }
        if (needle.isNotEmpty() && ! ref.name.containsIgnoreCase(needle) && ! ref.bank.containsIgnoreCase(needle))
            continue;
        rows.push_back(ref);
    }
    list.updateContent();
    // Keep the selection on the loaded preset if it is in view.
    const auto loaded = proc.loadedPreset();
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i] == loaded) { list.selectRow((int) i, true, true); break; }
    list.repaint();
}

void PresetsPage::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& ref   = rows[(size_t) row];
    const bool loaded = (ref == proc.loadedPreset());
    auto r = juce::Rectangle<int>(0, 0, w, h);

    if (selected)
    {
        g.setColour(col::engineCyan.withAlpha(0.18f));
        g.fillRoundedRectangle(r.toFloat().reduced(2.0f, 1.0f), 3.0f);
    }
    if (loaded)
    {
        g.setColour(col::engineCyan);
        g.fillRoundedRectangle(r.toFloat().withWidth(2.5f).reduced(0.0f, 4.0f), 1.0f);
    }
    auto row_ = r.reduced(12, 0);
    // Bank on the right when the filter spans banks, so "A" in two banks stays distinct.
    if (filter.bank == "*" || filter.source == PresetRef::Source::None)
    {
        g.setColour(col::textFaint);
        g.setFont(fonts::mono(11.0f));
        g.drawText(ref.bank.isEmpty() ? juce::String("My Patches") : ref.bank,
                   row_.removeFromRight(110), juce::Justification::centredRight);
    }
    g.setColour(loaded ? col::textPrimary : col::textPrimary.withAlpha(0.82f));
    g.setFont(fonts::label(16.0f));
    juce::String text = ref.name;
    if (loaded && proc.isPresetDirty()) text += " *";
    g.drawFittedText(text, row_, juce::Justification::centredLeft, 1);
}

void PresetsPage::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) rows.size()) return;
    selected = rows[(size_t) row];
    loadRow(row);                           // a click loads, as in every serious browser
}

void PresetsPage::listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) { listBoxItemClicked(row, e); }

void PresetsPage::loadRow(int row)
{
    const auto& ref = rows[(size_t) row];
    if (ref.isFactory())
    {
        // Selecting a factory patch: engine -> Factory, then the patch number. The
        // processor treats the patchA move as the load (identity + clean mark).
        auto set = [this] (const char* id, float v) {
            if (auto* p = proc.apvts().getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
        set(params::id::engine, 0.0f);
        set(params::id::patchA, (float) ref.factoryIndex);
    }
    else
    {
        proc.loadUserPatch(ref.bank, ref.name);
    }
    updateInfo();
    list.repaint();
}

void PresetsPage::timerCallback()
{
    const auto loaded = proc.loadedPreset();
    const bool dirty  = proc.isPresetDirty();
    if (loaded != lastLoadedShown || dirty != lastDirtyShown)
    {
        lastLoadedShown = loaded;
        lastDirtyShown  = dirty;
        updateInfo();
        list.repaint();
    }
}

void PresetsPage::updateInfo()
{
    const auto ref = proc.loadedPreset();
    if (ref.isFactory())
        selectedEngine = PresetCatalog::factoryEngineName(ref.factoryIndex);
    else if (ref.isUser())
    {
        PatchModel m;
        if (proc.patchLibrary().load(ref.bank, ref.name, m) && ! m.synths.empty())
            selectedEngine = m.synths[0].engine == PatchModel::Engine::FM     ? "FM (DX7)"
                           : m.synths[0].engine == PatchModel::Engine::Analog ? "Analog (Juno-6)"
                           : juce::String("Factory patch ") + juce::String(m.synths[0].patchNumber);
        else selectedEngine = "(file not found on this machine)";
    }
    else selectedEngine.clear();

    // Save overwrites only a user preset; the rest need a user preset to act on.
    const bool user = ref.isUser();
    saveBtn.setEnabled(user);
    renameBtn.setEnabled(user);
    moveBtn.setEnabled(user);
    deleteBtn.setEnabled(user);
    repaint();
}

// ---------------------------------------------------------------------------
// Layout + paint
// ---------------------------------------------------------------------------
void PresetsPage::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto left  = r.removeFromLeft(250);
    r.removeFromLeft(10);
    auto right = r.removeFromRight(280);
    r.removeFromRight(10);

    search.setBounds(left.removeFromTop(28));
    left.removeFromTop(8);
    tree.setBounds(left);

    list.setBounds(r);

    // Right column: info card on top, actions stacked below it.
    auto actions = right.removeFromBottom(8 * 30 + 7 * 6 + 12);
    actions.removeFromTop(12);
    for (auto* b : { &saveBtn, &saveAsBtn, &renameBtn, &moveBtn, &deleteBtn, &newBankBtn, &importBtn, &revealBtn })
    {
        b->setBounds(actions.removeFromTop(30));
        actions.removeFromTop(6);
    }
}

void PresetsPage::paint(juce::Graphics& g)
{
    // Three cards, matching the section cards elsewhere.
    auto r = getLocalBounds().reduced(8);
    auto left  = r.removeFromLeft(250); r.removeFromLeft(10);
    auto right = r.removeFromRight(280); r.removeFromRight(10);
    for (auto box : { left.withTrimmedTop(36), r, right })
    {
        g.setColour(col::panel);
        g.fillRoundedRectangle(box.toFloat(), 6.0f);
        g.setColour(col::hairline);
        g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 6.0f, 1.0f);
    }

    // PRESET INFO
    auto info = right.withTrimmedBottom(8 * 30 + 7 * 6 + 12).reduced(14, 12);
    g.setColour(col::textDim);
    g.setFont(fonts::header(12.0f).withExtraKerningFactor(0.16f));
    g.drawText("PRESET INFO", info.removeFromTop(18), juce::Justification::centredLeft);
    info.removeFromTop(10);

    const auto ref   = proc.loadedPreset();
    const bool dirty = proc.isPresetDirty();
    g.setColour(col::textPrimary);
    g.setFont(fonts::header(20.0f));
    g.drawFittedText(ref.isNone() ? "-" : ref.name + (dirty ? " *" : ""), info.removeFromTop(26),
                     juce::Justification::centredLeft, 1);
    info.removeFromTop(4);

    auto line = [&] (const juce::String& k, const juce::String& v)
    {
        auto row = info.removeFromTop(20);
        g.setColour(col::textFaint);
        g.setFont(fonts::mono(12.0f));
        g.drawText(k, row.removeFromLeft(64), juce::Justification::centredLeft);
        g.setColour(col::textDim);
        g.setFont(fonts::label(15.0f));
        g.drawFittedText(v, row, juce::Justification::centredLeft, 1);
    };
    line("status", ref.isNone() ? "-" : dirty ? "Modified" : "Loaded");
    line("bank",   ref.isNone() ? "-" : ref.isUser() && ref.bank.isEmpty() ? "My Patches" : ref.bank);
    line("source", ref.isFactory() ? "Factory" : ref.isUser() ? "User" : "-");
    line("engine", selectedEngine.isEmpty() ? "-" : selectedEngine);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void PresetsPage::addBankBox(juce::AlertWindow& w, const juce::StringArray& banks, const juce::String& current)
{
    juce::StringArray items { kNoBank };
    items.addArray(banks);
    w.addComboBox("bank", items, "BANK");
    if (auto* box = w.getComboBoxComponent("bank"))
    {
        box->setEditableText(true);                       // a list you can also type into
        box->setText(current.isEmpty() ? juce::String(kNoBank) : current, juce::dontSendNotification);
    }
}

juce::String PresetsPage::bankFromBox(juce::AlertWindow& w)
{
    if (auto* box = w.getComboBoxComponent("bank"))
    {
        const auto t = box->getText().trim();
        return (t == kNoBank) ? juce::String() : t;
    }
    return {};
}

void PresetsPage::saveOverwrite()
{
    const auto ref = proc.loadedPreset();
    if (! ref.isUser()) { saveAs(); return; }
    proc.saveUserPatch(ref.bank, ref.name);
    refresh();
}

void PresetsPage::saveAs()
{
    const auto ref = proc.loadedPreset();
    auto* w = dialogs.beginDialog("Save Preset", "", juce::MessageBoxIconType::NoIcon);
    addBankBox(*w, proc.patchLibrary().banks(), ref.isUser() ? ref.bank : juce::String());
    w->addTextEditor("name", ref.isNone() ? juce::String("My Patch") : ref.name, "NAME");
    w->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialogs.showDialog([this] (juce::AlertWindow& dlg, int r)
    {
        if (r != 1) return;
        const auto name = dlg.getTextEditorContents("name").trim();
        if (name.isEmpty()) return;
        proc.saveUserPatch(bankFromBox(dlg), name);
        refresh();
    });
    if (auto* ed = w->getTextEditor("name")) ed->grabKeyboardFocus();
}

void PresetsPage::renamePreset()
{
    const auto ref = proc.loadedPreset();
    if (! ref.isUser()) return;
    auto* w = dialogs.beginDialog("Rename Preset", "", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", ref.name, "NAME");
    w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialogs.showDialog([this, ref] (juce::AlertWindow& dlg, int r)
    {
        if (r != 1) return;
        const auto name = dlg.getTextEditorContents("name").trim();
        if (proc.patchLibrary().renamePreset(ref.bank, ref.name, name))
            proc.relabelLoadedPreset(ref.bank, name);
        refresh();
    });
    if (auto* ed = w->getTextEditor("name")) ed->grabKeyboardFocus();
}

void PresetsPage::moveToBank()
{
    const auto ref = proc.loadedPreset();
    if (! ref.isUser()) return;
    auto* w = dialogs.beginDialog("Move to Bank", ref.name, juce::MessageBoxIconType::NoIcon);
    addBankBox(*w, proc.patchLibrary().banks(), ref.bank);
    w->addButton("Move",   1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialogs.showDialog([this, ref] (juce::AlertWindow& dlg, int r)
    {
        if (r != 1) return;
        const auto bank = bankFromBox(dlg);
        if (proc.patchLibrary().movePreset(ref.bank, ref.name, bank))
            proc.relabelLoadedPreset(bank, ref.name);
        refresh();
    });
}

void PresetsPage::deletePreset()
{
    const auto ref = proc.loadedPreset();
    if (! ref.isUser()) return;
    // Explicit IDs: 1 is the ONLY path to the delete; Escape, the editor closing, and a
    // stray Return all yield 0. Never a platform button index (JUCE-UI-LnF__07, pitfall 7).
    auto* w = dialogs.beginDialog("Delete Preset",
                                  "Delete \"" + ref.name + "\" from "
                                      + (ref.bank.isEmpty() ? juce::String("My Patches") : ref.bank)
                                      + "?\nIt goes to the Trash.",
                                  juce::MessageBoxIconType::WarningIcon);
    w->addButton("Delete", 1);
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialogs.showDialog([this, ref] (juce::AlertWindow&, int r)
    {
        if (r != 1) return;
        proc.patchLibrary().remove(ref.bank, ref.name);   // by name, not by a captured index
        refresh();
    });
}

void PresetsPage::newBank()
{
    auto* w = dialogs.beginDialog("New Bank", "", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", "", "BANK NAME");
    w->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialogs.showDialog([this] (juce::AlertWindow& dlg, int r)
    {
        if (r != 1) return;
        proc.patchLibrary().createBank(dlg.getTextEditorContents("name"));
        refresh();
    });
    if (auto* ed = w->getTextEditor("name")) ed->grabKeyboardFocus();
}

void PresetsPage::renameBank(const juce::String& bank)
{
    auto* w = dialogs.beginDialog("Rename Bank", bank, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", bank, "NEW NAME");
    w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialogs.showDialog([this, bank] (juce::AlertWindow& dlg, int r)
    {
        if (r != 1) return;
        const auto to = dlg.getTextEditorContents("name").trim();
        if (proc.patchLibrary().renameBank(bank, to))
        {
            const auto ref = proc.loadedPreset();
            if (ref.isUser() && ref.bank == bank) proc.relabelLoadedPreset(to, ref.name);
        }
        refresh();
    });
    if (auto* ed = w->getTextEditor("name")) ed->grabKeyboardFocus();
}

void PresetsPage::deleteBank(const juce::String& bank)
{
    int n = 0;
    for (const auto& e : proc.patchLibrary().entries()) if (e.group == bank) ++n;
    auto* w = dialogs.beginDialog("Delete Bank",
                                  "Delete the bank \"" + bank + "\" and the " + juce::String(n)
                                      + (n == 1 ? " preset" : " presets") + " in it?\nThey go to the Trash.",
                                  juce::MessageBoxIconType::WarningIcon);
    w->addButton("Delete", 1);
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialogs.showDialog([this, bank] (juce::AlertWindow&, int r)
    {
        if (r != 1) return;
        proc.patchLibrary().deleteBank(bank);
        refresh();
    });
}

void PresetsPage::revealFolder()
{
    auto d = proc.patchLibrary().directory();
    if (! d.isDirectory()) d.createDirectory();
    d.revealToUser();
}
} // namespace amyplug
