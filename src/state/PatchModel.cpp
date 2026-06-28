// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "PatchModel.h"
#include "../engine/AmyWire.h"

namespace amyplug
{
PatchModel::PatchModel() = default;

namespace
{
// AMY amp envelope as breakpoint set bp0: "attackMs,1,decayMs,sustain,releaseMs,0".
// Time/value pairs; the final pair is the release, triggered on note-off.
std::string adsrToBp0(const PatchModel::Synth& s)
{
    auto ms = [] (float sec) { return juce::String(juce::roundToInt(sec * 1000.0f)); };
    return (ms(s.ampAttack) + ",1," + ms(s.ampDecay) + "," + juce::String(s.ampSustain, 3)
            + "," + ms(s.ampRelease) + ",0").toStdString();
}
} // namespace

std::vector<std::string> PatchModel::toWireMessages() const
{
    std::vector<std::string> out;

    // 1. Full reset so we start from a known state.
    {
        WireBuilder w; w.reset(amy::Reset::Amy);
        out.emplace_back(w.str());
    }

    // 2. Recreate each synth (patch + polyphony), then any per-osc edits, then the
    //    automatable macros (broadcast to the synth's voices).
    for (const auto& s : synths)
    {
        { WireBuilder w; w.synth(s.channel).numVoices(s.numVoices).patch(s.patchNumber);
          out.emplace_back(w.str()); }

        for (const auto& cmd : s.oscWireCommands)
            out.emplace_back(cmd); // already-formed wire strings for edited oscs

        { WireBuilder w; w.synth(s.channel).filterFreq(s.filterCutoff); out.emplace_back(w.str()); }
        { WireBuilder w; w.synth(s.channel).resonance(s.filterReso);    out.emplace_back(w.str()); }
        { WireBuilder w; w.synth(s.channel).bp0(adsrToBp0(s).c_str());  out.emplace_back(w.str()); }
    }

    // 3. Global mix + effects (bus 0).
    { WireBuilder w; w.volume(masterVolume); out.emplace_back(w.str()); }
    { WireBuilder w; w.reverb(reverb);       out.emplace_back(w.str()); }
    { WireBuilder w; w.chorus(chorus);       out.emplace_back(w.str()); }
    { WireBuilder w; w.echo(echo);           out.emplace_back(w.str()); }

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
        sv.setProperty("filterCutoff", s.filterCutoff, nullptr);
        sv.setProperty("filterReso",   s.filterReso,   nullptr);
        sv.setProperty("ampAttack",    s.ampAttack,    nullptr);
        sv.setProperty("ampDecay",     s.ampDecay,     nullptr);
        sv.setProperty("ampSustain",   s.ampSustain,   nullptr);
        sv.setProperty("ampRelease",   s.ampRelease,   nullptr);
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
        s.filterCutoff = (float) sv.getProperty("filterCutoff", 8000.0);
        s.filterReso   = (float) sv.getProperty("filterReso",   0.7);
        s.ampAttack    = (float) sv.getProperty("ampAttack",    0.005);
        s.ampDecay     = (float) sv.getProperty("ampDecay",     0.1);
        s.ampSustain   = (float) sv.getProperty("ampSustain",   0.7);
        s.ampRelease   = (float) sv.getProperty("ampRelease",   0.25);
        auto lines = juce::StringArray::fromLines(sv.getProperty("oscWire").toString());
        for (auto& l : lines) if (l.isNotEmpty()) s.oscWireCommands.push_back(l.toStdString());
        synths.push_back(std::move(s));
    }
    if (synths.empty()) synths.push_back(Synth {});
}
} // namespace amyplug
