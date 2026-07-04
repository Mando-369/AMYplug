// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Dx7Import tests — prove the DX7 .syx converter (a) maps operators in the right
// order (our ops[0] == DX7 operator 1), (b) applies the fm.py conversion math, and
// (c) decodes the packed bulk format identically to the unpacked VCED format (a
// cross-check on the packed bit-offsets, since we have no real cartridge in-repo).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "state/Dx7Import.h"
#include "state/Dx7Lfo.h"
#include "state/Dx7Osc.h"
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

    // Our ops[i] == DX7 operator (i+1): coarse was set to the operator number, and is
    // stored DX7-native (verbatim), so ops[0] (DX7 OP1) -> coarse 1, ops[5] -> coarse 6.
    REQUIRE(v.fm.ops[0].coarse == 1);
    REQUIRE(v.fm.ops[5].coarse == 6);
    REQUIRE(v.fm.ops[0].fine == 0);
    REQUIRE(v.fm.ops[0].detune == 7);
    REQUIRE(v.fm.ops[0].outputLevel == 99);
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
        REQUIRE(b.fm.ops[i].coarse      == a.fm.ops[i].coarse);
        REQUIRE(b.fm.ops[i].fine        == a.fm.ops[i].fine);
        REQUIRE(b.fm.ops[i].detune      == a.fm.ops[i].detune);
        REQUIRE(b.fm.ops[i].outputLevel == a.fm.ops[i].outputLevel);
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

// --- Factory wire decode (Load into Editor) --------------------------------
// A real AMY factory DX7 patch (#128 "DX7 BRASS 1", verbatim from patches.h).
// Operators live on v2..v7 with O2,3,4,5,6,7; ratio=I, level=first coef of a.
namespace
{
constexpr const char* kBrass1 =
    "v2a0.458502,0,0,1,0,0P0.25A0,0.000188,97,0.917004,0,0.917004,530,0.5,70,0.000188L1I1Z"
    "v3a1.834008,0,0,1,0,0P0.25A0,0.000188,4,1,30,0.917004,0,0.917004,53,0.000188L1I1.00125Z"
    "v4a2,0,0,1,0,0P0.25A0,0.000188,4,1,30,0.917004,0,0.917004,53,0.000188L1I1Z"
    "v5a2,0,0,1,0,0P0.25A0,0.000188,4,1,0,0.917004,0,0.917004,53,0.000188L1I0.9975Z"
    "v6a0.64842,0,0,1,0,0P0.25A0,0.000188,11,0.229251,26,0.707107,37,0.771105,52,0.000188L1I0.504375Z"
    "v7a1.834008,0,0,1,0,0P0.25A0,0.000188,7,1,3,0.385553,0,0.771105,52,0.000188L1I0.504375Z"
    "v1w0a1f6.166667P0.25Z"
    "v0w8a1,0,1,0,0,0f0,1,0,1,0,0.007298b0.16A0,1,0,1,0,1,0,1,731,1L1O2,3,4,5,6,7o22Z";
} // namespace

TEST_CASE("Factory wire decode maps a DX7 preset onto OP1..6", "[dx7][factory]")
{
    PatchModel::FmParams fm;
    REQUIRE(factoryFmWireToParams(kBrass1, fm));

    REQUIRE(fm.algorithm == 22);
    REQUIRE(fm.feedback == Approx(0.16f).margin(1e-4));

    // O2,3,4,5,6,7 -> ops[i] takes osc(src[5-i]); AMY lists algo_source 6->1 so our
    // OP1 == DX7 operator 1 == osc7, OP6 == osc2. The wire's ratio/amp are decoded back
    // to DX7 Coarse/Fine/Detune/Level; reconstructing them reproduces the wire values.
    auto ratioOf = [] (const PatchModel::FmOp& o)
    { return (float) dx7osc::coarseFineRatio(o.coarse, o.fine, o.detune); };
    auto ampOf   = [] (const PatchModel::FmOp& o)
    { return (float) dx7osc::outputLevelToAmp(o.outputLevel); };
    REQUIRE(ratioOf(fm.ops[0]) == Approx(0.504375f).margin(1e-3));   // osc7
    REQUIRE(ampOf(fm.ops[0])   == Approx(1.834008f).margin(0.02));
    REQUIRE(ratioOf(fm.ops[2]) == Approx(0.9975f).margin(1e-3));     // osc5
    REQUIRE(ratioOf(fm.ops[3]) == Approx(1.0f).margin(1e-3));        // osc4
    REQUIRE(ratioOf(fm.ops[5]) == Approx(1.0f).margin(1e-3));        // osc2
    REQUIRE(ampOf(fm.ops[5])   == Approx(0.458502f).margin(0.02));

    // Full DX7 4R/4L envelope decoded losslessly: L1 (attack peak) ~98, L3 (sustain)
    // ~91, L4 (release floor) 0. levelToLinear(98)=0.917, (91)=0.5, (0)~0.
    REQUIRE(fm.ops[5].egLevel[0] == Approx(98.0f).margin(1.0));
    REQUIRE(fm.ops[5].egLevel[2] == Approx(91.0f).margin(1.0));
    REQUIRE(fm.ops[5].egLevel[3] == Approx(0.0f).margin(1.0));
    REQUIRE_FALSE(fm.ops[0].fixedFreq);   // all BRASS 1 operators are ratio mode

    // LFO: osc1 = w0 (AMY SINE -> DX7 Sine wave 4), f6.166667 -> speed 37. The ALGO
    // freq mod-coef 0.007298 is vibrato -> PMS 7 + a small PMD. No tremolo (amp coef 0).
    REQUIRE(fm.lfoWave  == 4);
    REQUIRE(fm.lfoSpeed == Approx(37.0f).margin(0.05));
    REQUIRE(fm.lfoPms   == 7);
    REQUIRE(fm.lfoPmd   > 0.0f);
    REQUIRE(fm.lfoAmd   == Approx(0.0f).margin(1e-4));
    REQUIRE(fm.transpose == 0);   // BRASS 1 ALGO freq const is 0 -> no transpose
    for (int i = 0; i < 6; ++i) REQUIRE(fm.ops[i].ampModSens == 0);
    // Re-emitting the decoded PMS+PMD reproduces the original vibrato depth (lossless).
    REQUIRE((float) dx7lfo::pitchLfoAmp(fm.lfoPms, fm.lfoPmd) == Approx(0.007298f).margin(1e-5));
}

