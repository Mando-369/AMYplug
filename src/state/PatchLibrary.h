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

    void           refresh();                          // rescan the directory
    juce::StringArray names() const { return cachedNames; }

    bool save(const juce::String& name, const PatchModel& model);
    bool load(const juce::String& name, PatchModel& model) const;
    bool remove(const juce::String& name);

    static constexpr const char* kFileExt   = ".amypatch";
    static constexpr const char* kRootType  = "AMYplugUserPatch";

private:
    juce::File fileFor(const juce::String& name) const;

    juce::File        dir;
    juce::StringArray cachedNames;                     // display names, sorted
};
} // namespace amyplug
