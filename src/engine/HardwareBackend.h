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
#include "SerialTransport.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace amyplug
{
// Owns a MIDI output to a physical AMYboard. All outgoing MIDI is enqueued (RT-safe
// from the audio thread) and flushed by a dedicated sender thread, so the audio
// thread never touches the (blocking) MIDI driver. If no device is open, the queue
// is instead drained into the host's MIDI-out via collectHostMidi().
class HardwareBackend final : public IAmyBackend,
                              private juce::Thread
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
    void changeNote(int synth, int midiNote) override;
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

    // Serial control path (AMYboard MicroPython REPL). This is the ONLY path that edits
    // the board's sound — MIDI-SysEx wire is inert on real hardware. Notes still go over
    // MIDI (openOutput above); patch/param wire goes here.
    juce::StringArray availableSerialPorts() const { return SerialTransport::availablePorts(); }
    juce::String detectSerialPort() const          { return SerialTransport::detectAmyboardPort(); }
    bool   openSerial(const juce::String& devicePath)
    { const bool ok = serial.open(devicePath); if (ok) assertLatency(); return ok; }
    void   closeSerial()                            { serial.close(); }
    bool   serialConnected() const                  { return serial.isOpen(); }
    juce::String serialPortName() const             { return serial.portName(); }

    // Re-assert AMY's scheduling latency_ms to a small value over the serial REPL. The
    // board firmware forces a huge default (~700 ms) and re-applies it (e.g. on transport
    // stop), so the processor calls this on connect + transport start. See assertLatency().
    void   assertLatency();
    void   setLatencyMs(int ms) { latencyMs = juce::jlimit(0, 5000, ms); }

    // If no device is open, the processor pulls the queued MIDI each block and
    // merges it into the host's outgoing MidiBuffer (DAW-routed path). When a device
    // IS open the sender thread delivers it directly, so this is a no-op then.
    void   collectHostMidi(juce::MidiBuffer& out, int numSamples);
    juce::String connectedName() const { return connected; }

private:
    void run() override;                     // sender thread: drains queue -> device
    void enqueue(const juce::MidiMessage& m); // RT-safe: any thread

    std::unique_ptr<juce::MidiOutput> output;      // direct-to-board device (or null)
    juce::MidiBuffer  sendQueue;                    // pending outgoing MIDI
    juce::CriticalSection midiLock;                 // guards sendQueue + output
    juce::String connected;                         // name of the open device ("" if none)
    SerialTransport serial;                         // wire/patch/param edits -> REPL
    int latencyMs { 0 };                            // AMY scheduling latency; 0 = board default (instant, live)
};
} // namespace amyplug
