// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "PatchLibrary.h"

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

juce::File PatchLibrary::fileFor(const juce::String& name) const
{
    return dir.getChildFile(juce::File::createLegalFileName(name) + kFileExt);
}

void PatchLibrary::refresh()
{
    cachedNames.clear();
    if (! dir.isDirectory())
        return;

    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, juce::String("*") + kFileExt))
    {
        if (auto xml = juce::XmlDocument::parse(f))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            if (tree.hasType(kRootType))
                cachedNames.add(tree.getProperty("name", f.getFileNameWithoutExtension()).toString());
        }
    }
    cachedNames.sortNatural();
}

bool PatchLibrary::save(const juce::String& name, const PatchModel& model)
{
    if (name.trim().isEmpty())
        return false;
    if (! dir.isDirectory() && ! dir.createDirectory())
        return false;

    juce::ValueTree root { kRootType };
    root.setProperty("name", name, nullptr);
    root.appendChild(model.toValueTree(), nullptr);

    if (auto xml = root.createXml())
    {
        const bool ok = xml->writeTo(fileFor(name));
        refresh();
        return ok;
    }
    return false;
}

bool PatchLibrary::load(const juce::String& name, PatchModel& model) const
{
    // Match by display name (the on-disk file name is a sanitized form of it).
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, juce::String("*") + kFileExt))
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

bool PatchLibrary::remove(const juce::String& name)
{
    bool removed = false;
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, juce::String("*") + kFileExt))
    {
        if (auto xml = juce::XmlDocument::parse(f))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            if (tree.hasType(kRootType) && tree.getProperty("name").toString() == name)
                removed |= f.deleteFile();
        }
    }
    refresh();
    return removed;
}
} // namespace amyplug
