// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// IAmyBackend — the seam between the plugin and "wherever AMY actually lives".
// SoftwareBackend renders audio with libamy; HardwareBackend forwards events to a
// physical AMYboard over MIDI/SysEx. The processor talks ONLY to this interface,
// so flipping the mode switch is just swapping the concrete backend.

#include <juce_audio_basics/juce_audio_basics.h>
#include <string>

namespace amyplug
{
class PatchModel;

class IAmyBackend
{
public:
    virtual ~IAmyBackend() = default;

    // Called from prepareToPlay. hostSampleRate may differ from AMY's native rate;
    // the backend is responsible for reconciling it (resample or rebuild). See
    // docs/ENGINE_NOTES.md.
    virtual void prepare(double hostSampleRate, int maxBlockSize) = 0;
    virtual void release() = 0;

    // Realtime: render/forward one block. `audio` is the host buffer to fill
    // (Software mode) or ignored (Hardware mode). MIDI already translated upstream.
    virtual void processBlock(juce::AudioBuffer<float>& audio) = 0;

    // Send a single already-built AMY wire message (ASCII) from the MESSAGE thread:
    // implementations push to a preallocated lock-free FIFO, applied next block.
    virtual void sendWire(const char* msg, int len) = 0;

    // Like sendWire, but called ON THE AUDIO THREAD (e.g. parameter automation
    // streamed from processBlock). Must apply immediately and not allocate. The
    // SoftwareBackend hands it straight to AMY; HardwareBackend queues it as SysEx.
    virtual void streamWire(const char* msg, int len) = 0;

    // Convenience note-control used by NoteRouter (RT-safe).
    virtual void noteOn(int synth, int midiNote, float velocity) = 0;
    virtual void noteOff(int synth, int midiNote) = 0;
    virtual void allNotesOff() = 0;          // the anti-hang hammer
    // Pitch offset in OCTAVES (AMY's `s` unit). NoteRouter already applied the
    // pitch-bend range, so 0.1667 == +2 semitones. (HardwareBackend approximates.)
    virtual void pitchBend(float octaves) = 0;
    virtual void sustainPedal(int synth, bool down) = 0;

    // Re-create the entire AMY state from the canonical model (project load,
    // mode switch, patch change). NOT RT-safe — call off the audio thread.
    virtual void rebuildFrom(const PatchModel& model) = 0;

    // Identifies the concrete backend for UI/state.
    enum class Kind { Software, Hardware };
    virtual Kind kind() const = 0;
};
} // namespace amyplug
