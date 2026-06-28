// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// NoteRouter lifecycle tests — the project's #1 guarantee: no hanging notes.
// Drives NoteRouter with crafted MIDI and asserts every note-on is balanced by a
// note-off across every path (normal, transport-stop, sustain, panic).
#include <catch2/catch_test_macros.hpp>
#include "midi/NoteRouter.h"
#include "MockBackend.h"

using namespace amyplug;

namespace
{
juce::MidiBuffer noteOnBuf (int ch, int note, int vel)
{
    juce::MidiBuffer b; b.addEvent(juce::MidiMessage::noteOn(ch, note, (juce::uint8) vel), 0); return b;
}
juce::MidiBuffer noteOffBuf (int ch, int note)
{
    juce::MidiBuffer b; b.addEvent(juce::MidiMessage::noteOff(ch, note), 0); return b;
}
juce::MidiBuffer ccBuf (int ch, int cc, int val)
{
    juce::MidiBuffer b; b.addEvent(juce::MidiMessage::controllerEvent(ch, cc, (juce::uint8) val), 0); return b;
}
} // namespace

TEST_CASE("Every note-on is balanced by exactly one note-off", "[router]")
{
    NoteRouter r; MockBackend b;
    r.process(noteOnBuf(1, 60, 100), b);
    REQUIRE(r.anyActive());
    r.process(noteOffBuf(1, 60), b);
    REQUIRE_FALSE(r.anyActive());
    REQUIRE(b.netOnFor(1, 60) == 0);
    REQUIRE(b.totalNetOn() == 0);
}

TEST_CASE("Note-on with velocity 0 is treated as note-off", "[router]")
{
    NoteRouter r; MockBackend b;
    r.process(noteOnBuf(1, 64, 100), b);
    r.process(noteOnBuf(1, 64, 0), b);          // running-status note-off
    REQUIRE_FALSE(r.anyActive());
    REQUIRE(b.totalNetOn() == 0);
}

TEST_CASE("Transport stop flushes held notes (no hanging note)", "[router]")
{
    NoteRouter r; MockBackend b;
    r.updateTransport(true, b);                 // playing
    r.process(noteOnBuf(1, 60, 100), b);
    r.process(noteOnBuf(1, 67, 100), b);        // hold a 2-note chord, no note-offs
    REQUIRE(r.anyActive());

    r.updateTransport(false, b);                // STOP -> falling edge flushes
    REQUIRE(b.allNotesOffCount == 1);
    REQUIRE(b.totalNetOn() == 0);               // both notes explicitly released
    REQUIRE_FALSE(r.anyActive());
}

TEST_CASE("Panic / allNotesOff releases everything and clears tracking", "[router]")
{
    NoteRouter r; MockBackend b;
    r.process(noteOnBuf(3, 48, 100), b);
    r.process(noteOnBuf(3, 55, 100), b);
    r.allNotesOff(&b);
    REQUIRE(b.allNotesOffCount == 1);
    REQUIRE(b.totalNetOn() == 0);
    REQUIRE_FALSE(r.anyActive());
}

TEST_CASE("Sustain pedal defers note-offs and flushes on release", "[router]")
{
    NoteRouter r; MockBackend b;
    r.process(noteOnBuf(1, 60, 100), b);
    r.process(ccBuf(1, 64, 127), b);            // sustain ON
    r.process(noteOffBuf(1, 60), b);            // deferred — must NOT release yet
    REQUIRE(b.totalNetOn() == 1);               // still sounding
    REQUIRE(r.anyActive());

    r.process(ccBuf(1, 64, 0), b);              // sustain OFF -> flush
    REQUIRE(b.totalNetOn() == 0);
    REQUIRE_FALSE(r.anyActive());
}

TEST_CASE("Sustain held at transport stop still flushes", "[router]")
{
    NoteRouter r; MockBackend b;
    r.updateTransport(true, b);
    r.process(noteOnBuf(1, 62, 100), b);
    r.process(ccBuf(1, 64, 127), b);            // pedal down
    r.process(noteOffBuf(1, 62), b);            // deferred
    REQUIRE(b.totalNetOn() == 1);

    r.updateTransport(false, b);                // stop while pedal down
    REQUIRE(b.totalNetOn() == 0);
    REQUIRE_FALSE(r.anyActive());
}

TEST_CASE("Notes on independent channels are tracked separately", "[router]")
{
    NoteRouter r; MockBackend b;
    r.process(noteOnBuf(1, 60, 100), b);
    r.process(noteOnBuf(2, 60, 100), b);        // same note, different synth
    r.process(noteOffBuf(1, 60), b);
    REQUIRE(r.anyActive());                     // ch2 still sounding
    REQUIRE(b.netOnFor(2, 60) == 1);
    r.process(noteOffBuf(2, 60), b);
    REQUIRE_FALSE(r.anyActive());
    REQUIRE(b.totalNetOn() == 0);
}
