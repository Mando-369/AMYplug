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

// ---------------------------------------------------------------------------
// Banks — one level of subfolders, listed from the FOLDER tree.
// ---------------------------------------------------------------------------
TEST_CASE("PatchLibrary lists banks from the folder tree, including empty ones", "[library]")
{
    auto dir = makeTempDir();
    PatchLibrary lib; lib.setDirectory(dir);
    REQUIRE(lib.banks().isEmpty());

    // A folder made by hand in the Finder must show up while it is still empty — that
    // is how a bank usually starts. A hidden folder must not.
    dir.getChildFile("Hand Made").createDirectory();
    dir.getChildFile(".hidden").createDirectory();
    lib.refresh();
    REQUIRE(lib.banks() == juce::StringArray { "Hand Made" });

    PatchModel m;
    lib.save("Zeta",  "One", m);
    lib.save("Alpha", "Two", m);
    REQUIRE(lib.banks() == juce::StringArray { "Alpha", "Hand Made", "Zeta" });   // natural order
    dir.deleteRecursively();
}

TEST_CASE("PatchLibrary bank operations: create, move, rename, delete", "[library]")
{
    auto dir = makeTempDir();
    PatchLibrary lib; lib.setDirectory(dir);
    PatchModel m;

    REQUIRE(lib.createBank("Leads"));
    REQUIRE_FALSE(lib.createBank("   "));                       // nothing legal in it
    REQUIRE(lib.banks() == juce::StringArray { "Leads" });

    // Move a preset out of a bank: it lands in the target, and the emptied source is pruned.
    REQUIRE(lib.save("Leads", "A", m));
    REQUIRE(lib.movePreset("Leads", "A", "Basses"));
    REQUIRE(lib.banks() == juce::StringArray { "Basses" });
    PatchModel probe;
    REQUIRE(lib.load("Basses", "A", probe));
    REQUIRE_FALSE(lib.load("Leads", "A", probe));

    // Never clobber: a preset of that name already in the target refuses the move, and
    // both files are still where they were.
    REQUIRE(lib.save("X", "A", m));
    REQUIRE_FALSE(lib.movePreset("X", "A", "Basses"));
    REQUIRE(lib.load("X", "A", probe));
    REQUIRE(lib.load("Basses", "A", probe));

    // Rename a bank; refuse to rename ONTO an existing one.
    REQUIRE(lib.renameBank("Basses", "Bass"));
    REQUIRE(lib.load("Bass", "A", probe));
    REQUIRE_FALSE(lib.renameBank("Bass", "X"));

    // Rename a preset — the display name lives inside the file, so the file is rewritten.
    REQUIRE(lib.renamePreset("Bass", "A", "B"));
    REQUIRE(lib.load("Bass", "B", probe));
    REQUIRE_FALSE(lib.load("Bass", "A", probe));
    REQUIRE_FALSE(lib.renamePreset("X", "A", "A"));            // no-op is not a rename

    // Delete a bank, contents and all (to the trash).
    REQUIRE(lib.deleteBank("Bass"));
    REQUIRE(lib.banks() == juce::StringArray { "X" });
    REQUIRE_FALSE(lib.deleteBank(""));                          // the root is not a bank
    dir.deleteRecursively();
}

TEST_CASE("PatchLibrary prunes an emptied bank even when the Finder left a .DS_Store", "[library]")
{
    auto dir = makeTempDir();
    PatchLibrary lib; lib.setDirectory(dir);
    PatchModel m;
    REQUIRE(lib.save("Tidy", "P", m));
    dir.getChildFile("Tidy").getChildFile(".DS_Store").create();   // what the Finder leaves behind
    REQUIRE(lib.remove("Tidy", "P"));
    REQUIRE(lib.banks().isEmpty());                                 // pruned despite the hidden file
    dir.deleteRecursively();
}
