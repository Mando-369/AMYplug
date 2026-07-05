// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// MockBackend — a recording IAmyBackend for unit-testing NoteRouter (and the
// parameter-streaming path) without AMY or any audio device. Every call is logged
// so tests can assert the exact note lifecycle.
#include "engine/IAmyBackend.h"
#include <string>
#include <vector>

namespace amyplug
{
class MockBackend final : public IAmyBackend
{
public:
    struct NoteEvent { int synth; int note; float vel; bool on; };

    std::vector<NoteEvent>  notes;          // every noteOn/noteOff, in order
    std::vector<std::pair<int, int>> pitchChanges;   // (synth, note) from changeNote (legato)
    std::vector<std::string> wires;          // sendWire payloads
    int   allNotesOffCount = 0;
    float lastPitchBend    = 0.0f;
    int   pitchBendCount   = 0;
    std::vector<std::pair<int, bool>> sustainEvents;  // (synth, down)
    int   rebuildCount = 0;

    // Convenience: how many notes are currently "on" by our log (on minus off).
    int netOnFor(int synth, int note) const
    {
        int net = 0;
        for (const auto& e : notes)
            if (e.synth == synth && e.note == note) net += e.on ? 1 : -1;
        return net;
    }
    int totalNetOn() const
    {
        int net = 0;
        for (const auto& e : notes) net += e.on ? 1 : -1;
        return net;
    }

    // --- IAmyBackend ------------------------------------------------------
    void prepare(double, int) override {}
    void release() override {}
    void processBlock(juce::AudioBuffer<float>& audio) override { audio.clear(); }

    void sendWire(const char* msg, int len) override   { wires.emplace_back(msg, (size_t) len); }
    void streamWire(const char* msg, int len) override  { wires.emplace_back(msg, (size_t) len); }
    void noteOn(int synth, int note, float vel) override { notes.push_back({ synth, note, vel, true }); }
    void noteOff(int synth, int note) override          { notes.push_back({ synth, note, 0.0f, false }); }
    void changeNote(int synth, int note) override        { pitchChanges.emplace_back(synth, note); }
    void allNotesOff() override                          { ++allNotesOffCount; }
    void pitchBend(float v) override                     { lastPitchBend = v; ++pitchBendCount; }
    void sustainPedal(int synth, bool down) override     { sustainEvents.emplace_back(synth, down); }
    void rebuildFrom(const PatchModel&) override         { ++rebuildCount; }
    Kind kind() const override                           { return Kind::Software; }
};
} // namespace amyplug
