// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "HardwareBackend.h"
#include "AmyWire.h"
#include "../state/PatchModel.h"

namespace amyplug
{
HardwareBackend::HardwareBackend() : juce::Thread("AMYboard MIDI out") {}
HardwareBackend::~HardwareBackend() { closeOutput(); }

void HardwareBackend::prepare(double, int) {}
void HardwareBackend::release()            { closeOutput(); }

void HardwareBackend::processBlock(juce::AudioBuffer<float>& audio)
{
    audio.clear(); // hardware makes the sound; the plugin emits silence
}

// --- outgoing queue (RT-safe) ----------------------------------------------
void HardwareBackend::enqueue(const juce::MidiMessage& m)
{
    const juce::ScopedLock sl(midiLock);
    sendQueue.addEvent(m, 0);
    notify();   // wake the sender thread (harmless if no device is open)
}

void HardwareBackend::run()
{
    // Drain the queue to the open device off the audio thread. sendMessageNow can
    // block on the OS MIDI driver, which is exactly what we keep off the RT thread.
    // RATE-LIMITED: a whole patch (or a knob sweep) can queue dozens of SysEx at
    // once; firing them all instantly makes the board's small CPU pop while it
    // reconfigures. Send a bounded batch per wake and keep the rest for next time.
    constexpr int kBurstCap = 8;
    juce::MidiBuffer pending;   // not-yet-sent backlog
    while (! threadShouldExit())
    {
        {
            const juce::ScopedLock sl(midiLock);
            if (output != nullptr && ! sendQueue.isEmpty())
            { pending.addEvents(sendQueue, 0, -1, 0); sendQueue.clear(); }
        }
        int sent = 0;
        if (output != nullptr && ! pending.isEmpty())
        {
            juce::MidiBuffer keep;
            for (const auto meta : pending)
            {
                if (sent < kBurstCap) { output->sendMessageNow(meta.getMessage()); ++sent; }
                else                    keep.addEvent(meta.getMessage(), 0);
            }
            pending.swapWith(keep);
        }
        wait(pending.isEmpty() ? 4 : 1);   // poll faster while a backlog drains
    }
}

// --- wire / note events (called from audio + message threads) ---------------
void HardwareBackend::sendWire(const char* msg, int len)
{
    auto sysex = wrapSysex(msg, len);   // F0 00 03 45 <ascii> F7
    enqueue(juce::MidiMessage::createSysExMessage(sysex.data() + 1, (int) sysex.size() - 2));
}

void HardwareBackend::streamWire(const char* msg, int len) { sendWire(msg, len); }

void HardwareBackend::noteOn(int synth, int midiNote, float velocity)
{
    enqueue(juce::MidiMessage::noteOn(juce::jlimit(1, 16, synth), midiNote, velocity));
}

void HardwareBackend::noteOff(int synth, int midiNote)
{
    enqueue(juce::MidiMessage::noteOff(juce::jlimit(1, 16, synth), midiNote));
}

void HardwareBackend::allNotesOff()
{
    // Both CC123 (all-notes-off, honours release) and CC120 (all-sound-off, kills
    // immediately) on every channel — a stuck board note must die no matter what.
    for (int ch = 1; ch <= 16; ++ch)
    {
        enqueue(juce::MidiMessage::allNotesOff(ch));
        enqueue(juce::MidiMessage::allSoundOff(ch));
    }
    // ...and AMY's own reset-all-notes as wire SysEx: the board's AMY honours this
    // directly even if its MIDI layer ignores CC 120/123.
    WireBuilder w; w.reset(amy::Reset::AllNotes);
    sendWire(w.str(), w.size());
}

void HardwareBackend::pitchBend(float semitonesNormalized)
{
    // Map normalized [-1,1] to 14-bit. Bend range handling is a TODO (RPN).
    const int value = juce::jlimit(0, 16383, (int) std::lround((semitonesNormalized * 0.5f + 0.5f) * 16383.0f));
    enqueue(juce::MidiMessage::pitchWheel(1, value));
}

void HardwareBackend::sustainPedal(int synth, bool down)
{
    enqueue(juce::MidiMessage::controllerEvent(juce::jlimit(1, 16, synth), 64, down ? 127 : 0));
}

void HardwareBackend::rebuildFrom(const PatchModel& model)
{
    // Off-thread. First kill any notes still ringing on the board from a previous
    // session/patch (the board holds them across plugin reloads — that's the "noise
    // on load"). Then re-send the whole patch (reset + osc graph + mix) as wire SysEx.
    allNotesOff();
    for (const auto& msg : model.toWireMessages())
        sendWire(msg.c_str(), (int) msg.size());
}

// --- device management (message thread) ------------------------------------
juce::StringArray HardwareBackend::availableOutputs() const
{
    juce::StringArray names;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        names.add(d.name);
    return names;
}

bool HardwareBackend::openOutput(const juce::String& identifier)
{
    closeOutput();
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.identifier == identifier || d.name == identifier)
        {
            auto dev = juce::MidiOutput::openDevice(d.identifier);
            if (dev == nullptr) return false;
            {
                const juce::ScopedLock sl(midiLock);
                output = std::move(dev);
                connected = d.name;
            }
            startThread(juce::Thread::Priority::high);
            return true;
        }
    return false;
}

void HardwareBackend::closeOutput()
{
    signalThreadShouldExit();
    notify();
    stopThread(500);
    const juce::ScopedLock sl(midiLock);
    output.reset();
    connected = {};
    sendQueue.clear();
}

bool HardwareBackend::pingBoard()
{
    // Send `zI`; a live board replies F0 00 03 45 'O' 'K' F7 on its MIDI-in->out.
    // Observing the reply needs a MidiInput (the "auto-detect" scope) — TODO.
    WireBuilder w; w.raw("zI");
    sendWire(w.str(), w.size());
    return false;
}

void HardwareBackend::collectHostMidi(juce::MidiBuffer& out, int numSamples)
{
    const juce::ScopedLock sl(midiLock);
    if (output != nullptr) return;   // a device is open -> the sender thread delivers it
    out.addEvents(sendQueue, 0, numSamples, 0);
    sendQueue.clear();
}
} // namespace amyplug
