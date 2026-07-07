// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyChorus — AMY's bus chorus, reconstructed as a standalone stereo block. AMY
// modulates a per-channel fractional delay (delay_line_in_out in delay.c) with a
// TRIANGLE LFO at `lfo_freq` and amplitude `depth` (config_chorus in amy.c sets the
// chorus mod-source oscillator to TRIANGLE), reading with linear interpolation. The
// delay is centered at max_delay/2 with the documented mapping "mod=1 -> max delay,
// mod=-1 -> no delay, mod=0 -> max_delay/2", and the LFO is applied with opposite
// sign on L vs R (AMY flips delay_scale per channel) for stereo width. The delayed
// signal is mixed on top of the dry at `level`.
//
// This reconstructs AMY's chorus from its exact parameters + linear-interp method,
// rather than byte-copying, because AMY generates the LFO with its oscillator engine
// (render_osc_wave on a dedicated CHORUS_MOD_SOURCE osc). Derived from AMY
// (https://github.com/shorepine/amy), MIT. See NOTICES.md.

#include <vector>
#include <cmath>
#include <algorithm>

namespace amyplug
{
class AmyChorus
{
public:
    AmyChorus() = default;

    void prepare(double sr)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        // AMY CHORUS_DEFAULT_MAX_DELAY = 320 samples at 44.1 kHz; keep the time at any SR.
        maxDelay = (float) (320.0 * sampleRate / 44100.0);
        const int need = (int) std::ceil(maxDelay) + 4;
        delayL.init(need);
        delayR.init(need);
        setParams(0.5f, 0.5f);   // AMY defaults: lfo_freq 0.5 Hz, depth 0.5
        reset();
    }

    void reset() { delayL.clear(); delayR.clear(); phase = 0.0f; }

    void setParams(float lfoFreqHz, float depth_)
    {
        lfoFreq = lfoFreqHz;
        depth   = juce_clamp(depth_, 0.0f, 1.0f);
    }

    // Mix the chorus into the L/R blocks in place at `level` (dry + level*wet).
    void processStereo(float* L, float* R, int n, float level)
    {
        const float inc    = lfoFreq / (float) sampleRate;
        const float center = maxDelay * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float tri = 1.0f - 4.0f * std::fabs(phase - 0.5f);   // triangle in [-1, 1]
            const float mod = depth * tri;
            const float dL  = juce_clamp(center * (1.0f + mod), 1.0f, maxDelay);
            const float dR  = juce_clamp(center * (1.0f - mod), 1.0f, maxDelay);   // opposite phase

            const float inL = L[i];
            L[i] = inL + level * delayL.readInterp(dL);
            delayL.write(inL);

            if (R)
            {
                const float inR = R[i];
                R[i] = inR + level * delayR.readInterp(dR);
                delayR.write(inR);
            }

            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    struct FracDelay
    {
        std::vector<float> buf;
        int mask = 0, w = 0;
        void init(int minLen) { int len = 1; while (len < minLen) len <<= 1; buf.assign((size_t) len, 0.0f); mask = len - 1; w = 0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
        inline float readInterp(float delaySamples) const
        {
            const float rp = (float) w - delaySamples;
            const int   i0 = (int) std::floor(rp);
            const float fr = rp - (float) i0;
            const float a  = buf[(size_t) (i0 & mask)];
            const float b  = buf[(size_t) ((i0 + 1) & mask)];
            return a + fr * (b - a);
        }
        inline void write(float v) { buf[(size_t) w] = v; w = (w + 1) & mask; }
    };

    static float juce_clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    double sampleRate = 48000.0;
    FracDelay delayL, delayR;
    float maxDelay = 320.0f, lfoFreq = 0.5f, depth = 0.5f, phase = 0.0f;
};
} // namespace amyplug
