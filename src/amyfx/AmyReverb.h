// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyReverb — AMY's stereo reverb (stereo_reverb from delay.c), transcribed to
// float as a standalone stereo block. It's a Stautner-Puckette multichannel
// reverberator: 6 early-reflection allpass stages feeding 4 feedback delay lines
// mixed by a 4x4 Hadamard matrix, each delay damped by a 1-pole lowpass. Same
// delay tunings and topology as AMY, so it's the AMYplug bus reverb.
//
// Delay lengths are AMY's sample counts at 44.1 kHz, scaled to the host rate so the
// reverb *time* is identical at any sample rate. Buffers are pre-allocated in
// prepare() (RT-safe); processStereo() does no allocation.
//
// Derived from AMY (https://github.com/shorepine/amy), MIT. See NOTICES.md.
// NOTE: AMY's LPF() takes its filter state by value; the comment there calls it a
// "1-pole lowpass (exponential smoothing)", so we persist f1..f4 per-instance as
// intended, giving proper feedback-path damping.

#include <vector>
#include <cmath>
#include <algorithm>

namespace amyplug
{
class AmyReverb
{
public:
    AmyReverb() = default;

    void prepare(double sr)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        const double k = sampleRate / 44100.0;   // keep AMY's delay *times* at any SR
        auto sc = [k](int s) { return (int) std::lround((double) s * k); };

        // Feedback delays (AMY: 58.6/69.4/74.5/86.1 ms) and early reflections.
        delay[0].init(sc(2586));
        delay[1].init(sc(3062));
        delay[2].init(sc(3286));
        delay[3].init(sc(3798));
        ref[0].init(sc(3319));
        ref[1].init(sc(1920));
        ref[2].init(sc(1138));
        ref[3].init(sc(855));
        ref[4].init(sc(722));
        ref[5].init(sc(602));

        setParams(0.85f, 3000.0f, 0.5f);   // AMY defaults (liveness/xover/damping)
        reset();
    }

    void reset()
    {
        for (auto& d : delay) d.clear();
        for (auto& r : ref)   r.clear();
        f1 = f2 = f3 = f4 = 0.0f;
    }

    // size = liveness (0..1, decay length), damping = HF absorption (0..1).
    void setParams(float liveness_, float xoverHz, float damping)
    {
        liveness = liveness_;
        lpfcoef  = juce_clamp(6.2832f * xoverHz / (float) sampleRate, 0.0f, 1.0f);
        lpfgain  = 1.0f - damping;
    }

    // Add reverb at `level` into the L/R blocks in place (r_out = r_in + level*wet).
    // rBlock/lBlock each hold n samples; if mono, pass lBlock == nullptr.
    void processStereo(float* rBlock, float* lBlock, int n, float level)
    {
        for (int i = 0; i < n; ++i)
        {
            const float inR = rBlock[i];
            const float inL = lBlock ? lBlock[i] : inR;

            // Early reflections: allpass lattice (r_acc/l_acc), 0.0625 input scaling.
            float rAcc = 0.0625f * inR;
            float lAcc = 0.0625f * inL;
            for (int s = 0; s < 5; ++s)
            {
                ref[s].in(lAcc);
                const float dout = ref[s].out();
                lAcc = rAcc - dout;
                rAcc += dout;
            }
            ref[5].in(lAcc);
            lAcc = ref[5].out();

            // Feedback delays + damping, then the Hadamard mix matrix.
            float d1 = lpf(delay[0].out(), f1); d1 += rAcc;
            rBlock[i] = inR + level * d1;

            float d2 = lpf(delay[1].out(), f2); d2 += lAcc;
            if (lBlock) lBlock[i] = inL + level * d2;

            float d3 = lpf(delay[2].out(), f3);
            float d4 = lpf(delay[3].out(), f4);

            delay[0].in(d1 + d2 + d3 + d4);
            delay[1].in(d1 - d2 + d3 - d4);
            delay[2].in(d1 + d2 - d3 - d4);
            delay[3].in(d1 - d2 - d3 + d4);
        }
    }

private:
    struct DelayLine
    {
        std::vector<float> buf;
        int mask = 0, nextIn = 0, fixedDelay = 0;
        void init(int fixed)
        {
            fixedDelay = fixed;
            int len = 1; while (len < fixed + 1) len <<= 1;   // pow2 enclosing the delay
            buf.assign((size_t) len, 0.0f);
            mask = len - 1; nextIn = 0;
        }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); nextIn = 0; }
        inline float out(int extra = 0) const { return buf[(size_t) ((nextIn - fixedDelay - extra) & mask)]; }
        inline void  in(float v) { buf[(size_t) nextIn] = v; nextIn = (nextIn + 1) & mask; }
    };

    static float juce_clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // AMY LPF(): 1-pole smoothing + dry/smoothed crossfade, scaled by liveness/2.
    inline float lpf(float samp, float& state)
    {
        state += lpfcoef * (samp - state);
        return 0.5f * liveness * (state + lpfgain * (samp - state));
    }

    double sampleRate = 48000.0;
    DelayLine delay[4], ref[6];
    float f1 = 0, f2 = 0, f3 = 0, f4 = 0;     // per-delay damping state
    float liveness = 0.85f, lpfcoef = 0.4f, lpfgain = 0.5f;
};
} // namespace amyplug
