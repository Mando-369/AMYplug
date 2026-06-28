// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "HardwareBackend.h"
#include "AmyWire.h"
#include "../state/PatchModel.h"

namespace amyplug
{
HardwareBackend::HardwareBackend()  = default;
HardwareBackend::~HardwareBackend() { closeOutput(); }

void HardwareBackend::prepare(double, int) {}
void HardwareBackend::release()            { closeOutput(); }

void HardwareBackend::processBlock(juce::AudioBuffer<float>& audio)
{
    audio.clear(); // hardware makes the sound; the plugin emits silence
}

void HardwareBackend::sendWire(const char* msg, int len)
{
    auto sysex = wrapSysex(msg, len);
    if (output != nullptr)
    {
        output->sendMessageNow(juce::MidiMessage::createSysExMessage(
            sysex.data() + 1, (int) sysex.size() - 2)); // JUCE adds F0/F7 itself
    }
    else
    {
        const juce::ScopedLock sl(midiLock);
        pendingHostMidi.addEvent(juce::MidiMessage::createSysExMessage(
            sysex.data() + 1, (int) sysex.size() - 2), 0);
    }
}

void HardwareBackend::streamWire(const char* msg, int len)
{
    sendWire(msg, len);   // same SysEx path; queued for the host MIDI-out
}

void HardwareBackend::noteOn(int synth, int midiNote, float velocity)
{
    const int ch = juce::jlimit(1, 16, synth); // AMY synth 1..16 == MIDI channel
    auto m = juce::MidiMessage::noteOn(ch, midiNote, velocity);
    if (output) output->sendMessageNow(m);
    else { const juce::ScopedLock sl(midiLock); pendingHostMidi.addEvent(m, 0); }
}

void HardwareBackend::noteOff(int synth, int midiNote)
{
    const int ch = juce::jlimit(1, 16, synth);
    auto m = juce::MidiMessage::noteOff(ch, midiNote);
    if (output) output->sendMessageNow(m);
    else { const juce::ScopedLock sl(midiLock); pendingHostMidi.addEvent(m, 0); }
}

void HardwareBackend::allNotesOff()
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        auto m = juce::MidiMessage::allNotesOff(ch);
        if (output) output->sendMessageNow(m);
        else { const juce::ScopedLock sl(midiLock); pendingHostMidi.addEvent(m, 0); }
    }
}

void HardwareBackend::pitchBend(float semitonesNormalized)
{
    // Map normalized [-1,1] to 14-bit. Bend range handling is a TODO (RPN).
    const int value = juce::jlimit(0, 16383, (int) std::lround((semitonesNormalized * 0.5f + 0.5f) * 16383.0f));
    auto m = juce::MidiMessage::pitchWheel(1, value);
    if (output) output->sendMessageNow(m);
    else { const juce::ScopedLock sl(midiLock); pendingHostMidi.addEvent(m, 0); }
}

void HardwareBackend::sustainPedal(int synth, bool down)
{
    const int ch = juce::jlimit(1, 16, synth);
    auto m = juce::MidiMessage::controllerEvent(ch, 64, down ? 127 : 0);
    if (output) output->sendMessageNow(m);
    else { const juce::ScopedLock sl(midiLock); pendingHostMidi.addEvent(m, 0); }
}

void HardwareBackend::rebuildFrom(const PatchModel& model)
{
    // Off-thread. Re-send the whole patch so the board matches the project.
    //   for (auto& msg : model.toWireMessages()) sendWire(msg.c_str(), msg.size());
    juce::ignoreUnused(model);
}

juce::StringArray HardwareBackend::availableOutputs() const
{
    juce::StringArray names;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        names.add(d.name);
    return names;
}

bool HardwareBackend::openOutput(const juce::String& identifier)
{
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.identifier == identifier || d.name == identifier)
        {
            output = juce::MidiOutput::openDevice(d.identifier);
            return output != nullptr;
        }
    return false;
}

void HardwareBackend::closeOutput() { output.reset(); }

bool HardwareBackend::pingBoard()
{
    // Send `zI`; a live board replies F0 00 03 45 'O' 'K' F7 on its MIDI-in->out.
    // Requires a MidiInput callback to observe the reply — wired in M3.
    WireBuilder w; w.raw("zI");
    sendWire(w.str(), w.size());
    return false; // TODO: return true once the OK reply is observed
}

void HardwareBackend::collectHostMidi(juce::MidiBuffer& out, int numSamples)
{
    const juce::ScopedLock sl(midiLock);
    out.addEvents(pendingHostMidi, 0, numSamples, 0);
    pendingHostMidi.clear();
}
} // namespace amyplug
