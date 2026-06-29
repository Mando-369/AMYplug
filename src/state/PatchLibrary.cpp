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

bool PatchLibrary::load(const juce::String& group, const juce::String& name, PatchModel& model) const
{
    // Match by display name within the group (the file name is a sanitized form).
    for (const auto& f : groupDir(group).findChildFiles(juce::File::findFiles, false, juce::String("*") + kFileExt))
    {
        if (auto xml = juce::XmlDocument::parse(f))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            if (tree.hasType(kRootType) && tree.getProperty("name").toString() == name)
            {
                auto patch = tree.getChildWithName(PatchModel::kStateType);
                if (patch.isValid()) { model.fromValueTree(patch); return true; }
            }
        }
    }
    return false;
}

bool PatchLibrary::remove(const juce::String& group, const juce::String& name)
{
    bool removed = false;
    for (const auto& f : groupDir(group).findChildFiles(juce::File::findFiles, false, juce::String("*") + kFileExt))
    {
        if (auto xml = juce::XmlDocument::parse(f))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            if (tree.hasType(kRootType) && tree.getProperty("name").toString() == name)
                removed |= f.deleteFile();
        }
    }
    // Tidy up an emptied group folder so it stops showing as a heading.
    if (removed && group.isNotEmpty())
    {
        const auto gdir = groupDir(group);
        if (gdir.isDirectory() && gdir.getNumberOfChildFiles(juce::File::findFiles) == 0)
            gdir.deleteRecursively();
    }
    refresh();
    return removed;
}

// --- Root-group convenience overloads ----------------------------------------
bool PatchLibrary::save(const juce::String& name, const PatchModel& model) { return save(juce::String(), name, model); }
bool PatchLibrary::load(const juce::String& name, PatchModel& model) const  { return load(juce::String(), name, model); }
bool PatchLibrary::remove(const juce::String& name)                         { return remove(juce::String(), name); }
} // namespace amyplug
