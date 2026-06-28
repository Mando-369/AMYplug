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
            backend.pitchBend((float) (m.getPitchWheelValue() - 8192) / 8192.0f * bendOctaveScale);
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
    if (! active[ch - 1].test(note)) { active[ch - 1].set(note); ++activeCount; }
    heldBySustain[ch - 1].reset(note);
    b.noteOn(ch, note, vel);                               // AMY synth == MIDI channel
}

void NoteRouter::noteOff(int ch, int note, IAmyBackend& b)
{
    if (sustainDown[ch - 1]) { heldBySustain[ch - 1].set(note); return; } // defer
    if (active[ch - 1].test(note)) { active[ch - 1].reset(note); --activeCount; }
    b.noteOff(ch, note);
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
}

void NoteRouter::updateTransport(bool isPlaying, IAmyBackend& backend)
{
    if (wasPlaying && ! isPlaying)
        allNotesOff(&backend);     // falling edge: kill anything still sounding
    wasPlaying = isPlaying;
}
} // namespace amyplug