TEST_CASE("Dx7Lfo conversions round-trip (speed/wave/depths)", "[dx7][lfo]")
{
    using namespace amyplug::dx7lfo;
    // Wave map is a bijection over 0..5.
    for (int w = 0; w <= 5; ++w) REQUIRE(lfoAmyToDx7(lfoWaveToAmy(w)) == w);
    // Speed <-> Hz is exact in the common /6 band (a DX7 speed of 37 -> 6.1667 Hz).
    REQUIRE(lfoSpeedToHz(37.0) == Approx(6.16667).margin(1e-4));
    REQUIRE(lfoHzToSpeed(lfoSpeedToHz(37.0)) == Approx(37.0).margin(1e-4));
    // Tremolo depth AMD <-> amp_lfo_amp is invertible; AMD 99 = full (1.0).
    REQUIRE(ampLfoAmp(99.0) == Approx(1.0).margin(1e-6));
    REQUIRE(ampToAmd(ampLfoAmp(50.0)) == Approx(50.0).margin(1e-3));
    // Vibrato: PMD 0 -> no mod; solving PMD at fixed PMS reproduces the same depth.
    REQUIRE(pitchLfoAmp(7, 0.0) == Approx(0.0).margin(1e-9));
    const double amp = pitchLfoAmp(7, 30.0);
    REQUIRE(pitchLfoAmp(7, pitchAmpToPmd(amp, 7)) == Approx(amp).margin(1e-9));
}

TEST_CASE("Factory wire decode rejects a non-FM (Juno) patch", "[dx7][factory]")
{
    // Juno #0 "A11 Brass Set 1" — analog structure, no algorithm/O list.
    constexpr const char* kJuno0 =
        "v1w4a1,,0,1Zv0w20c2L1G4Zv2w1c3L1Zv3w3c4L1Zv4w1c5L1Zv5w5L1Z"
        "v1f0.945A156,1.0,10000,0Zv0F179.93,0.677,,5.024,0,0R0.93Z"
        "v0a0.85,,1,1,0A30,1,1355,0.354,232,0Zx0,0,0k1,,0.5,0.5Z";
    PatchModel::FmParams fm;
    REQUIRE_FALSE(factoryFmWireToParams(kJuno0, fm));
}

// --- Factory Juno (analog) decode ------------------------------------------
TEST_CASE("Factory analog decode maps a Juno preset onto OSC A/B/C/D + VCF/LFO", "[dx7][factory][analog]")
{
    // Juno #0 "A11 Brass Set 1", verbatim from patches.h. 4 DCOs on v2..v5, VCF/VCA
    // on v0 (w20, G filter, shared ADSR), LFO on v1 (w4).
    constexpr const char* kJuno0 =
        "v1w4a1,,0,1Zv0w20c2L1G4Zv2w1c3L1Zv3w3c4L1Zv4w1c5L1Zv5w5L1Z"
        "v1f0.945A156,1.0,10000,0Z"
        "v2a0,,0,0f220,1,,,,0,1d0.902,,,,,0m0Zv3a1,,0,0f220,1,,,,0,1m0Z"
        "v4a0,,0,0f110,1,,,,0,1m0Zv5a0,,0,0Z"
        "v0F179.93,0.677,,5.024,0,0R0.93Zv0a0.85,,1,1,0A30,1,1355,0.354,232,0Z"
        "x0,0,0k1,,0.5,0.5Z";

    PatchModel::AnalogParams a;
    REQUIRE(factoryAnalogWireToParams(kJuno0, a));

    // Four DCOs, in osc order v2->A, v3->B, v4->C, v5->D.
    REQUIRE(a.aWave == 1); REQUIRE(a.aFreq == Approx(220.0f).margin(0.5));
    REQUIRE(a.bWave == 3); REQUIRE(a.bFreq == Approx(220.0f).margin(0.5));
    REQUIRE(a.bLevel == Approx(1.0f).margin(0.01));
    REQUIRE(a.cWave == 1); REQUIRE(a.cFreq == Approx(110.0f).margin(0.5));
    REQUIRE(a.dWave == 5);

    // VCF + LFO.
    REQUIRE(a.vcfFreq == Approx(179.93f).margin(0.1));
    REQUIRE(a.vcfReso == Approx(0.93f).margin(0.01));
    REQUIRE(a.filterType == 4);
    REQUIRE(a.lfoWave == 4);
    REQUIRE(a.lfoFreq == Approx(0.945f).margin(0.01));

    // Shared ADSR (osc0 A30,1,1355,0.354,232,0) -> amp + filter env.
    REQUIRE(a.ampA == Approx(0.030f).margin(0.005));
    REQUIRE(a.ampS == Approx(0.354f).margin(0.01));
    REQUIRE(a.vcfS == Approx(0.354f).margin(0.01));   // Juno shares the env
}

