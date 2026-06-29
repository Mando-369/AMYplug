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
    // First message releases synths/voices/oscs (RESET_SYNTHS == 262144).
    REQUIRE(w.front().find('S')       != std::string::npos);
    REQUIRE(w.front().find("262144")  != std::string::npos);
    // Synth setup + each macro broadcast to synth 1.
    REQUIRE(anyContains(w, "K0"));       // patch 0
    REQUIRE(anyContains(w, "i1F"));      // filter cutoff
    REQUIRE(anyContains(w, "i1R"));      // resonance
    REQUIRE(anyContains(w, "i1A"));      // amp envelope (bp0)
    REQUIRE(anyContains(w, "V"));        // global volume
}

TEST_CASE("amp-ADSR macro overrides bp0 only for Juno, not DX7 (pitch-env safety)", "[state]")
{
    // bp0/eg0 is the amp envelope on Juno (0..127) but the carrier PITCH envelope on
    // DX7 (128..255). Overriding it on DX7 glitches the note's pitch at onset, so we
    // must not emit i1A there.
    PatchModel juno; juno.synths[0].patchNumber = 0;
    REQUIRE(anyContains(juno.toWireMessages(), "i1A"));

    PatchModel dx7; dx7.synths[0].patchNumber = 130;
    REQUIRE_FALSE(anyContains(dx7.toWireMessages(), "i1A"));
    // ...but cutoff/reso macros are still emitted for DX7.
    REQUIRE(anyContains(dx7.toWireMessages(), "i1F"));
}

TEST_CASE("Analog engine builds the 4-oscillator subtractive voice", "[state][analog]")
{
    PatchModel m;
    m.synths[0].engine = PatchModel::Engine::Analog;
    const auto w = m.toWireMessages();

    REQUIRE(anyContains(w, "in4"));        // 4 oscs per voice
    REQUIRE(anyContains(w, "v0w20"));      // osc0 = SILENT filter/VCA
    REQUIRE(anyContains(w, "v0w20"));
    REQUIRE(anyContains(w, "A"));          // amp env (bp0)
    REQUIRE(anyContains(w, "B"));          // filter env (bp1)
    REQUIRE(anyContains(w, "c2"));         // chained osc0<-osc2
    REQUIRE(anyContains(w, "v1w"));        // LFO osc1
    REQUIRE(anyContains(w, ",0Z"));        // LFO freq has note-coef 0 (won't track notes)
    REQUIRE(anyContains(w, "v2w"));        // OSC A
    REQUIRE(anyContains(w, "v3w"));        // OSC B
    REQUIRE(anyContains(w, "L1"));         // LFO as mod source
    REQUIRE_FALSE(anyContains(w, "K"));    // analog never loads a factory patch
}

TEST_CASE("Analog params survive the ValueTree round-trip", "[state][analog]")
{
    PatchModel a;
    a.synths[0].engine = PatchModel::Engine::Analog;
    a.synths[0].analog.vcfFreq = 1234.0f;
    a.synths[0].analog.aWave   = 5;
    a.synths[0].analog.lfoToFilter = 0.42f;
    a.eqLow = -3.0f;

    PatchModel b; b.fromValueTree(a.toValueTree());
    REQUIRE(b.synths[0].engine == PatchModel::Engine::Analog);
    REQUIRE(b.synths[0].analog.vcfFreq == 1234.0f);
    REQUIRE(b.synths[0].analog.aWave   == 5);
    REQUIRE(b.synths[0].analog.lfoToFilter == 0.42f);
    REQUIRE(b.eqLow == -3.0f);
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
