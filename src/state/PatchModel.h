// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// PatchModel — the canonical, serializable description of the AMY state. This is
// the SINGLE SOURCE OF TRUTH. Both backends are projections of it:
//   * SoftwareBackend.rebuildFrom(model) resets libamy and replays toWireMessages()
//   * HardwareBackend.rebuildFrom(model) re-sends the same wire messages to the board
//
// Recall works because get/setStateInformation only ever (de)serialize this model
// (+ the APVTS); we never trust AMY's live internal state to survive a save.

#include <juce_data_structures/juce_data_structures.h>
#include <string>
#include <vector>

namespace amyplug
{
class PatchModel
{
public:
    PatchModel();

    // --- Canonical fields (extend as the editor grows) ---------------------
    // For the MVP a "patch" can be just a base AMY patch number + a handful of
    // macro edits. As the editor deepens, store the full per-osc graph here
    // (waves, envelopes as bp strings, mod routings, CtrlCoefs, effects).
    struct Synth
    {
        int  channel     = 1;     // 1..16 (== AMY synth)
        int  patchNumber = 0;     // AMY patch (0 Juno..)
        int  numVoices   = 6;
        // User-patch wire commands (for synths built from scratch / edited).
        std::vector<std::string> oscWireCommands;
    };

    std::vector<Synth> synths { Synth {} };

    // Global effects + mix (mirrors Parameters.h).
    float masterVolume = 1.0f;
    float reverb = 0.0f, chorus = 0.0f, echo = 0.0f;

    // --- Rebuild + persistence --------------------------------------------
    // Ordered wire messages that recreate this exact state in a fresh AMY.
    // Begins with a full reset. Used by both backends' rebuildFrom().
    std::vector<std::string> toWireMessages() const;

    // (De)serialize to a ValueTree so it nests inside the plugin state blob.
    juce::ValueTree toValueTree() const;
    void            fromValueTree(const juce::ValueTree& tree);

    static constexpr const char* kStateType = "AMYplugPatch";
};
} // namespace amyplug
