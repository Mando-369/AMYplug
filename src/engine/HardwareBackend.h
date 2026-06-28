// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// HardwareBackend — drives a physical AMYboard. Produces NO audio; it serializes
// events to MIDI (notes/CC/pitchbend) and AMY wire messages (as SysEx) and emits
// them to a chosen MIDI output (the board's USB-MIDI port, or the host MIDI out).
//
// See docs/HARDWARE_MODE.md. Verify the AMYboard's USB-MIDI enumeration on real
// hardware before assuming a port name.

#include "IAmyBackend.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace amyplug
{
class HardwareBackend final : public IAmyBackend
{
public:
    HardwareBackend();
    ~HardwareBackend() override;

    void prepare(double hostSampleRate, int maxBlockSize) override;
    void release() override;
    void processBlock(juce::AudioBuffer<float>& audio) override; // clears audio

    void sendWire(const char* msg, int len) override;            // -> SysEx
    void streamWire(const char* msg, int len) override;          // -> SysEx (audio thread)
    void noteOn(int synth, int midiNote, float velocity) override;
    void noteOff(int synth, int midiNote) override;
    void allNotesOff() override;
    void pitchBend(float semitonesNormalized) override;
    void sustainPedal(int synth, bool down) override;
    void rebuildFrom(const PatchModel& model) override;

    Kind kind() const override { return Kind::Hardware; }

    // Device management (called from UI / message thread).
    juce::StringArray availableOutputs() const;
    bool   openOutput(const juce::String& identifier);
    void   closeOutput();
    bool   isConnected() const { return output != nullptr; }
    // Sends `zI` ping and watches for the F0 00 03 45 'O' 'K' F7 reply.
    bool   pingBoard();

    // If we instead route through the host's MIDI-out, the processor pulls this
    // buffer each block and merges it into its outgoing MidiBuffer.
    void   collectHostMidi(juce::MidiBuffer& out, int numSamples);

private:
    std::unique_ptr<juce::MidiOutput> output;      // direct-to-board path
    juce::MidiBuffer pendingHostMidi;              // host-routed path
    juce::CriticalSection midiLock;                // UI/audio handoff (host path only)
};
} // namespace amyplug
