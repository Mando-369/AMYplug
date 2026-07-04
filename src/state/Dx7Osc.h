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
// Detune -> a proportional pitch offset in cents. The REAL DX7 detune spans only
// +/-2 cents over its +/-7 range (DX7II manual; Dexed issue #88), applied relative to
// the note pitch. AMY's fm.py instead uses (detune-7)/8 *percent* (~+/-15 cents), which
// turns micro-detuned patches (DX7 BRASS 2 uses the full range) into a heavy amplitude
// beating the hardware never had. We model the hardware: a gentle chorus, not a pump.
inline constexpr double kDetuneCentsMax = 2.0;   // cents at detune = +/-7 (real DX7)
inline double detuneCents(int detune) { return (detune - 7) * (kDetuneCentsMax / 7.0); }

// coarse_fine_ratio: harmonic ratio. Coarse 0 is the 0.5 sub-octave special case;
// Fine is percent; Detune is the real DX7 +/-2 cents (see above).
inline double coarseFineRatio(int coarse, int fine, int detune)
{
    coarse &= 31;
    const double c = (coarse == 0) ? 0.5 : (double) coarse;
    return c * (1.0 + fine / 100.0) * std::pow(2.0, detuneCents(detune) / 1200.0);
}
// coarse_fine_fixed_hz: fixed-frequency operators, 10^(coarse + fine%) with the same
// real-DX7 +/-2-cent detune applied on top. Coarse 0..3.
inline double coarseFineFixedHz(int coarse, int fine, int detune)
{
    coarse &= 3;
    return std::pow(10.0, coarse + fine / 100.0) * std::pow(2.0, detuneCents(detune) / 1200.0);
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

// Split the fractional "x = fine + (detune-7)/8" (percent units, as AMY's fm.py bakes
// factory patches) into fine + detune. A DX7's micro-detuning belongs in DETUNE, not a
// whole fine step, so keep fine at 0 whenever the offset fits detune's +/-7/8 percent
// reach; fine takes only the surplus. (Recovers e.g. BRASS 2's detune 14 rather than
// mis-reading it as fine 1 + detune 6 — which would dodge the real +/-2-cent curve.)
inline void splitFineDetune(double x, int& fine, int& detune)
{
    const double f = (std::abs(x) <= 0.875 + 1e-6) ? 0.0 : std::round(x);
    fine   = juce::jlimit(0, 99, (int) f);
    detune = juce::jlimit(0, 14, (int) std::lround((x - f) * 8.0) + 7);
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

// Key Velocity Sensitivity (0..7) done the DX7 way: velocity scales the operator's
// OUTPUT LEVEL (its amp constant), applied by us at note-on. We do NOT use AMY's amp
// COEF_VEL — in the ALGO engine every operator is an algo_source that never receives
// the note velocity (its COEF_VEL input is 0), so a velocity coef there collapses the
// operator to silence. Scaling the level instead is exactly the DX7 model: on a carrier
// it is loudness, on a MODULATOR it is FM index -> brightness (harder = brighter), the
// core DX7 velocity feel. Returns a 0..1 multiplier on the baked operator amp:
//   velSens 0 -> 1.0 always (no velocity effect); 7 -> full range (soft notes attenuate
//   toward kVelFloor). velNorm is the MIDI velocity 0..1.
inline constexpr double kVelFloor = 0.06;   // a max-KVS soft note keeps a little level
inline double velLevelScale(double velNorm, int velSens)
{
    const double depth = juce::jlimit(0, 7, velSens) / 7.0;         // 0..1
    velNorm = juce::jlimit(0.0, 1.0, velNorm);
    const double soft = kVelFloor + (1.0 - kVelFloor) * velNorm;    // velNorm 0 -> floor
    return (1.0 - depth) * 1.0 + depth * soft;                      // velSens 0 -> 1.0
}

// (Legacy: KVS <-> AMY amp vel coef. Kept only so decoding an old/edge patch that put a
// value in COEF_VEL still round-trips a velSens number; emit no longer uses COEF_VEL.)
inline constexpr double kVelSensMax = 0.5;
inline double velSensToCoef(int kvs) { return juce::jlimit(0, 7, kvs) / 7.0 * kVelSensMax; }
inline int    coefToVelSens(double coef) { return juce::jlimit(0, 7, (int) std::lround(coef / kVelSensMax * 7.0)); }
} // namespace amyplug::dx7osc
