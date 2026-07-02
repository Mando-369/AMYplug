// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "NoteRouter.h"

namespace amyplug
{
void NoteRouter::process(const juce::MidiBuffer& midi, IAmyBackend& backend)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int  ch = juce::jlimit(1, 16, m.getChannel());

        if (m.isNoteOn())
            noteOn(ch, m.getNoteNumber(), m.getFloatVelocity(), backend);
        else if (m.isNoteOff())
            noteOff(ch, m.getNoteNumber(), backend);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allNotesOff(&backend);
        else if (m.isPitchWheel())
        {
            // Only forward a bend when the value actually changes — controllers often
            // stream a constant centred value, which floods a hardware board.
            const int pw = m.getPitchWheelValue();
            if (pw != lastPitchWheel)
            {
                lastPitchWheel = pw;
                backend.pitchBend((float) (pw - 8192) / 8192.0f * bendOctaveScale);
            }
        }
        else if (m.isSustainPedalOn())  { sustainDown[ch - 1] = true;  backend.sustainPedal(ch, true); }
        else if (m.isSustainPedalOff())
        {
            sustainDown[ch - 1] = false;
            backend.sustainPedal(ch, false);
            // Flush notes that were waiting for pedal release.
            for (int n = 0; n < kNumNotes; ++n)
                if (heldBySustain[ch - 1].test(n)) { heldBySustain[ch - 1].reset(n); noteOff(ch, n, backend); }
        }
        // TODO: map other CCs (mod wheel, expression) to AMY CtrlCoefs via PatchModel.
    }
}

void NoteRouter::noteOn(int ch, int note, float vel, IAmyBackend& b)
{
    if (vel <= 0.0f) { noteOff(ch, note, b); return; }     // running-status note-off
    heldVel[note] = vel;
    if (voiceMode != 0) { monoOn(ch, note, vel, b); return; }
    if (! active[ch - 1].test(note)) { active[ch - 1].set(note); ++activeCount; }
    heldBySustain[ch - 1].reset(note);
    b.noteOn(ch, note, vel);                               // AMY synth == MIDI channel
}

void NoteRouter::noteOff(int ch, int note, IAmyBackend& b)
{
    if (sustainDown[ch - 1]) { heldBySustain[ch - 1].set(note); return; } // defer
    if (voiceMode != 0) { monoOff(ch, note, b); return; }
    if (active[ch - 1].test(note)) { active[ch - 1].reset(note); --activeCount; }
    b.noteOff(ch, note);
}

// --- Mono / Legato (last-note priority) ------------------------------------
void NoteRouter::stackRemove(int ch, int note)
{
    auto& stk = heldStack[ch - 1]; int& n = heldCount[ch - 1];
    int w = 0;
    for (int r = 0; r < n; ++r) if (stk[r] != (uint8_t) note) stk[w++] = stk[r];
    n = w;
}

void NoteRouter::monoSound(int ch, int note, float vel, IAmyBackend& b)
{
    const bool legato = (voiceMode == 2) && (sounding[ch - 1] >= 0);
    const int  prev   = sounding[ch - 1];
    // Mono retrigger: release the old note so its envelope restarts on the new one.
    // Legato: leave it sounding — AMY reuses the single voice and glides without
    // retriggering (so the envelope sustains across the slur).
    if (! legato && prev >= 0)
    {
        b.noteOff(ch, prev);
        if (active[ch - 1].test(prev)) { active[ch - 1].reset(prev); --activeCount; }
    }
    b.noteOn(ch, note, vel);
    if (prev >= 0 && prev != note && active[ch - 1].test(prev)) { active[ch - 1].reset(prev); --activeCount; }
    if (! active[ch - 1].test(note)) { active[ch - 1].set(note); ++activeCount; }
    sounding[ch - 1] = note;
}

void NoteRouter::monoOn(int ch, int note, float vel, IAmyBackend& b)
{
    heldBySustain[ch - 1].reset(note);
    stackRemove(ch, note);                                  // a re-press moves to the top
    auto& stk = heldStack[ch - 1]; int& n = heldCount[ch - 1];
    if (n < kNumNotes) stk[n++] = (uint8_t) note;
    monoSound(ch, note, vel, b);
}

void NoteRouter::monoOff(int ch, int note, IAmyBackend& b)
{
    stackRemove(ch, note);
    if (note != sounding[ch - 1]) return;                   // a held-but-silent note: nothing to do
    const int n = heldCount[ch - 1];
    if (n > 0)
        monoSound(ch, heldStack[ch - 1][n - 1], heldVel[heldStack[ch - 1][n - 1]], b);  // resume newest held
    else
    {
        b.noteOff(ch, note);
        if (active[ch - 1].test(note)) { active[ch - 1].reset(note); --activeCount; }
        sounding[ch - 1] = -1;
    }
}

void NoteRouter::allNotesOff(IAmyBackend* backend)
{
    if (backend != nullptr)
    {
        backend->allNotesOff();
        // Also emit explicit offs for everything we tracked, in case the backend's
        // global all-notes-off ever misses (defensive).
        for (int ch = 0; ch < kNumChannels; ++ch)
            for (int n = 0; n < kNumNotes; ++n)
                if (active[ch].test(n)) backend->noteOff(ch + 1, n);
    }
    for (auto& a : active)        a.reset();
    for (auto& h : heldBySustain) h.reset();
    sustainDown.fill(false);
    activeCount = 0;
    heldCount.fill(0);
    sounding.fill(-1);
}

void NoteRouter::updateTransport(bool isPlaying, IAmyBackend& backend)
{
    if (wasPlaying && ! isPlaying)
        allNotesOff(&backend);     // falling edge: kill anything still sounding
    wasPlaying = isPlaying;
}
} // namespace amyplug
