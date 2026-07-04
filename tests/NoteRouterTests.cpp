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

TEST_CASE("Notes route to the single AMY synth regardless of MIDI channel", "[router]")
{
    // We build only AMY synth 1, so notes on ANY MIDI channel must target synth 1 —
    // emitting to an undefined synth (e.g. synth 2 for a channel-2 note) makes AMY
    // access unallocated voice state (out-of-bounds). Lifecycle is still tracked per
    // channel, but the backend only ever sees synth 1.
    NoteRouter r; MockBackend b;
    r.process(noteOnBuf(1, 60, 100), b);
    r.process(noteOnBuf(2, 62, 100), b);        // different channel -> STILL synth 1
    REQUIRE(b.netOnFor(1, 60) == 1);
    REQUIRE(b.netOnFor(1, 62) == 1);
    REQUIRE(b.netOnFor(2, 60) == 0);            // channel 2 must NOT create synth 2
    REQUIRE(b.netOnFor(2, 62) == 0);
    r.process(noteOffBuf(1, 60), b);
    r.process(noteOffBuf(2, 62), b);            // ch2 note-off also releases on synth 1
    REQUIRE(b.totalNetOn() == 0);
    REQUIRE_FALSE(r.anyActive());
}

TEST_CASE("Note transpose shifts the AMY note but tracks the original", "[router]")
{
    NoteRouter r; MockBackend b;
    r.setNoteTranspose(12);                     // +1 octave
    r.process(noteOnBuf(1, 60, 100), b);        // play C4 -> AMY should get 72
    REQUIRE(b.netOnFor(1, 72) == 1);            // transposed note reached the backend
    REQUIRE(b.netOnFor(1, 60) == 0);            // NOT the untransposed note
    r.process(noteOffBuf(1, 60), b);            // note-off (untransposed) must release 72
    REQUIRE(b.totalNetOn() == 0);
    REQUIRE_FALSE(r.anyActive());
}

TEST_CASE("Mono mode: last-note priority with retrigger", "[router][mono]")
{
    NoteRouter r; MockBackend b; r.setVoiceMode(1);    // Mono
    r.process(noteOnBuf(1, 60, 100), b);               // 60 sounds
    r.process(noteOnBuf(1, 64, 100), b);               // 64 steals; 60 still held
    r.process(noteOffBuf(1, 64), b);                   // release 64 -> resume 60
    r.process(noteOffBuf(1, 60), b);                   // release 60 -> silence

    // Mono retriggers: each change is off(old) + on(new). Full sequence:
    // on60, off60, on64, off64, on60, off60.
    REQUIRE(b.notes.size() == 6);
    REQUIRE((b.notes[0].on  && b.notes[0].note == 60));
    REQUIRE((!b.notes[1].on && b.notes[1].note == 60));   // retrigger: 60 released
    REQUIRE((b.notes[2].on  && b.notes[2].note == 64));
    REQUIRE((b.notes[4].on  && b.notes[4].note == 60));   // 60 resumes on release of 64
    REQUIRE(b.totalNetOn() == 0);
    REQUIRE_FALSE(r.anyActive());
}

TEST_CASE("Mono mode: releasing a held (non-top) note keeps the top sounding", "[router][mono]")
{
    NoteRouter r; MockBackend b; r.setVoiceMode(1);
    r.process(noteOnBuf(1, 60, 100), b);
    r.process(noteOnBuf(1, 67, 100), b);               // 67 sounds, 60 held silently
    const auto n = b.notes.size();
    r.process(noteOffBuf(1, 60), b);                   // release the silent held note
    REQUIRE(b.notes.size() == n);                      // -> no new note events
    r.process(noteOffBuf(1, 67), b);                   // release the sounding note -> silence
    REQUIRE(b.totalNetOn() == 0);
    REQUIRE_FALSE(r.anyActive());
}

TEST_CASE("Legato mode: overlapping notes do not retrigger", "[router][legato]")
{
    NoteRouter r; MockBackend b; r.setVoiceMode(2);    // Legato
    r.process(noteOnBuf(1, 60, 100), b);
    r.process(noteOnBuf(1, 64, 100), b);               // slur: no note-off between them
    REQUIRE(b.notes.size() == 2);
    REQUIRE((b.notes[0].on && b.notes[0].note == 60));
    REQUIRE((b.notes[1].on && b.notes[1].note == 64)); // straight to 64, no off60 (AMY glides/reuses)

    r.process(noteOffBuf(1, 64), b);                   // resume 60 (still no retrigger off)
    r.process(noteOffBuf(1, 60), b);                   // final release frees the voice
    REQUIRE_FALSE(r.anyActive());                      // nothing left sounding -> no hang
    REQUIRE_FALSE(b.notes.back().on);                  // last event is a note-off
}

TEST_CASE("Mono mode still flushes on panic / transport stop (no hang)", "[router][mono]")
{
    NoteRouter r; MockBackend b; r.setVoiceMode(2);    // Legato (the no-off-on-overlap case)
    r.process(noteOnBuf(1, 60, 100), b);
    r.process(noteOnBuf(1, 62, 100), b);
    r.process(noteOnBuf(1, 64, 100), b);
    r.allNotesOff(&b);                                 // panic
    REQUIRE_FALSE(r.anyActive());
    REQUIRE(b.allNotesOffCount == 1);
}

// A structural rebuild (engine/patch/voice-mode switch) flushes the router the same
// way panic does. This pins the invariant the processor's rebuild-flush relies on:
// after the flush the mono held-stack is EMPTY, so a stale release resurrects nothing
// and the next note starts clean — no phantom "stuck" note carried across the switch.
TEST_CASE("allNotesOff clears the mono held-stack (no phantom note after a rebuild)", "[router][mono]")
{
    NoteRouter r; MockBackend b; r.setVoiceMode(1);    // Mono
    r.process(noteOnBuf(1, 60, 100), b);               // 60 sounds
    r.process(noteOnBuf(1, 64, 100), b);               // 64 steals; 60 held underneath
    REQUIRE(r.anyActive());

    r.allNotesOff(&b);                                 // <- the rebuild flush
    REQUIRE_FALSE(r.anyActive());
    REQUIRE(b.totalNetOn() == 0);

    // Releasing the previously-stacked 60 must be a no-op (it's no longer held), NOT a
    // resume that resurrects it as a sounding voice.
    r.process(noteOffBuf(1, 60), b);
    REQUIRE_FALSE(r.anyActive());
    REQUIRE(b.totalNetOn() == 0);

    // A brand-new note starts cleanly: exactly one voice sounding, nothing phantom.
    r.process(noteOnBuf(1, 67, 100), b);
    REQUIRE(r.anyActive());
    REQUIRE(b.totalNetOn() == 1);                      // one on, no resurrected 60/64
    REQUIRE(b.netOnFor(1, 67) == 1);
}
