// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// SoftwareBackend — embeds AMY's C engine and renders audio inside the DAW.
// This is the ONLY translation unit that includes AMY's C headers, so AMY's C
// world stays contained behind this seam.

#include "IAmyBackend.h"
#include "AmyConfig.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace amyplug
{
// Embeds AMY's C engine and renders audio in the DAW.
//
// Threading model (keeps amy_add_message single-threaded):
//   * Note control (noteOn/noteOff/pitchBend/sustainPedal/allNotesOff) is called
//     by NoteRouter ON THE AUDIO THREAD inside processBlock, and goes DIRECTLY to
//     amy_add_message. The note path is allocation-free in AMY (events schedule
//     into a preallocated delta pool), so this is realtime-safe.
//   * Structural changes (sendWire, rebuildFrom: reset + patch/synth setup, which
//     DO allocate in AMY) are produced on the MESSAGE THREAD into an SPSC FIFO and
//     drained on the audio thread at the top of processBlock.
// Thus the audio thread is the only caller of amy_add_message; the message thread
// only ever writes the FIFO -> single-producer/single-consumer, lock-free.
class SoftwareBackend final : public IAmyBackend
{
public:
    SoftwareBackend();
    ~SoftwareBackend() override;

    void prepare(double hostSampleRate, int maxBlockSize) override;
    void release() override;
    void processBlock(juce::AudioBuffer<float>& audio) override;

    void sendWire(const char* msg, int len) override;   // message thread -> FIFO
    void streamWire(const char* msg, int len) override; // audio thread -> AMY now
    void flushPending() override { drainWireFifo(); }   // apply queued rebuild first
    void noteOn(int synth, int midiNote, float velocity) override;  // audio thread
    void noteOff(int synth, int midiNote) override;                 // audio thread
    void changeNote(int synth, int midiNote) override;              // audio thread (legato)
    void allNotesOff() override;                                    // audio thread
    void pitchBend(float semitonesNormalized) override;            // audio thread
    void sustainPedal(int synth, bool down) override;             // audio thread
    void rebuildFrom(const PatchModel& model) override;          // message thread -> FIFO

    Kind kind() const override { return Kind::Software; }

private:
    // --- Audio rendering helpers (audio thread) ----------------------------
    void renderAmyBlockToSrc();      // append one AMY block of float audio to srcBuf
    void ensureSrc(int framesNeeded);// top up srcBuf until it holds >= framesNeeded
    void consumeSrc(int frames);     // drop `frames` from the front of srcBuf

    // --- Wire FIFO -----------------------------------------------------------
    void pushWire(const char* msg, int len);  // producer (message thread)
    void drainWireFifo();                      // consumer (audio thread)

    bool   started = false;
    double hostRate = 0.0;

    // AMY runs at kAmySampleRate. If hostRate differs we resample. See ENGINE_NOTES.
    bool                       needsResample = false;
    juce::LagrangeInterpolator resamplerL, resamplerR;  // simple starting point (MVP)

    // AMY-rate float audio, deinterleaved. We pull fixed AMY blocks into here and
    // keep a running valid length so partial blocks carry across host callbacks.
    juce::AudioBuffer<float> srcBuf;
    int                      srcLen = 0;
    juce::AudioBuffer<float> monoScratch;   // right channel sink for mono downmix

    // SPSC wire-message FIFO. Fixed-size slots (no allocation on either thread).
    struct WireSlot { int len = 0; char data[256] = {}; };
    static constexpr int kFifoSlots = 1024;
    juce::AbstractFifo      wireFifo { kFifoSlots };
    std::vector<WireSlot>   wireSlots;       // sized kFifoSlots in the constructor
};
} // namespace amyplug
