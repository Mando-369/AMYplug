// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// Dx7Osc — DX7 operator frequency + output-level conversions, ported from AMY's
// reference converter amy/fm.py. Operators are stored the DX7 way (Coarse 0..31,
// Fine 0..99, Detune 0..14 centre 7, Output Level 0..99); these turn those into the
// AMY wire values emitFm needs (a harmonic ratio, or an absolute Hz for fixed
// operators, plus the operator amplitude / modulation index).
//
// Using the DX7's OWN ranges is deliberate: it keeps every emitted frequency and
// modulation depth inside the envelope AMY can render (AMY plays the entire factory
// DX7 bank cleanly), instead of arbitrary editor ranges that can push AMY's
// oscillator tables out of bounds.

#include <cmath>
#include <juce_core/juce_core.h>

namespace amyplug::dx7osc
{
// coarse_fine_ratio: harmonic ratio. Coarse 0 is the 0.5 sub-octave special case;
// Fine is percent, Detune nudges by (detune-7)/8 percent.
inline double coarseFineRatio(int coarse, int fine, int detune)
{
    coarse &= 31;
    const double c = (coarse == 0) ? 0.5 : (double) coarse;
    return c * (1.0 + (fine + (detune - 7) / 8.0) / 100.0);
}
// coarse_fine_fixed_hz: fixed-frequency operators, 10^(coarse + cents). Coarse 0..3.
inline double coarseFineFixedHz(int coarse, int fine, int detune)
{
    coarse &= 3;
    return std::pow(10.0, coarse + (fine + (detune - 7) / 8.0) / 100.0);
}
// Output Level 0..99 -> operator amplitude (modulation index). fm.py uses
// 2 * dx7level_to_linear(level); level 99 -> 2.0 (the DX7 maximum).
inline double outputLevelToAmp(int level) { return 2.0 * std::pow(2.0, (level - 99) / 8.0); }
inline int    ampToOutputLevel(double amp)
{
    if (amp <= 0.0) return 0;
    return juce::jlimit(0, 99, (int) std::lround(99.0 + 8.0 * std::log2(amp / 2.0)));
}

// --- Inverses (used to decode a baked factory patch, which carries the already
//     computed ratio / Hz, back into DX7-native coarse/fine/detune). Factory ratios
//     were produced from integer coarse/fine/detune, so the search recovers them.
struct CoarseFineDetune { int coarse, fine, detune; };

// Split the fractional "x = fine + (detune-7)/8" (percent units) into fine + detune.
inline void splitFineDetune(double x, int& fine, int& detune)
{
    fine   = juce::jlimit(0, 99, (int) std::lround(x));
    detune = juce::jlimit(0, 14, (int) std::lround((x - fine) * 8.0) + 7);
}
inline CoarseFineDetune ratioToCoarseFineDetune(double ratio)
{
    CoarseFineDetune r {};
    r.coarse = (ratio < 0.75) ? 0 : juce::jlimit(1, 31, (int) std::lround(ratio));
    const double base = (r.coarse == 0) ? 0.5 : (double) r.coarse;
    splitFineDetune((ratio / base - 1.0) * 100.0, r.fine, r.detune);
    return r;
}
inline CoarseFineDetune fixedHzToCoarseFineDetune(double hz)
{
    CoarseFineDetune r {};
    const double lg = (hz > 0.0) ? std::log10(hz) : 0.0;
    r.coarse = juce::jlimit(0, 3, (int) std::floor(lg));
    splitFineDetune((lg - r.coarse) * 100.0, r.fine, r.detune);
    return r;
}

// Key Velocity Sensitivity (0..7) -> AMY amp `vel` coef. AMY's amp works out to
// amp = velocity^coef, so coef 1.0 is already a LINEAR velocity response. We scale by
// kVelSensMax (< 1) for a gentler, more DX7-like feel: at max KVS a soft note stays
// audible (velocity^0.5) instead of dropping off steeply, which also avoids the
// fast-attack "onset delay" from AMY's amplitude floor.
inline constexpr double kVelSensMax = 0.5;
inline double velSensToCoef(int kvs) { return juce::jlimit(0, 7, kvs) / 7.0 * kVelSensMax; }
inline int    coefToVelSens(double coef) { return juce::jlimit(0, 7, (int) std::lround(coef / kVelSensMax * 7.0)); }
} // namespace amyplug::dx7osc