TEST_CASE("Factory analog decode rejects an FM (DX7) patch", "[dx7][factory][analog]")
{
    PatchModel::AnalogParams a;
    REQUIRE_FALSE(factoryAnalogWireToParams(kBrass1, a));   // no filter 'G' -> not analog
}

// --- DX7 4R/4L envelope round-trip (Stage 2 lossless-shape proof) ----------
#include "state/Dx7Envelope.h"
namespace
{
// Parse "t0,l0,t1,l1,..." into parallel arrays.
void parseBp(const juce::String& s, std::vector<double>& t, std::vector<double>& l)
{
    juce::StringArray p; p.addTokens(s, ",", "");
    for (int i = 0; i + 1 < p.size(); i += 2) { t.push_back(p[i].getDoubleValue()); l.push_back(p[i + 1].getDoubleValue()); }
}
// Decode a factory operator envelope to 4R/4L, re-emit, and assert the breakpoints
// come back (times within 2% or 3ms; levels within 4%).
void requireEnvRoundTrips(const char* env)
{
    float rate[4], level[4];
    REQUIRE(amyplug::dx7env::breakpointsToEg(env, rate, level));
    const juce::String re = amyplug::dx7env::egToBreakpoints(rate, level);

    std::vector<double> t0, l0, t1, l1;
    parseBp(env, t0, l0); parseBp(re, t1, l1);
    REQUIRE(t1.size() == t0.size());
    for (size_t i = 0; i < t0.size(); ++i)
    {
        REQUIRE(t1[i] == Approx(t0[i]).epsilon(0.02).margin(3.0));   // times
        REQUIRE(l1[i] == Approx(l0[i]).epsilon(0.04).margin(0.0005)); // linear levels
    }
}
} // namespace

TEST_CASE("DX7 4R/4L envelope decode<->emit round-trips the factory shapes", "[dx7][env]")
{
    // Real operator envelopes from patches.h (MARIMBA, BASS 1, PIANO 3) — the shapes
    // Stage 1's ADSR flattened. If these round-trip, the full shape is preserved.
    requireEnvRoundTrips("0,0.000188,12896,0.162105,0,0.162105,270,0.000188,60000,0.000188"); // MARIMBA v2
    requireEnvRoundTrips("0,0.000188,0,0.594604,201,0.00213,3294,0.000188,208,0.000188");      // BASS 1 v2
    requireEnvRoundTrips("0,0.000188,2,1,34,0.000977,5708,0.000188,28743,0.000188");            // PIANO 3 v2
    requireEnvRoundTrips("0,0.000188,97,0.917004,0,0.917004,530,0.5,70,0.000188");              // BRASS 1 v2
}

TEST_CASE("DX7 pitch-EG decode<->emit round-trips (ratio levels + pitch timing)", "[dx7][env]")
{
    using namespace amyplug::dx7env;
    auto roundTrips = [] (const char* bpStr)
    {
        float rate[4], level[4];
        REQUIRE(breakpointsToPitchEg(bpStr, rate, level));
        const juce::String re = pitchEgToBreakpoints(rate, level);
        std::vector<double> t0, l0, t1, l1;
        juce::StringArray a; a.addTokens(juce::String(bpStr), ",", ""); juce::StringArray b; b.addTokens(re, ",", "");
        REQUIRE(a.size() == b.size());
        for (int i = 0; i + 1 < a.size(); i += 2)
        {
            REQUIRE(b[i].getDoubleValue()     == Approx(a[i].getDoubleValue()).epsilon(0.03).margin(3.0));   // time
            REQUIRE(b[i + 1].getDoubleValue() == Approx(a[i + 1].getDoubleValue()).epsilon(0.03).margin(0.002)); // ratio
        }
    };
    roundTrips("0,1,0,1,0,1,0,1,731,1");                       // BRASS 1 ALGO pitch env (flat)
    roundTrips("0,1,120,1.2968,30,1.15,300,0.9441,200,1");     // synthetic pitch sweep (distinct levels)
}
