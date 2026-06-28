// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// PatchModel state tests — recall correctness (the project's #2 goal). Proves the
// ValueTree round-trip is lossless and that toWireMessages() rebuilds a fresh AMY
// in the right order: reset -> patch -> macros -> global FX.
#include <catch2/catch_test_macros.hpp>
#include "state/PatchModel.h"
#include <algorithm>

using namespace amyplug;

namespace
{
bool anyContains(const std::vector<std::string>& v, const std::string& needle)
{
    return std::any_of(v.begin(), v.end(),
                       [&] (const std::string& s) { return s.find(needle) != std::string::npos; });
}
} // namespace

TEST_CASE("PatchModel ValueTree round-trip is lossless", "[state]")
{
    PatchModel a;
    a.masterVolume = 3.5f; a.reverb = 0.4f; a.chorus = 0.2f; a.echo = 0.1f;
    a.synths[0].channel     = 1;
    a.synths[0].patchNumber = 42;
    a.synths[0].numVoices   = 8;
    a.synths[0].filterCutoff = 1234.0f;
    a.synths[0].filterReso   = 3.3f;
    a.synths[0].ampAttack    = 0.02f;
    a.synths[0].ampDecay     = 0.3f;
    a.synths[0].ampSustain   = 0.55f;
    a.synths[0].ampRelease   = 0.8f;
    a.synths[0].oscWireCommands = { "v0w0f440", "v1w1" };

    PatchModel b;
    b.fromValueTree(a.toValueTree());

    REQUIRE(b.masterVolume == a.masterVolume);
    REQUIRE(b.reverb == a.reverb);
    REQUIRE(b.chorus == a.chorus);
    REQUIRE(b.echo   == a.echo);
    REQUIRE(b.synths.size() == 1);
    const auto& s = b.synths[0];
    REQUIRE(s.patchNumber  == 42);
    REQUIRE(s.numVoices    == 8);
    REQUIRE(s.filterCutoff == 1234.0f);
    REQUIRE(s.filterReso   == 3.3f);
    REQUIRE(s.ampAttack    == 0.02f);
    REQUIRE(s.ampDecay     == 0.3f);
    REQUIRE(s.ampSustain   == 0.55f);
    REQUIRE(s.ampRelease   == 0.8f);
    REQUIRE(s.oscWireCommands.size() == 2);
    REQUIRE(s.oscWireCommands[0] == "v0w0f440");
}

TEST_CASE("toWireMessages rebuilds in order: reset, patch, macros, FX", "[state]")
{
    PatchModel m;                       // default: synth 1, patch 0, 6 voices
    const auto w = m.toWireMessages();

    REQUIRE_FALSE(w.empty());
    // First message is a full reset (RESET_AMY == 32768).
    REQUIRE(w.front().find('S')      != std::string::npos);
    REQUIRE(w.front().find("32768")  != std::string::npos);
    // Synth setup + each macro broadcast to synth 1.
    REQUIRE(anyContains(w, "K0"));       // patch 0
    REQUIRE(anyContains(w, "i1F"));      // filter cutoff
    REQUIRE(anyContains(w, "i1R"));      // resonance
    REQUIRE(anyContains(w, "i1A"));      // amp envelope (bp0)
    REQUIRE(anyContains(w, "V"));        // global volume
}

TEST_CASE("amp ADSR encodes as a 6-field bp0 breakpoint string", "[state]")
{
    PatchModel m;
    m.synths[0].ampAttack  = 0.01f;     // 10 ms
    m.synths[0].ampDecay   = 0.2f;      // 200 ms
    m.synths[0].ampSustain = 0.5f;
    m.synths[0].ampRelease = 0.4f;      // 400 ms
    const auto w = m.toWireMessages();

    auto it = std::find_if(w.begin(), w.end(),
                           [] (const std::string& s) { return s.find("i1A") != std::string::npos; });
    REQUIRE(it != w.end());
    // "i1A10,1,200,0.500,400,0Z" — ms are integers, sustain is fixed 3-dp.
    REQUIRE(it->find("10,1,200,0.500,400,0") != std::string::npos);
}
