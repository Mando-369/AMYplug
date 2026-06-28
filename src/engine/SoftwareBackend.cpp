// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "SoftwareBackend.h"
#include "AmyWire.h"
#include "../state/PatchModel.h"

// AMY is C. Keep all of its surface area inside this file.
extern "C" {
//   After bootstrap, the real header is third_party/amy/src/amy.h. Including it
//   here (added to libamy's include dirs by cmake/amy.cmake) gives us:
//     amy_default_config, amy_start, amy_stop, amy_default_event, amy_add_event,
//     amy_add_message, amy_simple_fill_buffer, amy_sysclock, ...
// #include "amy.h"
}

namespace amyplug
{
SoftwareBackend::SoftwareBackend() = default;
SoftwareBackend::~SoftwareBackend() { release(); }

void SoftwareBackend::prepare(double hostSampleRate, int maxBlockSize)
{
    hostRate      = hostSampleRate;
    needsResample = std::abs(hostSampleRate - (double) kAmySampleRate) > 0.5;

    // TODO(M1):
    //   amy_config_t cfg = amy_default_config();
    //   cfg.audio = AMY_AUDIO_IS_NONE;     // we drive rendering ourselves
    //   cfg.midi  = AMY_MIDI_IS_NONE;      // we feed MIDI ourselves
    //   cfg.platform.multicore = 0;
    //   cfg.platform.multithread = 0;
    //   amy_start(cfg);
    //   started = true;

    amyScratch.setSize(kAmyChannels, kAmyBlockSize, false, false, true);
    resamplerL.reset();
    resamplerR.reset();

    juce::ignoreUnused(maxBlockSize);
}

void SoftwareBackend::release()
{
    if (started)
    {
        // amy_stop();
        started = false;
    }
}

int SoftwareBackend::renderOneAmyBlock(juce::AudioBuffer<float>& dest, int destOffset)
{
    // TODO(M1):
    //   AmySample* b = amy_simple_fill_buffer();   // kAmyBlockSize * kAmyChannels int16
    //   Deinterleave + convert int16 -> float (/32768) into dest at destOffset.
    //   If needsResample, render into amyScratch then push through resamplerL/R.
    juce::ignoreUnused(dest, destOffset);
    return 0;
}

void SoftwareBackend::processBlock(juce::AudioBuffer<float>& audio)
{
    // 1. Drain the wire-message FIFO into amy_add_message()/amy_add_event().
    // 2. Repeatedly renderOneAmyBlock() until `audio` is full.
    // 3. Until implemented, output silence so the plugin loads cleanly.
    audio.clear();
}

void SoftwareBackend::sendWire(const char* msg, int len)
{
    juce::ignoreUnused(msg, len);
    // RT-safe path: push bytes into the lock-free FIFO; processBlock drains it.
    // Off-thread path (rebuild): may call amy_add_message(const_cast<char*>(msg)) directly.
}

void SoftwareBackend::noteOn(int synth, int midiNote, float velocity)
{
    WireBuilder w; w.synth(synth).note((float) midiNote).velocity(velocity);
    sendWire(w.str(), w.size());
}

void SoftwareBackend::noteOff(int synth, int midiNote)
{
    WireBuilder w; w.synth(synth).note((float) midiNote).velocity(0.0f);
    sendWire(w.str(), w.size());
}

void SoftwareBackend::allNotesOff()
{
    // Belt and suspenders: AMY honours an all-notes-off; also send vel=0 per synth
    // in NoteRouter. TODO confirm the exact wire form (CC123 via sendWire, or a
    // per-synth `vel=0` with no note as docs/synth.md describes).
}

void SoftwareBackend::pitchBend(float semis)
{
    WireBuilder w; w.pitchBend(semis);
    sendWire(w.str(), w.size());
}

void SoftwareBackend::sustainPedal(int synth, bool down)
{
    WireBuilder w; w.synth(synth).raw(down ? "ip1" : "ip0");
    sendWire(w.str(), w.size());
}

void SoftwareBackend::rebuildFrom(const PatchModel& model)
{
    // Off-thread. Reset AMY, then replay the model's wire messages so AMY state ==
    // the canonical model. This is what makes recall reliable.
    //   sendWire("S<RESET_ALL_OSCS>Z");  // via amy.send(reset=RESET_ALL_OSCS)
    //   for (auto& msg : model.toWireMessages()) amy_add_message(msg);
    juce::ignoreUnused(model);
}
} // namespace amyplug
