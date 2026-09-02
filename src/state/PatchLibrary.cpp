// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "PatchLibrary.h"
#include <algorithm>

namespace amyplug
{
PatchLibrary::PatchLibrary()
{
    dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
              .getChildFile("AMYplug").getChildFile("Patches");
    refresh();
}

void PatchLibrary::setDirectory(const juce::File& d)
{
    dir = d;
    refresh();
}

juce::File PatchLibrary::groupDir(const juce::String& group) const
{
    return group.isEmpty() ? dir : dir.getChildFile(juce::File::createLegalFileName(group));
}

juce::File PatchLibrary::fileFor(const juce::String& group, const juce::String& name) const
{
    return groupDir(group).getChildFile(juce::File::createLegalFileName(name) + kFileExt);
}

void PatchLibrary::refresh()
{
    cachedNames.clear();
    cachedEntries.clear();
    if (! dir.isDirectory())
        return;

    auto scan = [this] (const juce::File& d, const juce::String& group)
    {
        for (const auto& f : d.findChildFiles(juce::File::findFiles, false, juce::String("*") + kFileExt))
        {
            if (auto xml = juce::XmlDocument::parse(f))
            {
                auto tree = juce::ValueTree::fromXml(*xml);
                if (tree.hasType(kRootType))
                {
                    const juce::String name = tree.getProperty("name", f.getFileNameWithoutExtension()).toString();
                    cachedEntries.push_back({ group, name });
                    if (group.isEmpty()) cachedNames.add(name);
                }
            }
        }
    };

    scan(dir, {});                                  // the user's own patches (root)
    for (const auto& sub : dir.findChildFiles(juce::File::findDirectories, false))
        scan(sub, sub.getFileName());               // one group per subfolder

    cachedNames.sortNatural();
    // Sort: root group first, then groups alphabetically; names natural within a group.
    std::sort(cachedEntries.begin(), cachedEntries.end(), [] (const Entry& a, const Entry& b)
    {
        if (a.group != b.group)
        {
            if (a.group.isEmpty()) return true;
            if (b.group.isEmpty()) return false;
            return a.group.compareNatural(b.group) < 0;
        }
        return a.name.compareNatural(b.name) < 0;
    });
}

bool PatchLibrary::save(const juce::String& group, const juce::String& name, const PatchModel& model)
{
    if (name.trim().isEmpty())
        return false;
    const auto gdir = groupDir(group);
    if (! gdir.isDirectory() && ! gdir.createDirectory())
        return false;

    juce::ValueTree root { kRootType };
    root.setProperty("name", name, nullptr);
    root.appendChild(model.toValueTree(), nullptr);

    if (auto xml = root.createXml())
    {
        const bool ok = xml->writeTo(fileFor(group, name));
        refresh();
        return ok;
    }
    return false;
}

// Match by DISPLAY name within the group — the file name is a sanitised form of it, so
// the file has to be opened to know what it is called.
juce::File PatchLibrary::findPresetFile(const juce::String& group, const juce::String& name) const
{
    for (const auto& f : groupDir(group).findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles,
                                                        false, juce::String("*") + kFileExt))
        if (auto xml = juce::XmlDocument::parse(f))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            if (tree.hasType(kRootType) && tree.getProperty("name").toString() == name)
                return f;
        }
    return {};
}

bool PatchLibrary::load(const juce::String& group, const juce::String& name, PatchModel& model) const
{
    const auto f = findPresetFile(group, name);
    if (f == juce::File {}) return false;
    if (auto xml = juce::XmlDocument::parse(f))
    {
        auto patch = juce::ValueTree::fromXml(*xml).getChildWithName(PatchModel::kStateType);
        if (patch.isValid()) { model.fromValueTree(patch); return true; }
    }
    return false;
}

