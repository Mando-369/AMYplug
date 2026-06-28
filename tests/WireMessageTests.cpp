// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include <catch2/catch_test_macros.hpp>
#include "engine/AmyWire.h"
#include <algorithm>
#include <cstring>
#include <string>

using namespace amyplug;

TEST_CASE("WireBuilder emits compact AMY wire strings", "[wire]")
{
    WireBuilder w;
    w.osc(0).wave(amy::Wave::Sine).freq(440.0f).velocity(1.0f);
    // Expect something like "v0w0f440l1Z" (exact float formatting via %g).
    const std::string s = w.str();
    REQUIRE(s.front() == 'v');
    REQUIRE(s.back()  == 'Z');
    REQUIRE(s.find("w0")   != std::string::npos);
    REQUIRE(s.find("f440") != std::string::npos);
    REQUIRE(s.find("l1")   != std::string::npos);
}

TEST_CASE("Wire messages terminate exactly once", "[wire]")
{
    WireBuilder w;
    w.synth(1).patch(0);
    const std::string a = w.str();
    const std::string b = w.str();           // calling str() twice must be stable
    REQUIRE(a == b);
    REQUIRE(std::count(a.begin(), a.end(), 'Z') == 1);
}

TEST_CASE("SysEx wrapping uses AMY manufacturer id 00 03 45", "[sysex]")
{
    const char* msg = "v0f440l1";
    auto frame = wrapSysex(msg, (int) std::strlen(msg));
    REQUIRE(frame.front() == 0xF0);
    REQUIRE(frame.back()  == 0xF7);
    REQUIRE(frame[1] == 0x00);
    REQUIRE(frame[2] == 0x03);
    REQUIRE(frame[3] == 0x45);
    // Body is the raw ASCII wire string (AMY needs no encoding for lower ASCII).
    REQUIRE(frame[4] == 'v');
}

// TODO(M1): NoteRouterTests — prove every note-on yields a note-off on transport
// stop, sustain-pedal deferral works, and Panic clears all active notes.
// TODO(M1): PatchModelTests — toValueTree()/fromValueTree() round-trips, and
// toWireMessages() starts with a reset and recreates each synth.
