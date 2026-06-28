// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "PatchModel.h"
#include "../engine/AmyWire.h"

namespace amyplug
{
PatchModel::PatchModel() = default;

std::vector<std::string> PatchModel::toWireMessages() const
{
    std::vector<std::string> out;

    // 1. Full reset so we start from a known state.
    {
        WireBuilder w; w.reset(amy::Reset::Amy);
        out.emplace_back(w.str());
    }

    // 2. Recreate each synth (patch + polyphony), then any per-osc edits.
    for (const auto& s : synths)
    {
        WireBuilder w;
        w.synth(s.channel).numVoices(s.numVoices).patch(s.patchNumber);
        out.emplace_back(w.str());

        for (const auto& cmd : s.oscWireCommands)
            out.emplace_back(cmd); // already-formed wire strings for edited oscs
    }

    // 3. Global mix + effects.
    {
        WireBuilder w; w.raw("V").raw(std::to_string(masterVolume).c_str());
        out.emplace_back(w.str());
        // TODO: reverb/chorus/echo via 'h'/'k'/'M' wire codes (see api.md).
    }

    return out;
}

juce::ValueTree PatchModel::toValueTree() const
{
    juce::ValueTree root { kStateType };
    root.setProperty("masterVolume", masterVolume, nullptr);
    root.setProperty("reverb", reverb, nullptr);
    root.setProperty("chorus", chorus, nullptr);
    root.setProperty("echo",   echo,   nullptr);

    for (const auto& s : synths)
    {
        juce::ValueTree sv { "Synth" };
        sv.setProperty("channel",     s.channel,     nullptr);
        sv.setProperty("patchNumber", s.patchNumber, nullptr);
        sv.setProperty("numVoices",   s.numVoices,   nullptr);
        juce::String joined;
        for (const auto& c : s.oscWireCommands) joined << juce::String(c) << "\n";
        sv.setProperty("oscWire", joined, nullptr);
        root.addChild(sv, -1, nullptr);
    }
    return root;
}

void PatchModel::fromValueTree(const juce::ValueTree& tree)
{
    if (! tree.hasType(kStateType)) return;
    masterVolume = (float) tree.getProperty("masterVolume", 1.0);
    reverb = (float) tree.getProperty("reverb", 0.0);
    chorus = (float) tree.getProperty("chorus", 0.0);
    echo   = (float) tree.getProperty("echo",   0.0);

    synths.clear();
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto sv = tree.getChild(i);
        if (! sv.hasType("Synth")) continue;
        Synth s;
        s.channel     = (int) sv.getProperty("channel", 1);
        s.patchNumber = (int) sv.getProperty("patchNumber", 0);
        s.numVoices   = (int) sv.getProperty("numVoices", 6);
        auto lines = juce::StringArray::fromLines(sv.getProperty("oscWire").toString());
        for (auto& l : lines) if (l.isNotEmpty()) s.oscWireCommands.push_back(l.toStdString());
        synths.push_back(std::move(s));
    }
    if (synths.empty()) synths.push_back(Synth {});
}
} // namespace amyplug
