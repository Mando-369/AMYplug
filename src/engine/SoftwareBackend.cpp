// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "SoftwareBackend.h"
#include "AmyWire.h"
#include "../state/PatchModel.h"
#include <cmath>
#include <cstring>

// AMY is C. Keep all of its surface area inside this file.
extern "C" {
#include "amy.h"
}

namespace amyplug
{
namespace
{
// Process-wide user count for AMY's single global engine. amy_start() runs once
// (first backend to prepare) and amy_stop() once (last backend to release), so
// loading two plugin instances cannot double-init / double-free AMY's globals.
// NOTE: AMY is single-instance — instances share one engine. See docs/ENGINE_NOTES.md §4.
std::atomic<int> s_engineUsers { 0 };

inline void amyAddNow(const char* s)
{
    // amy_add_message takes char* but only reads it. Safe to const_cast.
    amy_add_message(const_cast<char*>(s));
}
} // namespace

SoftwareBackend::SoftwareBackend()
{
    wireSlots.resize(kFifoSlots);   // one-time allocation, off the audio thread
}

SoftwareBackend::~SoftwareBackend() { release(); }

void SoftwareBackend::prepare(double hostSampleRate, int maxBlockSize)
{
    hostRate      = hostSampleRate;
    needsResample = std::abs(hostSampleRate - (double) kAmySampleRate) > 0.5;

    if (! started)
    {
        if (s_engineUsers.fetch_add(1) == 0)
        {
            amy_config_t cfg = amy_default_config();   // already audio/midi = NONE
            cfg.audio = AMY_AUDIO_IS_NONE;             // be explicit: we own audio
            cfg.midi  = AMY_MIDI_IS_NONE;             // we own MIDI
            cfg.platform.multicore   = 0;             // never spawn threads in a plugin
            cfg.platform.multithread = 0;
            amy_start(cfg);
        }
        started = true;
    }

    // AMY-rate float source: large enough to over-pull a host block plus one AMY
    // block of carry, even if the host runs well below AMY's rate (ratio > 1).
    srcBuf.setSize(kAmyChannels, maxBlockSize * 4 + kAmyBlockSize + 8, false, false, true);
    srcBuf.clear();
    srcLen = 0;
    monoScratch.setSize(1, maxBlockSize + 8, false, false, true);

    resamplerL.reset();
    resamplerR.reset();

    // Drop anything stale queued from a previous configuration.
    wireFifo.reset();
}

void SoftwareBackend::release()
{
    if (started)
    {
        started = false;
        if (s_engineUsers.fetch_sub(1) == 1)
            amy_stop();
    }
}

// ---------------------------------------------------------------------------
// Audio rendering (audio thread)
// ---------------------------------------------------------------------------
void SoftwareBackend::renderAmyBlockToSrc()
{
    // One AMY block: kAmyBlockSize frames, int16 interleaved stereo.
    const AmySample* b = amy_simple_fill_buffer();

    float* dl = srcBuf.getWritePointer(0) + srcLen;
    float* dr = srcBuf.getWritePointer(1) + srcLen;
    constexpr float kInv = 1.0f / 32768.0f;
    for (int i = 0; i < kAmyBlockSize; ++i)
    {
        dl[i] = (float) b[2 * i]     * kInv;
        dr[i] = (float) b[2 * i + 1] * kInv;
    }
    srcLen += kAmyBlockSize;
}

void SoftwareBackend::ensureSrc(int framesNeeded)
{
    const int cap = srcBuf.getNumSamples() - kAmyBlockSize;   // headroom for one block
    while (srcLen < framesNeeded && srcLen <= cap)
        renderAmyBlockToSrc();
}

void SoftwareBackend::consumeSrc(int frames)
{
    frames = juce::jlimit(0, srcLen, frames);
    const int remain = srcLen - frames;
    if (remain > 0)
    {
        for (int ch = 0; ch < srcBuf.getNumChannels(); ++ch)
        {
            float* p = srcBuf.getWritePointer(ch);
            std::memmove(p, p + frames, (size_t) remain * sizeof(float));
        }
    }
    srcLen = remain;
}

void SoftwareBackend::processBlock(juce::AudioBuffer<float>& audio)
{
    // 1. Apply any structural changes queued from the message thread.
    drainWireFifo();

    const int n   = audio.getNumSamples();
    const int nch = audio.getNumChannels();
    if (! started || n <= 0) { audio.clear(); return; }

    if (needsResample)
    {
        const double ratio = (double) kAmySampleRate / hostRate;   // input per output
        const int    need  = (int) std::ceil((double) n * ratio) + 4;
        ensureSrc(need);

        float* d0 = audio.getWritePointer(0);
        float* dr = (nch > 1) ? audio.getWritePointer(1) : monoScratch.getWritePointer(0);

        const int usedL = resamplerL.process(ratio, srcBuf.getReadPointer(0), d0, n);
        const int usedR = resamplerR.process(ratio, srcBuf.getReadPointer(1), dr, n);
        consumeSrc(juce::jmax(usedL, usedR));

        if (nch == 1)
            for (int i = 0; i < n; ++i)
                d0[i] = 0.5f * (d0[i] + monoScratch.getReadPointer(0)[i]);
    }
    else
    {
        ensureSrc(n);
        if (nch > 1)
        {
            audio.copyFrom(0, 0, srcBuf, 0, 0, n);
            audio.copyFrom(1, 0, srcBuf, 1, 0, n);
        }
        else
        {
            float*       d0 = audio.getWritePointer(0);
            const float* L  = srcBuf.getReadPointer(0);
            const float* R  = srcBuf.getReadPointer(1);
            for (int i = 0; i < n; ++i) d0[i] = 0.5f * (L[i] + R[i]);
        }
        consumeSrc(n);
    }

    // Any channels beyond the stereo pair get silence.
    for (int ch = 2; ch < nch; ++ch)
        audio.clear(ch, 0, n);
}

// ---------------------------------------------------------------------------
// Wire FIFO
// ---------------------------------------------------------------------------
void SoftwareBackend::pushWire(const char* msg, int len)
{
    if (len <= 0) return;
    if (len > 255) len = 255;                     // AMY message cap; never overflow

    const auto scope = wireFifo.write(1);
    if (scope.blockSize1 > 0)
    {
        auto& slot = wireSlots[(size_t) scope.startIndex1];
        std::memcpy(slot.data, msg, (size_t) len);
        slot.data[len] = 0;
        slot.len       = len;
    }
    // else: FIFO full -> drop. Structural messages are rare; this should not happen.
}

void SoftwareBackend::drainWireFifo()
{
    const auto scope = wireFifo.read(wireFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i)
        amyAddNow(wireSlots[(size_t) (scope.startIndex1 + i)].data);
    for (int i = 0; i < scope.blockSize2; ++i)
        amyAddNow(wireSlots[(size_t) (scope.startIndex2 + i)].data);
}

void SoftwareBackend::sendWire(const char* msg, int len)
{
    pushWire(msg, len);
}

void SoftwareBackend::streamWire(const char* msg, int len)
{
    // Audio thread: apply immediately (msg is null-terminated by WireBuilder::str).
    juce::ignoreUnused(len);
    amyAddNow(msg);
}

// ---------------------------------------------------------------------------
// Note control (audio thread, direct to AMY — allocation-free path)
// ---------------------------------------------------------------------------
void SoftwareBackend::noteOn(int synth, int midiNote, float velocity)
{
    WireBuilder w; w.synth(synth).note((float) midiNote).velocity(velocity);
    amyAddNow(w.str());
}

void SoftwareBackend::noteOff(int synth, int midiNote)
{
    WireBuilder w; w.synth(synth).note((float) midiNote).velocity(0.0f);
    amyAddNow(w.str());
}

void SoftwareBackend::changeNote(int synth, int midiNote)
{
    // Pitch only — NO velocity. AMY moves the sounding voice to this note without a
    // note-on, so the envelope keeps running (no re-attack) and the pitch glides when
    // portamento is set on the melodic oscs. This is the Legato slur.
    WireBuilder w; w.synth(synth).note((float) midiNote);
    amyAddNow(w.str());
}

void SoftwareBackend::allNotesOff()
{
    // Release every note on every synth (keeps the loaded patches intact).
    WireBuilder w; w.reset(amy::Reset::AllNotes);
    amyAddNow(w.str());
}

void SoftwareBackend::pitchBend(float semis)
{
    WireBuilder w; w.pitchBend(semis);
    amyAddNow(w.str());
}

void SoftwareBackend::sustainPedal(int synth, bool down)
{
    WireBuilder w; w.synth(synth).raw(down ? "ip1" : "ip0");
    amyAddNow(w.str());
}

// ---------------------------------------------------------------------------
// Rebuild (message thread -> FIFO; applied on the audio thread next block)
// ---------------------------------------------------------------------------
void SoftwareBackend::rebuildFrom(const PatchModel& model)
{
    // model.toWireMessages() begins with a reset, then recreates each synth from
    // its patch + polyphony, then global mix. Queue them; processBlock applies
    // them in order so AMY's live state == the canonical model.
    for (const auto& msg : model.toWireMessages())
        pushWire(msg.c_str(), (int) msg.size());
}
} // namespace amyplug
