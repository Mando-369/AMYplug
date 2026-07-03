// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// Dx7Lfo — DX7 LFO parameter conversions, ported verbatim from AMY's reference
// converter `amy/fm.py`. The DX7 LFO is stored natively (Speed/PMD/AMD 0..99,
// Wave 0..5, PMS 0..7); these turn those into the AMY values emitFm needs:
//
//   * LFO Speed  -> LFO oscillator frequency in Hz         (lfoSpeedToHz)
//   * LFO Wave   -> AMY wave code                           (lfoWaveToAmy)
//   * PMS + PMD  -> pitch_lfo_amp (freq mod-coef, vibrato)  (pitchLfoAmp)
//   * AMD        -> amp_lfo_amp   (amp  mod-coef, tremolo)  (ampLfoAmp)
//
// The inverses (used when decoding a baked factory patch that only carries the
// already-converted AMY values back into DX7-native form) are exact enough that
// re-emitting reproduces the same AMY numbers — so recall stays faithful. For the
// PMS/PMD ambiguity (both scale the same product) factory decode fixes PMS = 7 and
// solves PMD; emit re-runs the same forward formula, so the round-trip is lossless.

#include <cmath>
#include <juce_core/juce_core.h>

namespace amyplug::dx7lfo
{
// AMY wave codes (amy.h): SINE 0, PULSE 1, SAW_DOWN 2, SAW_UP 3, TRIANGLE 4, NOISE 5.
inline constexpr int kAmySine = 0, kAmyPulse = 1, kAmySawDown = 2,
                     kAmySawUp = 3, kAmyTriangle = 4, kAmyNoise = 5;

// fm.py lfo_wave: DX7 wave byte -> AMY wave code.
//   0 Triangle, 1 Saw Down, 2 Saw Up, 3 Square, 4 Sine, 5 Sample&Hold.
inline int lfoWaveToAmy(int dx7wave)
{
    static const int map[6] = { kAmyTriangle, kAmySawDown, kAmySawUp,
                                kAmyPulse, kAmySine, kAmyNoise };
    return map[juce::jlimit(0, 5, dx7wave)];
}
inline int lfoAmyToDx7(int amyWave)
{
    switch (amyWave)
    {
        case kAmyTriangle: return 0;
        case kAmySawDown:  return 1;
        case kAmySawUp:    return 2;
        case kAmyPulse:    return 3;
        case kAmySine:     return 4;
        case kAmyNoise:    return 5;
        default:           return 0;
    }
}

// fm.py lfo_speed_to_hz (measured/linear fit from a TX802).
inline double lfoSpeedToHz(double speed)
{
    if (speed <= 0.0)  return 0.064;
    if (speed <= 64.0) return speed / 6.0;
    if (speed <= 85.0) return speed - 64.0 * 5.0 / 6.0;   // == speed - 53.333
    return 31.67 + (speed - 85.0) * 1.33;
}
// Inverse (piecewise), for factory decode. Returns a 0..99 speed whose forward
// mapping reproduces `hz`; common LFO rates land in the exactly-invertible /6 band.
inline double lfoHzToSpeed(double hz)
{
    if (hz <= 0.064)  return 0.0;
    if (hz <= 64.0 / 6.0) return hz * 6.0;                 // <= ~10.667 Hz
    if (hz <= 31.67)  return hz + 64.0 * 5.0 / 6.0;
    return juce::jmin(99.0, 85.0 + (hz - 31.67) / 1.33);
}

// fm.py dx7level_to_linear (also the amp-mod-depth map): 0..99 -> linear, 99 -> 1.
inline double dx7LevelToLinear(double lvl) { return std::pow(2.0, (lvl - 99.0) / 8.0); }
inline double linearToDx7Level(double lin)
{
    if (lin <= 0.0) return 0.0;
    return juce::jlimit(0.0, 99.0, 99.0 + 8.0 * std::log2(lin));
}

// Tremolo depth: AMD (0..99) -> amp mod-coef (amp_lfo_amp).
inline double ampLfoAmp(double amd)          { return dx7LevelToLinear(amd); }
inline double ampToAmd(double ampLfoAmp_)    { return linearToDx7Level(ampLfoAmp_); }

// Vibrato depth: PMS (0..7) + PMD (0..99) -> freq mod-coef (pitch_lfo_amp).
// fm.py: 0 for PMD<=0, else 0.6 * 1.7^(PMS-1) * PMD / 1188.
inline double pitchLfoAmp(int pms, double pmd)
{
    if (pmd <= 0.0) return 0.0;
    return 0.6 * std::pow(1.7, (double) pms - 1.0) * pmd / 1188.0;
}
// Inverse at a fixed PMS (factory decode uses PMS = 7): solve for PMD.
inline double pitchAmpToPmd(double pitchLfoAmp_, int pms)
{
    const double denom = 0.6 * std::pow(1.7, (double) pms - 1.0) / 1188.0;
    if (denom <= 0.0) return 0.0;
    return juce::jlimit(0.0, 99.0, pitchLfoAmp_ / denom);
}
inline constexpr int kFactoryDecodePms = 7;   // the PMS we assume when decoding factory patches
} // namespace amyplug::dx7lfo
