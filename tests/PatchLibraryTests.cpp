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

TEST_CASE("PatchLibrary rejects empty names", "[library]")
{
    auto dir = makeTempDir();
    PatchLibrary lib; lib.setDirectory(dir);
    PatchModel m;
    REQUIRE_FALSE(lib.save("   ", m));
    REQUIRE(lib.names().isEmpty());
    dir.deleteRecursively();
}
