// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyFilter — the AMY analog VCF, extracted from AMY's filters.c and ported to
// float for use as a standalone, per-instance DSP block (no AMY engine, no global
// state). This is the same filter the AMYplug instrument's Juno voice runs.
//
// Derived from AMY (https://github.com/shorepine/amy) — Copyright (c) DAn Ellis,
// Brian Whitman, Shore Pine Sound Systems — MIT licensed. AMY builds this filter
// in fixed-point with block-floating-point headroom management; that machinery
// exists only to keep the microcontroller's integer math from overflowing, so in
// AMY's own float mode (amy_fixedpoint.h) it collapses to the plain biquad math
// transcribed here. The coefficient generation and DF-I split-feedback topology
// are copied verbatim (including AMY's sign conventions), so the response matches.
// The 24 dB/oct LPF is AMY's `_twice` cascade — the Juno slope. See NOTICES.md.

#include <cmath>
#include <algorithm>

namespace amyplug
{
class AmyFilter
{
public:
    // Filter types mirror AMY's constants (amy.h): 0 none, 1 LPF(12), 2 BPF, 3 HPF, 4 LPF24.
    enum Type { None = 0, LPF = 1, BPF = 2, HPF = 3, LPF24 = 4 };

    AmyFilter() = default;

    void prepare(double sr) { sampleRate = sr > 1.0 ? sr : 48000.0; reset(); }
    void reset() { for (auto& s : w) s = 0.0f; }

    // Filter one mono block in place. cutoff in Hz, resonance = Q. (Use one instance
    // per channel — the delay memories are per-channel, exactly as AMY is per-osc.)
    void process(float* block, int n, float cutoffHz, float resonance, int type)
    {
        if (type == None) return;

        // Switching type swaps both the coefficients and the state layout (LPF24 uses
        // 6 memories, the others 4). Feeding stale state into a different topology can
        // send the resonant recurrence unstable, so clear the memory on any type change.
        if (type != lastType) { reset(); lastType = type; }

        // AMY: ratio = freq / SR, floored at LOWEST_RATIO (~4.4 Hz), f capped at 0.45 in gen.
        float ratio = (float) (cutoffHz / sampleRate);
        if (ratio < kLowestRatio) ratio = kLowestRatio;

        float coeffs[5];
        if (type == LPF || type == LPF24) genLpf(coeffs, ratio, resonance);
        else if (type == BPF)             genBpf(coeffs, ratio, resonance);
        else if (type == HPF)             genHpf(coeffs, ratio, resonance);
        else return;

        if (type == LPF24) biquadSplitFbTwice(block, block, n, coeffs, w);   // 24 dB/oct (Juno)
        else               biquadSplitFb     (block, block, n, coeffs, w);   // 12 dB/oct

        // Safety net: if the filter ever goes non-finite (an unstable coefficient jump,
        // extreme resonance), a NaN/Inf in the state would propagate forever. Detect it,
        // clear the state, and silence this block so nothing bad reaches the rest of the
        // chain — the next block starts clean.
        for (float s : w)
            if (! std::isfinite(s))
            {
                reset();
                for (int i = 0; i < n; ++i) block[i] = 0.0f;
                break;
            }
    }

private:
    static constexpr float kLowestRatio = 0.0001f;   // AMY LOWEST_RATIO
    static constexpr float kPi = 3.14159265358979323846f;

    // --- coefficient generators (verbatim from filters.c, F2S = identity in float) ---
    static void genLpf(float* c, float f, float q)
    {
        if (q < 0.51f) q = 0.51f;
        if (f > 0.45f) f = 0.45f;
        float w0 = 2.0f * kPi * f;                 // Fs = 1
        w0 = std::max(0.01f, w0);
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float b0 = (1.0f - cw) * 0.5f, b1 = 1.0f - cw, b2 = b0;
        const float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
        c[0] = -b0 / a0; c[1] = -b1 / a0; c[2] = -b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
    }
    static void genHpf(float* c, float f, float q)
    {
        if (q <= 0.0001f) q = 0.0001f;
        if (f > 0.45f) f = 0.45f;
        const float w0 = 2.0f * kPi * f;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float b0 = (1.0f + cw) * 0.5f, b1 = -(1.0f + cw), b2 = b0;
        const float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
        c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
    }
    static void genBpf(float* c, float f, float q)
    {
        if (q <= 0.0001f) q = 0.0001f;
        if (f > 0.45f) f = 0.45f;
        const float w0 = 2.0f * kPi * f;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float b0 = sw * 0.5f, b1 = 0.0f, b2 = -b0;
        const float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
        c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
    }

    // --- 12 dB/oct: DF-I with split feedback (a1 = -2 + e, a2 = 1 - f) ---
    static void biquadSplitFb(const float* in, float* out, int len, const float* coef, float* w)
    {
        float x1 = w[0], x2 = w[1], y1 = w[2], y2 = w[3];
        const float e = 2.0f + coef[3];
        const float f = 1.0f - coef[4];
        for (int i = 0; i < len; ++i)
        {
            const float x0 = in[i];
            const float w0 = coef[0] * x0 + coef[1] * x1 + coef[2] * x2;
            float y0 = w0 + 2.0f * y1 - y2;
            y0 = y0 - e * y1 + f * y2;
            x2 = x1; x1 = x0; y2 = y1; y1 = y0;
            out[i] = y0;
        }
        w[0] = x1; w[1] = x2; w[2] = y1; w[3] = y2;
    }

    // --- 24 dB/oct (Juno): the same LPF biquad cascaded twice (AMY `_twice`) ---
    static void biquadSplitFbTwice(const float* in, float* out, int len, const float* coef, float* w)
    {
        float x1 = w[0], x2 = w[1], y1 = w[2], y2 = w[3], v1 = w[4], v2 = w[5];
        const float a = coef[0];                  // LPF: b1 = 2 b0, b2 = b0 → numerator = a(x + 2x1 + x2)
        const float e = 2.0f + coef[3];
        const float f = 1.0f - coef[4];
        for (int i = 0; i < len; ++i)
        {
            const float x0 = a * in[i];
            float w0 = x0 + 2.0f * x1 + x2;       // stage 1 numerator
            float v0 = w0 + 2.0f * v1 - v2;
            v0 = v0 - e * v1 + f * v2;            // stage 1 poles
            w0 = v0 + 2.0f * v1 + v2;             // stage 2 numerator
            w0 = a * w0;
            float y0 = w0 + 2.0f * y1 - y2;
            y0 = y0 - e * y1 + f * y2;            // stage 2 poles
            x2 = x1; x1 = x0; v2 = v1; v1 = v0; y2 = y1; y1 = y0;
            out[i] = y0;
        }
        w[0] = x1; w[1] = x2; w[2] = y1; w[3] = y2; w[4] = v1; w[5] = v2;
    }

    double sampleRate = 48000.0;
    float  w[8] = { 0 };    // biquad delay memories (LPF24 uses 6, others 4)
    int    lastType = -1;   // detect a type switch → clear state (different topology)
};
} // namespace amyplug
