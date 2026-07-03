// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// Dx7Envelope — the DX7 4-rate / 4-level operator envelope, converted to and from
// AMY's breakpoint form, faithfully porting AMY's own fm.py (calc_loglin_eg_breakpoints).
//
// A DX7 EG is R1..R4 + L1..L4 (each 0..99). AMY renders it as 5 (time_ms, linear_level)
// breakpoints: start at L4 (the release floor), attack to L1, decay to L2, L3, then
// release back to L4. `egToBreakpoints` is the forward (for emit); `breakpointsToEg`
// inverts it (for decoding factory patches) — the inverse is exact because each segment
// time is monotonic in its rate.

#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdio>

namespace amyplug::dx7env
{
// DX7 level 0..99 <-> linear amplitude (99 -> 1.0). Inverse clamps to the 0-level floor.
inline double levelToLinear(double lvl) { return std::pow(2.0, (lvl - 99.0) / 8.0); }
inline double linearToLevel(double lin)
{
    const double floor = levelToLinear(0.0);
    return juce::jlimit(0.0, 99.0, std::log2(std::max(floor, lin)) * 8.0 + 99.0);
}

// Segment timing (seconds) for one rate to move between two DX7 levels. Attack is a
// rising exponential; decay/release is linear in the log-amp (DX7 level) domain.
inline constexpr double kMinLevel = 34.0, kAttackRange = 75.0;
inline double attackTimeAtLevel(double level, double tConst)
{
    const double l = std::max(kMinLevel, level);
    return -tConst * std::log((kMinLevel + kAttackRange - l) / kAttackRange);
}
inline double attackSeconds(double rate, double from, double to)
{
    const double tConst = 0.008 * std::pow(2.0, (65.0 - rate) / 6.0);
    return std::max(0.0, attackTimeAtLevel(to, tConst) - attackTimeAtLevel(from, tConst));
}
inline double decayPerSec(double rate) { return 0.5 + 0.5 * std::pow(2.0, rate / 6.0); }

// Duration of one segment. `isRelease` reproduces fm.py's goose: a release segment that
// doesn't move (sustain==0 held to 0) is given a 60-level fall so it isn't zero-length.
inline double segSeconds(double rate, double from, double to, bool isRelease)
{
    if (to > from) return attackSeconds(rate, from, to);
    double diff = from - to;
    if (isRelease && diff == 0.0) diff = 60.0;
    return diff / decayPerSec(rate);
}

// Inverse: the rate (0..99) whose segment from->to lasts `durationSec`. Monotonic
// decreasing in rate, so a binary search converges. Same-level non-release segments are
// zero-length regardless of rate — return the fastest.
inline double rateForSegment(double from, double to, double durationSec, bool isRelease)
{
    if (to == from && ! isRelease) return 99.0;
    double lo = 0.0, hi = 99.0;
    for (int it = 0; it < 48; ++it)
    {
        const double mid = 0.5 * (lo + hi);
        // Higher rate -> shorter time. Time too long -> need a higher rate.
        if (segSeconds(mid, from, to, isRelease) > durationSec) lo = mid; else hi = mid;
    }
    return juce::jlimit(0.0, 99.0, 0.5 * (lo + hi));
}

// Forward (allocation-free): 4R/4L -> "<ms>,<lin>,<ms>,<lin>,..." into `out`. Returns
// the length. Safe to call on the audio thread (used by live streaming).
inline int egToBreakpointsC(char* out, int cap, const float rate[4], const float level[4])
{
    int n = std::snprintf(out, (size_t) cap, "0,%.6g", levelToLinear(level[3]));
    double cur = level[3];
    for (int i = 0; i < 4 && n < cap; ++i)
    {
        const double sec = segSeconds(rate[i], cur, level[i], i == 3);
        n += std::snprintf(out + n, (size_t) (cap - n), ",%d,%.6g",
                           (int) std::lround(sec * 1000.0), levelToLinear(level[i]));
        cur = level[i];
    }
    return n;
}

// Forward: 4R/4L -> AMY breakpoint string "<ms>,<lin>,<ms>,<lin>,..." (5 pairs).
inline juce::String egToBreakpoints(const float rate[4], const float level[4])
{
    char b[128]; egToBreakpointsC(b, (int) sizeof b, rate, level);
    return juce::String(b);
}

// --- Pitch EG (the ALGO osc's bp0) -----------------------------------------------
// The pitch envelope uses a DIFFERENT level map and segment timing than the amp EG
// (fm.py: dx7_attacks=False, rate_double_interval=20, rate_scale=11, rate_offset=-6),
// and its levels are pitch *ratios* (DX7 pitch value 50 -> ratio 1.0 = no shift).
inline double pitchvalToRatio(double pitchval)
{
    const double sign = (pitchval >= 50.0) ? 1.0 : -1.0;
    double semi = std::abs(pitchval - 50.0);
    if (semi > 36.0) semi += (semi - 34.0) * (semi - 34.0) * 93.0 / 225.0 - semi + 34.0;
    return std::pow(2.0, (sign * semi) / 32.0);
}
inline double ratioToPitchval(double ratio)
{
    double semi = 32.0 * std::log2(std::max(1e-9, ratio));
    const double sign = (semi >= 0.0) ? 1.0 : -1.0;
    semi = std::abs(semi);
    if (semi > 36.0) semi += 34.0 + std::sqrt(std::abs(semi - 34.0) * 225.0 / 93.0) - semi;
    return juce::jlimit(0.0, 99.0, 50.0 + sign * semi);
}
inline double pitchSegSeconds(double rate, double from, double to, bool isRelease)
{
    const double dir = (to > from) ? 1.0 : -1.0;
    double diff = to - from;
    if (isRelease && diff == 0.0) diff = dir * 60.0;
    const double lcps = dir * (-6.0 + 11.0 * std::pow(2.0, rate / 20.0));
    return (lcps != 0.0) ? diff / lcps : 0.0;
}
inline double pitchRateForSegment(double from, double to, double durationSec, bool isRelease)
{
    if (to == from && ! isRelease) return 99.0;
    double lo = 0.0, hi = 99.0;
    for (int it = 0; it < 48; ++it)
    {
        const double mid = 0.5 * (lo + hi);
        if (pitchSegSeconds(mid, from, to, isRelease) > durationSec) lo = mid; else hi = mid;
    }
    return juce::jlimit(0.0, 99.0, 0.5 * (lo + hi));
}
inline int pitchEgToBreakpointsC(char* out, int cap, const float rate[4], const float level[4])
{
    // AMY adds the pitch-env value directly to log2(freq); the operators then multiply
    // that base, so a large value pushes every operator past AMY's freq range (OOB ->
    // abort). The neutral value 1.0 (the +1-octave base) is proven safe, so keep the
    // env in a tight band around it. Real envelopes sit near 1.0; a deep sweep clamps.
    auto R = [] (double v) { return juce::jlimit(0.5, 2.0, pitchvalToRatio(v)); };
    int n = std::snprintf(out, (size_t) cap, "0,%.6g", R(level[3]));
    double cur = level[3];
    for (int i = 0; i < 4 && n < cap; ++i)
    {
        const double sec = pitchSegSeconds(rate[i], cur, level[i], i == 3);
        n += std::snprintf(out + n, (size_t) (cap - n), ",%d,%.6g",
                           (int) std::lround(sec * 1000.0), R(level[i]));
        cur = level[i];
    }
    return n;
}
inline juce::String pitchEgToBreakpoints(const float rate[4], const float level[4])
{
    char b[128]; pitchEgToBreakpointsC(b, (int) sizeof b, rate, level);
    return juce::String(b);
}
inline bool breakpointsToPitchEg(const juce::String& bp, float rate[4], float level[4])
{
    juce::StringArray p; p.addTokens(bp, ",", "");
    if (p.size() < 10) return false;
    const double L4 = ratioToPitchval(p[1].getDoubleValue());
    double cur = L4;
    for (int i = 0; i < 4; ++i)
    {
        const double tms = p[2 + i * 2].getDoubleValue();
        const double lvl = ratioToPitchval(p[3 + i * 2].getDoubleValue());
        level[i] = (float) lvl;
        rate[i]  = (float) pitchRateForSegment(cur, lvl, tms / 1000.0, i == 3);
        cur = lvl;
    }
    level[3] = (float) L4;
    return true;
}

// Inverse: an AMY breakpoint string (>=5 pairs) -> 4R/4L. Returns false if malformed.
inline bool breakpointsToEg(const juce::String& bp, float rate[4], float level[4])
{
    juce::StringArray p; p.addTokens(bp, ",", "");
    if (p.size() < 10) return false;
    const double L4 = linearToLevel(p[1].getDoubleValue());   // pair 0 = (0, L4)
    double cur = L4;
    for (int i = 0; i < 4; ++i)
    {
        const double tms = p[2 + i * 2].getDoubleValue();
        const double lvl = linearToLevel(p[3 + i * 2].getDoubleValue());
        level[i] = (float) lvl;
        rate[i]  = (float) rateForSegment(cur, lvl, tms / 1000.0, i == 3);
        cur = lvl;
    }
    level[3] = (float) L4;   // release target is the floor
    return true;
}
} // namespace amyplug::dx7env
