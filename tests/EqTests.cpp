// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// The master EQ. These exist because AMY's own 3-band EQ FAILS them: it sums its bands in
// parallel (LPF - BPF + HPF), so one band's gain notches another — measured on AMY 1.2.16,
// +1 dB on the low band put a -7.7 dB hole at 1.5 kHz. See docs/upstream/AMY-eq-issue.md.
// The property worth guarding is not "a boost boosts" but "a boost does NOT dig a hole
// somewhere else", so most of what follows checks the bands we did not touch.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/ShelfPeakEq.h"
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace amyplug;

namespace
{
constexpr double kSr = 48000.0;

// Steady-state gain at one frequency, in dB: drive a sine through the EQ and compare RMS.
// The first half is discarded so the filter states have settled.
float gainDbAt(ShelfPeakEq& eq, float freqHz, int cycles = 400)
{
    const int n = (int) std::lround(kSr / freqHz * cycles);
    std::vector<float> buf((size_t) n);
    for (int i = 0; i < n; ++i)
        buf[(size_t) i] = std::sin(2.0f * 3.14159265358979f * freqHz * (float) i / (float) kSr);

    double inSum = 0.0;
    for (int i = n / 2; i < n; ++i) inSum += (double) buf[(size_t) i] * buf[(size_t) i];

    eq.reset();
    float* chans[1] = { buf.data() };
    eq.process(chans, 1, n);

    double outSum = 0.0;
    for (int i = n / 2; i < n; ++i) outSum += (double) buf[(size_t) i] * buf[(size_t) i];
    if (inSum <= 0.0 || outSum <= 0.0) return -120.0f;
    return (float) (10.0 * std::log10(outSum / inSum));
}
} // namespace

TEST_CASE("EQ at 0/0/0 is a true bypass, sample for sample")
{
    ShelfPeakEq eq; eq.prepare(kSr);
    eq.setGainsDb(0.0f, 0.0f, 0.0f);

    std::vector<float> buf(512), copy(512);
    for (int i = 0; i < 512; ++i) buf[(size_t) i] = std::sin(0.11f * (float) i) * 0.7f;
    copy = buf;
    float* chans[1] = { buf.data() };
    eq.process(chans, 1, 512);
    // Not "close to" — identical. Every band is skipped, so no state is even touched. This
    // is what makes the FX switch honest: off is off, not a very quiet filter.
    for (int i = 0; i < 512; ++i) REQUIRE(buf[(size_t) i] == copy[(size_t) i]);
}

TEST_CASE("a boost on one band does not notch the others")
{
    // THE regression. AMY scores -7.7 dB at 1.5 kHz here for a +1 dB low boost.
    ShelfPeakEq eq; eq.prepare(kSr);

    SECTION("low +6 leaves the mid and top alone")
    {
        eq.setGainsDb(6.0f, 0.0f, 0.0f);
        CHECK(gainDbAt(eq, 100.0f)   == Approx(6.0f).margin(1.0));   // in the shelf
        CHECK(gainDbAt(eq, 2500.0f)  == Approx(0.0f).margin(1.0));   // AMY: -0.6, but via -15.1 @1.5k
        CHECK(gainDbAt(eq, 7000.0f)  == Approx(0.0f).margin(1.0));
        CHECK(gainDbAt(eq, 12000.0f) == Approx(0.0f).margin(1.0));
        // No hole anywhere between the low shelf and the top — the AMY failure exactly.
        for (float f : { 1200.0f, 1500.0f, 2000.0f, 3000.0f, 5000.0f })
        { INFO(f << " Hz"); CHECK(gainDbAt(eq, f) > -1.0f); }
    }
    SECTION("low +1 stays a 1 dB move, not an 8 dB hole")
    {
        eq.setGainsDb(1.0f, 0.0f, 0.0f);
        CHECK(gainDbAt(eq, 100.0f) == Approx(1.0f).margin(0.5));
        for (float f : { 800.0f, 1500.0f, 2500.0f, 7000.0f })
        { INFO(f << " Hz"); CHECK(gainDbAt(eq, f) > -0.5f); }   // AMY: -5.9 and -7.7
    }
    SECTION("mid +6 is a bell, and the shelves stay put")
    {
        eq.setGainsDb(0.0f, 6.0f, 0.0f);
        CHECK(gainDbAt(eq, 2500.0f) == Approx(6.0f).margin(1.0));
        CHECK(gainDbAt(eq, 100.0f)  == Approx(0.0f).margin(0.6));
        CHECK(gainDbAt(eq, 15000.0f) == Approx(0.0f).margin(0.6));
        CHECK(gainDbAt(eq, 800.0f)  > -0.5f);                    // AMY: -11.3
    }
    SECTION("high +6 lifts the top without gouging the mids")
    {
        eq.setGainsDb(0.0f, 0.0f, 6.0f);
        CHECK(gainDbAt(eq, 14000.0f) == Approx(6.0f).margin(1.0));
        CHECK(gainDbAt(eq, 100.0f)   == Approx(0.0f).margin(0.5));
        for (float f : { 800.0f, 1500.0f })
        { INFO(f << " Hz"); CHECK(gainDbAt(eq, f) > -0.5f); }     // AMY: -6.8 and -6.4
    }
}

TEST_CASE("EQ cuts as well as it boosts, and the control is monotonic")
{
    ShelfPeakEq eq; eq.prepare(kSr);
    // Monotonic in the control is the other half of "behaves like an EQ": AMY's is not,
    // because the notch depth depends on the DIFFERENCE between bands, not the setting.
    float last = -99.0f;
    for (float db : { -12.0f, -6.0f, -1.0f, 0.0f, 1.0f, 6.0f, 12.0f })
    {
        eq.setGainsDb(db, 0.0f, 0.0f);
        const float g = gainDbAt(eq, 100.0f);
        INFO("set " << db << " dB -> " << g << " dB");
        CHECK(g == Approx(db).margin(1.0));
        CHECK(g > last);
        last = g;
    }
}

TEST_CASE("EQ bands compose: all three together sum in dB")
{
    ShelfPeakEq eq; eq.prepare(kSr);
    eq.setGainsDb(6.0f, 6.0f, 6.0f);
    // In series the magnitudes multiply, so equal settings give that gain broadly across
    // the band. (AMY happens to pass this one case — equal gains are the only place its
    // parallel reconstruction still holds.)
    for (float f : { 100.0f, 2500.0f, 14000.0f })
    { INFO(f << " Hz"); CHECK(gainDbAt(eq, f) == Approx(6.0f).margin(1.5)); }
}

TEST_CASE("EQ survives a nonsense sample rate and extreme settings without going non-finite")
{
    ShelfPeakEq eq; eq.prepare(8000.0);            // band centres above Nyquist/2 get clamped
    eq.setGainsDb(15.0f, -15.0f, 15.0f);
    std::vector<float> buf(1024, 0.5f);
    float* chans[1] = { buf.data() };
    eq.process(chans, 1, 1024);
    for (float v : buf) REQUIRE(std::isfinite(v));
}
