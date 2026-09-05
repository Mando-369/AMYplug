// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// The flat preset catalogue the ‹ › arrows walk: factory in AMY's order, then user
// presets with the top level first, STOPPING at both ends.
#include <catch2/catch_test_macros.hpp>
#include "state/PresetCatalog.h"

using namespace amyplug;

namespace
{
juce::File makeTempDir()
{
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("amyplug_catalog_" + juce::String(juce::Time::getHighResolutionTicks()));
    d.createDirectory();
    return d;
}
} // namespace

TEST_CASE("PresetCatalog: factory first, user after, steps stop at the ends", "[preset]")
{
    auto dir = makeTempDir();
    PatchLibrary lib; lib.setDirectory(dir);
    PatchModel m;
    lib.save("", "Root One", m);
    lib.save("Zed", "In Zed", m);
    lib.save("Alpha", "In Alpha", m);

    const auto c = PresetCatalog::build(lib);
    REQUIRE((int) c.items.size() == kBuiltinPatchCount + 3);
    REQUIRE(c.items[0] == PresetRef::factory(0));
    REQUIRE(c.items[(size_t) kBuiltinPatchCount] == PresetRef::user("", "Root One"));     // top level first
    REQUIRE(c.items[(size_t) kBuiltinPatchCount + 1] == PresetRef::user("Alpha", "In Alpha"));
    REQUIRE(c.items[(size_t) kBuiltinPatchCount + 2] == PresetRef::user("Zed", "In Zed"));

    // ⚠️ The arrows STOP at the ends. They used to wrap, and wrapping meant ‹ on the very
    // first factory patch jumped to an unrelated user patch in the last bank — reported as
    // "something weird happens at default". Standing still is the correct answer there.
    REQUIRE(c.step(PresetRef::factory(0), -1) == PresetRef::factory(0));              // was: last user
    REQUIRE(c.step(PresetRef::user("Zed", "In Zed"), +1) == PresetRef::user("Zed", "In Zed"));
    REQUIRE(c.step(PresetRef::factory(0), +1) == PresetRef::factory(1));              // still steps
    REQUIRE(c.step(PresetRef::user("Zed", "In Zed"), -1) == PresetRef::user("Alpha", "In Alpha"));

    // ...and the editor greys the arrow out so a dead button is visibly a limit, not a fault.
    REQUIRE(c.atEnd(PresetRef::factory(0), -1));
    REQUIRE_FALSE(c.atEnd(PresetRef::factory(0), +1));
    REQUIRE(c.atEnd(PresetRef::user("Zed", "In Zed"), +1));
    REQUIRE_FALSE(c.atEnd(PresetRef::user("Zed", "In Zed"), -1));

    // An orphan (a preset from another machine) is at neither end, and steps from the end
    // it is heading away from.
    REQUIRE_FALSE(c.atEnd(PresetRef::user("Gone", "Orphan"), -1));
    REQUIRE(c.step(PresetRef::user("Gone", "Orphan"), +1) == PresetRef::factory(0));
    REQUIRE(c.step(PresetRef::user("Gone", "Orphan"), -1) == PresetRef::user("Zed", "In Zed"));

    REQUIRE(juce::String(PresetCatalog::factoryEngineName(0))   == "Analog (Juno-6)");
    REQUIRE(juce::String(PresetCatalog::factoryEngineName(200)) == "FM (DX7)");
    dir.deleteRecursively();
}
