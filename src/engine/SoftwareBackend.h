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

namespace amyplug
{
class SoftwareBackend final : public IAmyBackend
{
public:
    SoftwareBackend();
    ~SoftwareBackend() override;

    void prepare(double hostSampleRate, int maxBlockSize) override;
    void release() override;
    void processBlock(juce::AudioBuffer<float>& audio) override;

    void sendWire(const char* msg, int len) override;
    void noteOn(int synth, int midiNote, float velocity) override;
    void noteOff(int synth, int midiNote) override;
    void allNotesOff() override;
    void pitchBend(float semitonesNormalized) override;
    void sustainPedal(int synth, bool down) override;
    void rebuildFrom(const PatchModel& model) override;

    Kind kind() const override { return Kind::Software; }

private:
    // Pull one AMY block (int16 interleaved stereo) and convert into `dest`
    // (float, deinterleaved) starting at frame `destOffset`. Returns frames written.
    int renderOneAmyBlock(juce::AudioBuffer<float>& dest, int destOffset);

    bool   started = false;
    double hostRate = 0.0;

    // AMY runs at kAmySampleRate. If hostRate differs we resample. See ENGINE_NOTES.
    bool                 needsResample = false;
    juce::LagrangeInterpolator resamplerL, resamplerR;  // simple starting point

    // Scratch buffer of AMY-rate float audio that feeds the resampler.
    juce::AudioBuffer<float> amyScratch;

    // TODO: a single-producer/single-consumer lock-free FIFO of wire strings so
    // the message/UI thread can hand events to the audio thread without locking.
    // (juce::AbstractFifo + a flat char ring buffer works well.)
};
} // namespace amyplug
