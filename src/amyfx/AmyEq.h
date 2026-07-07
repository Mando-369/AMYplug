// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyEq — AMY's 3-band bus EQ (parametric_eq_process from filters.c), transcribed
// to float as a standalone per-instance block. Three fixed-frequency biquads —
// low LPF @ 800 Hz (Q 0.707), mid BPF @ 2500 Hz (Q 1.0), high HPF @ 7000 Hz
// (Q 0.707) — each scaled by its band gain and recombined as low - mid + high,
// exactly as AMY does. The band frequencies/Qs are fixed; the user controls the
// three gains. Same centers as the AMYplug instrument's EQ, so it matches.
//
// Derived from AMY (https://github.com/shorepine/amy), MIT. See NOTICES.md.

#include "AmyBiquad.h"

namespace amyplug
{
class AmyEq
{
public:
    AmyEq() = default;

    void prepare(double sr)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        biquad::genLpf(coeffs[0], (float) (800.0  / sampleRate), 0.707f);
        biquad::genBpf(coeffs[1], (float) (2500.0 / sampleRate), 1.000f);
        biquad::genHpf(coeffs[2], (float) (7000.0 / sampleRate), 0.707f);
        reset();
    }
    void reset()
    {
        x1 = x2 = 0.0f;
        for (auto& b : y) { b[0] = 0.0f; b[1] = 0.0f; }
    }

    // Band gains are LINEAR multipliers (1.0 = flat). Process one mono block in place.
    // The three FIR numerators exploit each band's known zero structure (LP: x+2x1+x2,
    // BP: x-x2, HP: x-2x1+x2); poles are fixed, so this is unconditionally stable.
    void process(float* block, int n, float gLow, float gMid, float gHigh)
    {
        const float c00 = gLow  * coeffs[0][0], c03 = coeffs[0][3], c04 = coeffs[0][4];
        const float c10 = gMid  * coeffs[1][0], c13 = coeffs[1][3], c14 = coeffs[1][4];
        const float c20 = gHigh * coeffs[2][0], c23 = coeffs[2][3], c24 = coeffs[2][4];

        float lx1 = x1, lx2 = x2;
        float y01 = y[0][0], y02 = y[0][1];
        float y11 = y[1][0], y12 = y[1][1];
        float y21 = y[2][0], y22 = y[2][1];

        for (int i = 0; i < n; ++i)
        {
            const float x0   = block[i];
            const float x1t2 = 2.0f * lx1;
            const float y00 = c00 * (x0 + x1t2 + lx2) - c03 * y01 - c04 * y02;   // low  (LPF)
            const float y10 = c10 * (x0 - lx2)        - c13 * y11 - c14 * y12;   // mid  (BPF)
            const float y20 = c20 * (x0 - x1t2 + lx2) - c23 * y21 - c24 * y22;   // high (HPF)
            lx2 = lx1; lx1 = x0;
            y02 = y01; y01 = y00;
            y12 = y11; y11 = y10;
            y22 = y21; y21 = y20;
            block[i] = y00 - y10 + y20;
        }

        x1 = lx1; x2 = lx2;
        y[0][0] = y01; y[0][1] = y02;
        y[1][0] = y11; y[1][1] = y12;
        y[2][0] = y21; y[2][1] = y22;
    }

private:
    double sampleRate = 48000.0;
    float  coeffs[3][5] = { { 0 } };   // low/mid/high biquad coeffs (fixed)
    float  x1 = 0.0f, x2 = 0.0f;       // shared input memory
    float  y[3][2] = { { 0 } };        // per-band output memory
};
} // namespace amyplug
