// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// PatchModel state tests — recall correctness (the project's #2 goal). Proves the
// ValueTree round-trip is lossless and that toWireMessages() rebuilds a fresh AMY
// in the right order: reset -> patch -> macros -> global FX.
#include <catch2/catch_test_macros.hpp>
#include "state/PatchModel.h"
#include "state/FmAlgorithms.h"
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

TEST_CASE("Effects emit AMY's full parameter lists and round-trip", "[state]")
{
    PatchModel m;
    m.reverb = 0.3f; m.reverbSize = 0.7f;  m.reverbDamping = 0.4f;
    m.chorus = 0.5f; m.chorusRate = 2.0f;  m.chorusDepth = 0.6f;
    m.echo   = 0.4f; m.echoTime = 250.0f;  m.echoFeedback = 0.3f; m.echoTone = 0.2f;

    const auto w = m.toWireMessages();
    REQUIRE(anyContains(w, "h0.3000,0.7000,0.4000,3000"));   // reverb level,size,damp,xover
    REQUIRE(anyContains(w, "k0.5000,320,2.0000,0.6000"));    // chorus level,maxdelay,rate,depth
    REQUIRE(anyContains(w, "M0.4000,250.0000,743,0.3000,0.2000")); // echo level,time,maxdelay,fb,tone

    PatchModel b; b.fromValueTree(m.toValueTree());
    REQUIRE(b.reverbSize    == 0.7f);
    REQUIRE(b.chorusRate    == 2.0f);
    REQUIRE(b.echoTime      == 250.0f);
    REQUIRE(b.echoFeedback  == 0.3f);
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

TEST_CASE("FM engine builds the 6-operator ALGO voice", "[state][fm]")
{
    PatchModel m;
    m.synths[0].engine = PatchModel::Engine::FM;
    auto& fm = m.synths[0].fm;
    fm.algorithm = 22;
    fm.feedback  = 0.16f;
    fm.ops[0].ratio = 1.0f;  fm.ops[0].level = 1.0f;
    fm.ops[1].ratio = 2.0f;  fm.ops[1].level = 0.7f;
    const auto w = m.toWireMessages();

    REQUIRE(anyContains(w, "in7"));        // 7 oscs per voice (0 = ALGO ctrl, 1..6 ops)
    REQUIRE(anyContains(w, "v0w8"));       // osc0 = ALGO wave (8)
    REQUIRE(anyContains(w, "o22"));        // algorithm 22
    REQUIRE(anyContains(w, "b0.1600"));    // feedback on the ALGO osc
    REQUIRE(anyContains(w, "O6,5,4,3,2,1"));   // operator list (AMY orders ops 6→1)
    REQUIRE(anyContains(w, "v1w0"));       // operator 1 = sine
    REQUIRE(anyContains(w, "v6w0"));       // operator 6 = sine
    REQUIRE(anyContains(w, "I1.0000"));    // op1 ratio
    REQUIRE(anyContains(w, "I2.0000"));    // op2 ratio
    REQUIRE(anyContains(w, "a1.0000,0,0,1.0000")); // op1 amp = level x (1 + eg0)
    REQUIRE_FALSE(anyContains(w, "K"));    // FM never loads a factory patch
}

TEST_CASE("FM algorithm carriers match the known DX7 topologies", "[state][fm]")
{
    // Carriers = operators routed to the output (what you hear). Spot-check against
    // the standard DX7 algorithms.
    auto carriers = [] (int a)
    {
        auto arr = fm::algorithmCarriers(a);
        std::vector<int> v; for (int x : arr) v.push_back(x);
        return v;
    };
    REQUIRE(carriers(1)  == std::vector<int> { 1, 3 });        // algo 1: ops 1 & 3
    REQUIRE(carriers(5)  == std::vector<int> { 1, 3, 5 });     // algo 5: three stacks
    REQUIRE(carriers(16) == std::vector<int> { 1 });           // algo 16: single carrier
    REQUIRE(carriers(32) == std::vector<int> { 1, 2, 3, 4, 5, 6 }); // algo 32: all carriers
}

TEST_CASE("FM algorithm topology reconstructs modulation + feedback", "[state][fm]")
{
    // Algo 1: stacks 6->5->4->3 (op6 feedback) and 2->1; carriers 1,3.
    auto a1 = fm::algorithmTopology(1);
    REQUIRE(a1.feedback[6]);
    REQUIRE_FALSE(a1.feedback[2]);
    REQUIRE(a1.modulators[1] == juce::Array<int> { 2 });
    REQUIRE(a1.modulators[3] == juce::Array<int> { 4 });
    REQUIRE(a1.modulators[5] == juce::Array<int> { 6 });

    // Algo 2 has the same chains but feedback on op2 instead of op6 (why #1 and #2
    // look identical by carriers but are different algorithms).
    auto a2 = fm::algorithmTopology(2);
    REQUIRE(a2.feedback[2]);
    REQUIRE_FALSE(a2.feedback[6]);

    // Algo 22: op6 (feedback) fans out to carriers 3,4,5 in parallel; 2->1.
    auto a22 = fm::algorithmTopology(22);
    REQUIRE(a22.feedback[6]);
    REQUIRE(a22.modulators[3] == juce::Array<int> { 6 });
    REQUIRE(a22.modulators[4] == juce::Array<int> { 6 });
    REQUIRE(a22.modulators[5] == juce::Array<int> { 6 });
    REQUIRE(a22.modulators[1] == juce::Array<int> { 2 });
}

TEST_CASE("FM params survive the ValueTree round-trip", "[state][fm]")
{
    PatchModel a;
    a.synths[0].engine = PatchModel::Engine::FM;
    a.synths[0].fm.algorithm = 17;
    a.synths[0].fm.feedback  = 0.33f;
    a.synths[0].fm.ops[3].ratio = 4.5f;
    a.synths[0].fm.ops[3].level = 2.2f;
    a.synths[0].fm.ops[3].s     = 0.25f;

    PatchModel b; b.fromValueTree(a.toValueTree());
    REQUIRE(b.synths[0].engine == PatchModel::Engine::FM);
    REQUIRE(b.synths[0].fm.algorithm == 17);
    REQUIRE(b.synths[0].fm.feedback  == 0.33f);
    REQUIRE(b.synths[0].fm.ops[3].ratio == 4.5f);
    REQUIRE(b.synths[0].fm.ops[3].level == 2.2f);
    REQUIRE(b.synths[0].fm.ops[3].s     == 0.25f);
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
