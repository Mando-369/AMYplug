// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// PatchLibrary tests — named user presets round-trip through disk (save/load/delete).
#include <catch2/catch_test_macros.hpp>
#include "state/PatchLibrary.h"

using namespace amyplug;

namespace
{
juce::File makeTempDir()
{
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("amyplug_test_" + juce::String(juce::Time::getHighResolutionTicks()));
    d.createDirectory();
    return d;
}
} // namespace

TEST_CASE("PatchLibrary saves and reloads a named preset", "[library]")
{
    auto dir = makeTempDir();
    PatchLibrary lib;
    lib.setDirectory(dir);
    REQUIRE(lib.names().isEmpty());

    PatchModel m;
    m.synths[0].patchNumber  = 130;        // a DX7 patch
    m.synths[0].filterCutoff = 2500.0f;
    m.synths[0].ampSustain   = 0.42f;
    m.masterVolume = 2.0f;
    m.reverb = 0.3f;

    REQUIRE(lib.save("Warm Pad", m));
    REQUIRE(lib.names().contains("Warm Pad"));

    PatchModel out;
    REQUIRE(lib.load("Warm Pad", out));
    REQUIRE(out.synths[0].patchNumber  == 130);
    REQUIRE(out.synths[0].filterCutoff == 2500.0f);
    REQUIRE(out.synths[0].ampSustain   == 0.42f);
    REQUIRE(out.masterVolume == 2.0f);
    REQUIRE(out.reverb == 0.3f);

    // A fresh library pointed at the same dir sees the saved patch.
    PatchLibrary lib2; lib2.setDirectory(dir);
    REQUIRE(lib2.names().contains("Warm Pad"));

    REQUIRE(lib.remove("Warm Pad"));
    REQUIRE_FALSE(lib.names().contains("Warm Pad"));

    dir.deleteRecursively();
}

TEST_CASE("PatchLibrary groups (subfolders) keep imports out of the user list", "[library]")
{
    auto dir = makeTempDir();
    PatchLibrary lib; lib.setDirectory(dir);

    PatchModel mine;  mine.synths[0].patchNumber = 5;
    PatchModel voice; voice.synths[0].engine = PatchModel::Engine::FM;

    REQUIRE(lib.save("My Sound", mine));                 // root (the user's own)
    REQUIRE(lib.save("DX7 ROM 1A", "BRASS   1", voice)); // grouped (imported cartridge)
    REQUIRE(lib.save("DX7 ROM 1A", "E.PIANO 1", voice));

    // names() is the user's own list only — imports don't pollute it.
    REQUIRE(lib.names().contains("My Sound"));
    REQUIRE_FALSE(lib.names().contains("BRASS   1"));

    // entries() exposes everything, grouped; root group sorts first.
    const auto& es = lib.entries();
    REQUIRE(es.size() == 3);
    REQUIRE(es.front().group.isEmpty());
    int inGroup = 0;
    for (const auto& e : es) if (e.group == "DX7 ROM 1A") ++inGroup;
    REQUIRE(inGroup == 2);

    // Load + remove are group-aware; a same-named patch in another group is untouched.
    PatchModel out;
    REQUIRE(lib.load("DX7 ROM 1A", "BRASS   1", out));
    REQUIRE(out.synths[0].engine == PatchModel::Engine::FM);
    REQUIRE(lib.remove("DX7 ROM 1A", "BRASS   1"));
    REQUIRE_FALSE(lib.load("DX7 ROM 1A", "BRASS   1", out));
    REQUIRE(lib.load("DX7 ROM 1A", "E.PIANO 1", out));

    dir.deleteRecursively();
}

TEST_CASE("PatchLibrary rejects empty names", "[library]")
{
    auto dir = makeTempDir();
    PatchLibrary lib; lib.setDirectory(dir);
    PatchModel m;
    REQUIRE_FALSE(lib.save("   ", m));
    REQUIRE(lib.names().isEmpty());
    dir.deleteRecursively();
}