// "Empty" ignores hidden files: a .DS_Store must not keep a bank alive, and it goes to the
// trash with the folder either way, so nothing is destroyed.
void PatchLibrary::pruneIfEmpty(const juce::String& group)
{
    if (group.isEmpty()) return;                              // never prune the root
    const auto gdir = groupDir(group);
    if (gdir.isDirectory()
        && gdir.getNumberOfChildFiles(juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles) == 0)
        gdir.moveToTrash();
}

bool PatchLibrary::remove(const juce::String& group, const juce::String& name)
{
    const auto f = findPresetFile(group, name);
    if (f == juce::File {}) return false;
    const bool removed = f.moveToTrash();                     // recoverable, never unlink
    if (removed) pruneIfEmpty(group);
    refresh();
    return removed;
}

// --- banks --------------------------------------------------------------------------
juce::StringArray PatchLibrary::banks() const
{
    juce::StringArray out;
    if (dir.isDirectory())
        for (const auto& sub : dir.findChildFiles(juce::File::findDirectories | juce::File::ignoreHiddenFiles, false))
            out.add(sub.getFileName());
    out.sortNatural();
    return out;
}

bool PatchLibrary::createBank(const juce::String& bank)
{
    const auto legal = juce::File::createLegalFileName(bank.trim());
    if (legal.isEmpty()) return false;
    if (! dir.isDirectory() && ! dir.createDirectory()) return false;
    const bool ok = groupDir(legal).createDirectory();        // true if it already existed, too
    refresh();
    return ok;
}

bool PatchLibrary::renameBank(const juce::String& from, const juce::String& to)
{
    const auto legal = juce::File::createLegalFileName(to.trim());
    if (from.isEmpty() || legal.isEmpty() || legal == from) return false;
    const auto src = groupDir(from), dst = groupDir(legal);
    if (! src.isDirectory() || dst.exists()) return false;    // no source, or would clobber
    const bool ok = src.moveFileTo(dst);
    refresh();
    return ok;
}

bool PatchLibrary::deleteBank(const juce::String& bank)
{
    if (bank.isEmpty()) return false;                         // the root is not a bank
    const auto gdir = groupDir(bank);
    if (! gdir.isDirectory()) return false;
    const bool ok = gdir.moveToTrash();                       // contents and all — recoverable
    refresh();
    return ok;
}

bool PatchLibrary::movePreset(const juce::String& fromBank, const juce::String& name, const juce::String& toBank)
{
    if (fromBank == toBank) return false;
    const auto f = findPresetFile(fromBank, name);
    if (f == juce::File {}) return false;
    if (findPresetFile(toBank, name) != juce::File {}) return false;   // would clobber
    const auto ddir = groupDir(toBank);
    if (! ddir.isDirectory() && ! ddir.createDirectory()) return false;
    const bool ok = f.moveFileTo(fileFor(toBank, name));
    if (ok) pruneIfEmpty(fromBank);
    refresh();
    return ok;
}

bool PatchLibrary::renamePreset(const juce::String& bank, const juce::String& from, const juce::String& to)
{
    const auto newName = to.trim();
    if (newName.isEmpty() || newName == from) return false;
    const auto f = findPresetFile(bank, from);
    if (f == juce::File {}) return false;
    if (findPresetFile(bank, newName) != juce::File {}) return false;  // would clobber
    auto xml = juce::XmlDocument::parse(f);
    if (xml == nullptr) return false;
    auto tree = juce::ValueTree::fromXml(*xml);
    tree.setProperty("name", newName, nullptr);               // the display name lives INSIDE the file
    bool ok = false;
    if (auto out = tree.createXml())
        ok = out->writeTo(fileFor(bank, newName)) && f.moveToTrash();
    refresh();
    return ok;
}

// --- Root-group convenience overloads ----------------------------------------
bool PatchLibrary::save(const juce::String& name, const PatchModel& model) { return save(juce::String(), name, model); }
bool PatchLibrary::load(const juce::String& name, PatchModel& model) const  { return load(juce::String(), name, model); }
bool PatchLibrary::remove(const juce::String& name)                         { return remove(juce::String(), name); }
} // namespace amyplug
