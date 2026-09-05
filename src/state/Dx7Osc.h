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
// Coarse/Fine/Detune -> the fractional pitch offset, VERBATIM from AMY's fm.py
// (`coarse_fine_ratio` / `coarse_fine_fixed_hz`): fine + (detune-7)/8, as a PERCENT.
// This must match fm.py exactly, because fm.py generated AMY's baked factory bank:
// decoding a factory preset and re-emitting through this reproduces its ratio
// bit-for-bit, so "To Editor" sounds identical to Factory mode (the same holds for
// .syx imports, whose integer coarse/fine/detune are the DX7's own).
//
// (A prior build modelled the DX7II's "true" +/-2-cent detune here instead. But that
// diverged every micro-detuned factory patch — DX7 BRASS 2 uses the full detune range
// — from the factory sound: ~8x less detune slows the operator beat into a phaser-like
// drift. Matching fm.py is what the user hears as correct, so hardware-authenticity
// yields to factory-parity.)
inline double detuneFraction(int fine, int detune) { return (fine + (detune - 7) / 8.0) / 100.0; }

// coarse_fine_ratio: harmonic ratio. Coarse 0 is the 0.5 sub-octave special case.
inline double coarseFineRatio(int coarse, int fine, int detune)
{
    coarse &= 31;
    const double c = (coarse == 0) ? 0.5 : (double) coarse;
    return c * (1.0 + detuneFraction(fine, detune));
}
// coarse_fine_fixed_hz: fixed-frequency operators, 10^(coarse + (fine + detune)%). Coarse 0..3.
inline double coarseFineFixedHz(int coarse, int fine, int detune)
{
    coarse &= 3;
    return std::pow(10.0, coarse + detuneFraction(fine, detune));
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
    // ⚠️ The epsilon must absorb a float32 round-trip. Factory ratios reach us through
    // juce::String::getFloatValue(), and 0.99125 as a float is 0.9912499785 — so x is
    // -0.8750021, which a 1e-6 window around 0.875 MISSES by two millionths. It then took
    // the fine branch: round(-0.875) = -1, which jlimit clipped to fine 0 while detune was
    // still computed against -1, giving detune 8 (+0.125%) for what is detune 0 (-0.875%).
    // The operator's detune INVERTED. On DX7 E.PIANO 1, where two near-unison operators
    // beat against each other, that is heard as phasiness and a lost transient after
    // "To Editor" — measured 82% mean-abs against the same preset played from the browser.
    const double f = (std::abs(x) <= 0.875 + 1e-4) ? 0.0 : std::round(x);
    // Clamp fine FIRST and derive detune from the CLAMPED value: otherwise a negative
    // round() is clipped here while detune is still computed from the raw one, and the two
    // stop describing the same ratio.
    const double fc = juce::jlimit(0.0, 99.0, f);
    fine   = (int) fc;
    detune = juce::jlimit(0, 14, (int) std::lround((x - fc) * 8.0) + 7);
}
// ⚠️ SEARCH, do not invert arithmetically. fm.py's forward map is
//     ratio = coarse * (1 + (fine + (detune-7)/8) / 100),  coarse 0 meaning 0.5
// and coarse is genuinely AMBIGUOUS: 3.14 is not coarse 3 (which cannot reach it — the
// nearest is 3.13875) but coarse 2, fine 57, detune 7, i.e. 2 x 1.57. Picking coarse by
// round() or floor() therefore mis-reads part of the bank: 43 of the 752 factory operator
// ratios came back wrong, the worst by 32%. Trying every coarse and keeping the closest
// exact triple recovers all of them, because the bank was GENERATED from integer
// coarse/fine/detune in the first place. ~64 candidates, at decode time only.
inline CoarseFineDetune ratioToCoarseFineDetune(double ratio)
{
    CoarseFineDetune best { 1, 0, 7 };
    double bestErr = 1.0e30;
    for (int coarse = 0; coarse <= 31; ++coarse)
    {
        const double base = (coarse == 0) ? 0.5 : (double) coarse;
        const double x = (ratio / base - 1.0) * 100.0;      // = fine + (detune-7)/8
        if (x < -1.0 || x > 100.0) continue;                // outside what fine+detune spans
        for (int fine = juce::jmax(0, (int) std::floor(x)); fine <= juce::jmin(99, (int) std::ceil(x)); ++fine)
        {
            const int detune = juce::jlimit(0, 14, (int) std::lround((x - fine) * 8.0) + 7);
            const double err = std::abs(coarseFineRatio(coarse, fine, detune) - ratio);
            if (err < bestErr) { bestErr = err; best = { coarse, fine, detune }; }
        }
    }
    return best;
}
// Same search, for fixed-frequency operators: hz = 10^(coarse + (fine + (detune-7)/8)/100).
inline CoarseFineDetune fixedHzToCoarseFineDetune(double hz)
{
    CoarseFineDetune best { 0, 0, 7 };
    double bestErr = 1.0e30;
    const double lg = (hz > 0.0) ? std::log10(hz) : 0.0;
    for (int coarse = 0; coarse <= 3; ++coarse)
    {
        const double x = (lg - coarse) * 100.0;
        if (x < -1.0 || x > 100.0) continue;
        for (int fine = juce::jmax(0, (int) std::floor(x)); fine <= juce::jmin(99, (int) std::ceil(x)); ++fine)
        {
            const int detune = juce::jlimit(0, 14, (int) std::lround((x - fine) * 8.0) + 7);
            const double err = std::abs(coarseFineFixedHz(coarse, fine, detune) - hz);
            if (err < bestErr) { bestErr = err; best = { coarse, fine, detune }; }
        }
    }
    return best;
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
