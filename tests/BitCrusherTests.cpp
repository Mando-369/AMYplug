// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// BitCrusher tests — retro sample-rate + bit-depth reduction. Self-contained DSP,
// so these run in the light Catch2 target (no JUCE, no AMY).

#include <catch2/catch_test_macros.hpp>
#include "dsp/BitCrusher.h"

#include <cmath>
#include <set>
#include <vector>

using amyplug::BitCrusher;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> ramp(int n)
{
    std::vector<float> v((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (std::sin(2.0 * kPi * 3.0 * i / n));
    return v;
}

void run(BitCrusher& bc, std::vector<float>& buf)
{
    for (auto& s : buf) { float* ch[1] = { &s }; bc.process(ch, 1, 1); }
}
} // namespace

TEST_CASE("BitCrusher is a true bypass at defaults (16 bit, full rate)")
{
    BitCrusher bc;
    bc.prepare(48000.0);
    bc.setBits(16.0f);
    bc.setFreqHz(48000.0f);

    auto in = ramp(512);
    auto out = in;
    run(bc, out);
    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE(out[i] == in[i]);          // untouched
}

TEST_CASE("BitCrusher reduces bit depth to a small set of levels")
{
    BitCrusher bc;
    bc.prepare(48000.0);
    bc.setBits(3.0f);                       // scale = 2^3 - 1 = 7 -> very few levels
    bc.setFreqHz(48000.0f);                 // no sample-rate reduction

    auto buf = ramp(4096);
    run(bc, buf);

    std::set<float> levels(buf.begin(), buf.end());
    // 3-bit quantization of a +-1 signal can only land on a handful of values.
    REQUIRE(levels.size() <= 20);
    REQUIRE(levels.size() > 1);             // and it is not collapsed to silence
}

TEST_CASE("BitCrusher sample-rate reduction holds samples in groups")
{
    BitCrusher bc;
    bc.prepare(48000.0);
    bc.setBits(16.0f);                      // isolate the downsampling
    bc.setFreqHz(12000.0f);                 // decim = 48000/12000 = 4 -> hold 4 samples

    auto buf = ramp(64);
    run(bc, buf);

    // Each run of 4 consecutive output samples should be identical (sample & hold).
    int holds = 0;
    for (size_t i = 1; i < buf.size(); ++i)
        if (buf[i] == buf[i - 1]) ++holds;
    REQUIRE(holds >= (int) (buf.size() * 0.6));   // ~3 of every 4 samples repeat
}

TEST_CASE("BitCrusher passes silence through as silence")
{
    BitCrusher bc;
    bc.prepare(48000.0);
    bc.setBits(4.0f);
    bc.setFreqHz(8000.0f);

    std::vector<float> buf(256, 0.0f);
    run(bc, buf);
    for (float s : buf) REQUIRE(s == 0.0f);
}
