// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// PatchLibrary — named user presets stored on disk. A user patch (v1) is a snapshot
// of the current preset: the base built-in patch number plus the macro edits
// (cutoff/reso/ADSR/FX/volume/voices). It is just a serialized PatchModel wrapped
// with a display name, so it round-trips through the same ValueTree the project
// state uses. (Arbitrary osc-graph patches in AMY slots 1024-1055 come later, with
// the per-engine editor.)

#include "PatchModel.h"
#include <juce_core/juce_core.h>

namespace amyplug
{
class PatchLibrary
{
public:
    PatchLibrary();                                   // default dir + initial scan

    void setDirectory(const juce::File& dir);          // (tests) point at a temp dir
    juce::File directory() const { return dir; }

    // A user patch, optionally inside a named group (subfolder). The user's own
    // saved patches live in the root (group == ""); imported DX7 cartridges go in a
    // group named after the cartridge so they don't clutter the user's list.
    struct Entry { juce::String group; juce::String name; };

    void           refresh();                          // rescan the directory
    juce::StringArray names() const { return cachedNames; }  // root group only (compat)
    const std::vector<Entry>& entries() const { return cachedEntries; }  // all, grouped

    // Root-group operations (the user's own patches).
    bool save(const juce::String& name, const PatchModel& model);
    bool load(const juce::String& name, PatchModel& model) const;
    bool remove(const juce::String& name);
    // Group-aware operations (group "" == root).
    bool save(const juce::String& group, const juce::String& name, const PatchModel& model);
    bool load(const juce::String& group, const juce::String& name, PatchModel& model) const;
    bool remove(const juce::String& group, const juce::String& name);

    static constexpr const char* kFileExt   = ".amypatch";
    static constexpr const char* kRootType  = "AMYplugUserPatch";

private:
    juce::File groupDir(const juce::String& group) const;
    juce::File fileFor(const juce::String& group, const juce::String& name) const;

    juce::File         dir;
    juce::StringArray  cachedNames;                    // root-group display names, sorted
    std::vector<Entry> cachedEntries;                  // all entries, grouped + sorted
};
} // namespace amyplug
