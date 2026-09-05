// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../state/PresetRef.h"
#include "../state/PresetCatalog.h"
#include <functional>
#include <memory>
#include <vector>

namespace amyplug
{
class AmyPlugProcessor;

// Who owns the modal prompts. The editor does — its dialogs are parented into the scaled
// content and share its lifetime — so the page asks it rather than raising its own.
struct DialogHost
{
    virtual ~DialogHost() = default;
    virtual juce::AlertWindow* beginDialog(const juce::String& title, const juce::String& message,
                                           juce::MessageBoxIconType icon) = 0;
    virtual void showDialog(std::function<void (juce::AlertWindow&, int)> onResult) = 0;
    // Run `proceed` unless the loaded preset has unsaved edits, in which case ask first.
    // Both routes into a preset load go through this — the header's arrows and menu, and
    // the list here — so neither can forget it.
    virtual void confirmDiscardIfDirty(std::function<void()> proceed) = 0;
};

// The PRESETS tab: a directory tree on the left (FACTORY banks, USER banks), the
// preset list in the middle, and the loaded preset's details plus every library action
// on the right. Click a row to load it. Everything a bank can do to a preset — save,
// rename, move, delete, new bank — lives here, so no convention has to be discovered.
class PresetsPage final : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::Timer
{
public:
    PresetsPage(AmyPlugProcessor& processor, DialogHost& dialogs, std::function<void()> importDx7);
    ~PresetsPage() override;

    void refresh();                          // rescan the library, rebuild tree + list
    void resized() override;
    void paint(juce::Graphics&) override;

    // The tree's filter. bank == "*" means "every bank of that source".
    struct Filter { PresetRef::Source source = PresetRef::Source::None; juce::String bank = "*"; };

    // Shared with the header's SAVE button and the DX7 import prompt.
    void saveAs();
    // A BANK chooser: an editable ComboBox — the only stock control that is both a list
    // of what exists and a place to type something new (JUCE-UI-LnF__14 §7).
    static void addBankBox(juce::AlertWindow&, const juce::StringArray& banks, const juce::String& current);
    static juce::String bankFromBox(juce::AlertWindow&);

private:
    // ListBoxModel
    int  getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void timerCallback() override;           // mirror the processor's identity + dirty mark

    void rebuildTree();
    void rebuildRows();
    void setFilter(Filter f);
    void loadRow(int row);          // asks first if the loaded preset has unsaved edits
    void loadRowNow(const PresetRef&);
    void updateInfo();

    // Actions (right column). Every destructive one confirms with explicit button IDs.
    void saveOverwrite();
    void renamePreset();
    void moveToBank();
    void deletePreset();
    void newBank();
    void renameBank(const juce::String& bank);
    void deleteBank(const juce::String& bank);
    void revealFolder();

    static constexpr const char* kNoBank = "(no bank)";

    class TreeItem;
    friend class TreeItem;

    AmyPlugProcessor&      proc;
    DialogHost&            dialogs;
    std::function<void()>  importDx7;

    juce::TextEditor       search;
    juce::TreeView         tree;
    std::unique_ptr<TreeItem> treeRoot;
    juce::ListBox          list { "presets", this };
    Filter                 filter;
    std::vector<PresetRef> rows;             // what the list shows (filter + search applied)
    PresetRef              selected;         // the row the user clicked (may differ from loaded)
    PresetRef              lastLoadedShown;  // to repaint only when the identity moves
    bool                   lastDirtyShown = false;
    juce::String           selectedEngine;   // info panel: "FM (DX7)" etc.

    juce::TextButton saveBtn     { "Save" };
    juce::TextButton saveAsBtn   { "Save As..." };
    juce::TextButton renameBtn   { "Rename..." };
    juce::TextButton moveBtn     { "Move to Bank..." };
    juce::TextButton deleteBtn   { "Delete" };
    juce::TextButton newBankBtn  { "New Bank..." };
    juce::TextButton importBtn   { "Import DX7..." };
    juce::TextButton revealBtn   { "Reveal Folder" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetsPage)
};
} // namespace amyplug
