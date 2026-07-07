// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// EnvelopeFollower — a simple peak envelope follower (one-pole attack/release) that
// tracks the input level so it can drive the filter cutoff. This is what makes the
// VCF useful as an *insert* (an FX plugin has audio, not note-ons): louder input
// opens the filter. Pure float, RT-safe, per-instance.

#include <cmath>
#include <algorithm>

namespace amyplug
{
class EnvelopeFollower
{
public:
    EnvelopeFollower() = default;

    void prepare(double sr) { sampleRate = sr > 1.0 ? sr : 48000.0; recalc(); env = 0.0f; }
    void reset() { env = 0.0f; }

    void setTimesMs(float attackMs, float releaseMs)
    {
        atkMs = attackMs; relMs = releaseMs; recalc();
    }

    // Advance the follower over a block, returning the PEAK envelope reached (≈0..1).
    // Peak-within-block so fast transients still open the filter for that block.
    float processBlockPeak(const float* data, int n)
    {
        float peak = env;
        for (int i = 0; i < n; ++i)
        {
            const float r = std::fabs(data[i]);
            const float c = (r > env) ? aCoef : rCoef;   // attack rising, release falling
            env = c * (env - r) + r;                     // one-pole toward |x|
            if (env > peak) peak = env;
        }
        return peak;
    }

private:
    void recalc() { aCoef = coef(atkMs); rCoef = coef(relMs); }
    float coef(float ms) const
    {
        return (float) std::exp(-1.0 / (std::max(0.05, (double) ms) * 0.001 * sampleRate));
    }

    double sampleRate = 48000.0;
    float  atkMs = 5.0f, relMs = 80.0f;
    float  aCoef = 0.0f, rCoef = 0.0f, env = 0.0f;
};
} // namespace amyplug
