// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// WdfClipper tests — the output WDF diode saturator. No JUCE, no AMY: the clipper
// is a self-contained DSP class, so these run in the light Catch2 target.

#include <catch2/catch_test_macros.hpp>
#include "dsp/WdfClipper.h"

#include <cmath>
#include <vector>

using amyplug::WdfClipper;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Render a sine of the given amplitude/freq through the clipper at a fixed drive,
// returning the output. Discards a warm-up prefix so the DC-blocker and gain
// smoothers have settled before the caller measures anything.
std::vector<float> renderSine(double amp, double freqHz, double driveDb,
                              double sr = 48000.0, int frames = 9600)
{
    WdfClipper clip;
    clip.setOutputDb(0.0f);
    clip.setDriveDb((float) driveDb);
    clip.prepare(sr);

    std::vector<float> out((size_t) frames);
    const double w = 2.0 * kPi * freqHz / sr;
    for (int i = 0; i < frames; ++i)
    {
        float s = (float) (amp * std::sin(w * i));
        float* ch[1] = { &s };
        clip.process(ch, 1, 1);
        out[(size_t) i] = s;
    }
    return out;
}

double rms(const std::vector<float>& v, int from)
{
    double acc = 0.0; int n = 0;
    for (size_t i = (size_t) from; i < v.size(); ++i) { acc += (double) v[i] * v[i]; ++n; }
    return n > 0 ? std::sqrt(acc / n) : 0.0;
}

double peak(const std::vector<float>& v, int from)
{
    double p = 0.0;
    for (size_t i = (size_t) from; i < v.size(); ++i) p = std::max(p, (double) std::fabs(v[i]));
    return p;
}
} // namespace

TEST_CASE("WdfClipper passes silence through as silence")
{
    WdfClipper clip;
    clip.setDriveDb(6.0f);
    clip.prepare(48000.0);

    float zero = 0.0f;
    for (int i = 0; i < 2048; ++i) { float* ch[1] = { &zero }; clip.process(ch, 1, 1); zero = 0.0f; }
    REQUIRE(std::fabs(zero) < 1.0e-6f);
}

TEST_CASE("WdfClipper never exceeds the output ceiling")
{
    // Slam it with a 4x-overscale sine; output must stay within 0 dBFS (+eps).
    auto out = renderSine(4.0, 220.0, 0.0);
    REQUIRE(peak(out, 4800) <= 1.0001);
    REQUIRE(rms(out, 4800) > 0.05);          // and it is definitely not silent
}

TEST_CASE("WdfClipper drive adds saturation: lower crest factor as drive rises")
{
    // Sub-knee input: clean at low drive, squashed toward a square at high drive.
    auto clean = renderSine(0.5, 220.0, -24.0);
    auto hot   = renderSine(0.5, 220.0,  24.0);

    const double crestClean = peak(clean, 4800) / rms(clean, 4800);
    const double crestHot   = peak(hot,   4800) / rms(hot,   4800);

    // A pure sine has crest ~1.414; clipping pushes it toward a square (~1.0).
    REQUIRE(crestClean > 1.3);
    REQUIRE(crestHot < crestClean);
}

TEST_CASE("WdfClipper gain compensation keeps the level steady through the diode")
{
    // The pre-multiply / post-divide pair compensates the drive: a small signal in
    // the near-linear region keeps essentially the same level when you raise Drive,
    // instead of the +6 dB (x2) jump a bare pre-gain would give.
    auto a = renderSine(0.1, 220.0, 0.0);
    auto b = renderSine(0.1, 220.0, 6.0);
    const double ratio = rms(b, 4800) / rms(a, 4800);
    REQUIRE(ratio > 0.8);
    REQUIRE(ratio < 1.2);
}
