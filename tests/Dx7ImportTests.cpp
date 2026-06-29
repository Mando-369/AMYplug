// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Dx7Import tests — prove the DX7 .syx converter (a) maps operators in the right
// order (our ops[0] == DX7 operator 1), (b) applies the fm.py conversion math, and
// (c) decodes the packed bulk format identically to the unpacked VCED format (a
// cross-check on the packed bit-offsets, since we have no real cartridge in-repo).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "state/Dx7Import.h"
#include <vector>
#include <cstdint>

using namespace amyplug;
using Catch::Approx;

namespace
{
// A logical test voice: per-operator coarse ratio = the DX7 operator number
// (OP1->1 .. OP6->6), all output levels 99, ratio mode; algorithm 5, feedback 7.
constexpr int kAlgoByte = 4;     // stored 0..31 -> algorithm 5
constexpr int kFeedback = 7;     // -> 0.00125 * 2^7 = 0.16
const char* kName = "TEST VOICE";

// Build the unpacked single-voice VCED payload (155 bytes), ops stored OP6..OP1.
std::vector<std::uint8_t> buildVced()
{
    std::vector<std::uint8_t> d(155, 0);
    for (int k = 0; k < 6; ++k)               // k=0 is OP6, k=5 is OP1
    {
        const int opNum = 6 - k;              // OP6..OP1
        std::uint8_t* o = d.data() + k * 21;
        o[4] = o[5] = o[6] = o[7] = 99;       // EG levels all 99
        o[16] = 99;                           // output level
        o[17] = 0;                            // mode = ratio
        o[18] = (std::uint8_t) opNum;         // coarse
        o[19] = 0;                            // fine
        o[20] = 7;                            // detune (centre)
    }
    d[134] = kAlgoByte;
    d[135] = kFeedback;
    for (int i = 0; i < 10; ++i) d[145 + i] = (std::uint8_t) kName[i];
    return d;
}

// Build the packed bulk payload for one voice (128 bytes), ops stored OP6..OP1.
std::vector<std::uint8_t> buildPackedVoice()
{
    std::vector<std::uint8_t> d(128, 0);
    for (int k = 0; k < 6; ++k)
    {
        const int opNum = 6 - k;
        std::uint8_t* o = d.data() + k * 17;
        o[4] = o[5] = o[6] = o[7] = 99;       // EG levels all 99
        o[12] = (std::uint8_t) (7 << 3);      // detune 7 in bits 3-6
        o[14] = 99;                           // output level
        o[15] = (std::uint8_t) ((opNum << 1) | 0);  // coarse in bits1-5, mode bit0 = ratio
        o[16] = 0;                            // fine
    }
    d[110] = kAlgoByte;
    d[111] = kFeedback;                       // bits0-2
    for (int i = 0; i < 10; ++i) d[118 + i] = (std::uint8_t) kName[i];
    return d;
}

// Wrap a payload in the DX7 bulk SysEx frame (F0 43 00 09 20 00 <4096> cksum F7).
std::vector<std::uint8_t> wrapBulk(const std::vector<std::uint8_t>& voice0)
{
    std::vector<std::uint8_t> data(4096, 0);
    for (size_t i = 0; i < voice0.size() && i < 128; ++i) data[i] = voice0[i];
    std::vector<std::uint8_t> syx { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00 };
    syx.insert(syx.end(), data.begin(), data.end());
    syx.push_back(0x00);   // checksum (parser ignores it)
    syx.push_back(0xF7);
    return syx;
}
} // namespace

TEST_CASE("DX7 VCED single voice converts with correct operator order", "[dx7]")
{
    const auto vced = buildVced();
    auto voices = Dx7Import::parse(vced.data(), vced.size());
    REQUIRE(voices.size() == 1);
    const auto& v = voices[0];

    REQUIRE(v.name == juce::String("TEST VOICE"));
    REQUIRE(v.fm.algorithm == 5);
    REQUIRE(v.fm.feedback == Approx(0.16f).margin(0.001f));

    // Our ops[i] == DX7 operator (i+1): coarse was set to the operator number, so
    // ops[0] (DX7 OP1) -> ratio 1, ops[5] (DX7 OP6) -> ratio 6.
    REQUIRE(v.fm.ops[0].ratio == Approx(1.0f));
    REQUIRE(v.fm.ops[5].ratio == Approx(6.0f));
    // output level 99 -> 2 * 2^((99-99)/8) = 2.0.
    REQUIRE(v.fm.ops[0].level == Approx(2.0f));
}

TEST_CASE("DX7 packed bulk decodes identically to VCED (bit-offset cross-check)", "[dx7]")
{
    const auto vcedVoices   = Dx7Import::parse(buildVced().data(), 155);
    const auto bulk         = wrapBulk(buildPackedVoice());
    const auto bulkVoices   = Dx7Import::parse(bulk.data(), bulk.size());

    REQUIRE(bulkVoices.size() == 32);              // full cartridge
    const auto& a = vcedVoices[0];
    const auto& b = bulkVoices[0];                 // our real voice is slot 0

    REQUIRE(b.name == a.name);
    REQUIRE(b.fm.algorithm == a.fm.algorithm);
    REQUIRE(b.fm.feedback == Approx(a.fm.feedback));
    for (int i = 0; i < 6; ++i)
    {
        REQUIRE(b.fm.ops[i].ratio == Approx(a.fm.ops[i].ratio));
        REQUIRE(b.fm.ops[i].level == Approx(a.fm.ops[i].level));
    }
}

TEST_CASE("DX7 import builds a recallable FM PatchModel", "[dx7]")
{
    const auto vced = buildVced();
    auto voices = Dx7Import::parse(vced.data(), vced.size());
    REQUIRE_FALSE(voices.empty());

    PatchModel m = Dx7Import::toPatchModel(voices[0]);
    REQUIRE(m.synths[0].engine == PatchModel::Engine::FM);

    // Survives the ValueTree round-trip (recall) and emits the FM ALGO voice.
    PatchModel rt; rt.fromValueTree(m.toValueTree());
    REQUIRE(rt.synths[0].engine == PatchModel::Engine::FM);
    REQUIRE(rt.synths[0].fm.algorithm == 5);
    const auto wire = m.toWireMessages();
    bool hasAlgo = false;
    for (const auto& s : wire) if (s.find("w8") != std::string::npos && s.find("o5") != std::string::npos) hasAlgo = true;
    REQUIRE(hasAlgo);
}
