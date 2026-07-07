// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyBiquad — the RBJ/AMY biquad coefficient generators, transcribed to float from
// AMY's filters.c (dsps_biquad_gen_lpf/hpf/bpf). Shared by AmyFilter (the VCF) and
// AmyEq (the 3-band bus EQ). F2S = identity in AMY's float mode, so these are the
// coefficients AMY computes, verbatim (including its sign conventions).
//
// Derived from AMY (https://github.com/shorepine/amy), MIT. See NOTICES.md.
// coeffs layout: [ b0/a0 (sign per AMY), b1/a0, b2/a0, a1/a0, a2/a0 ], f = freq/SR.

#include <cmath>
#include <algorithm>

namespace amyplug::biquad
{
inline constexpr float kPi = 3.14159265358979323846f;

inline void genLpf(float* c, float f, float q)
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

inline void genHpf(float* c, float f, float q)
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

inline void genBpf(float* c, float f, float q)
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
} // namespace amyplug::biquad
