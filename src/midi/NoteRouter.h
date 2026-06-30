// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// NoteRouter — the reason this plugin exists: deterministic note lifecycle so we
// NEVER hang a note. It is the single owner of "what is currently sounding".
//
// Responsibilities:
//   * Translate the host MidiBuffer into backend note on/off/CC each block.
//   * Track every active note (per MIDI channel / AMY synth).
//   * Guarantee a matching note-off: if a note-on is replaced/stolen, or the
//     transport stops, or the plugin is bypassed/reset, or the user hits Panic,
//     force note-offs for all tracked notes.
//   * Handle sustain pedal (defer note-offs while held; flush on release).
//
// All methods here run on the audio thread and must be allocation/lock free.

#include "../engine/IAmyBackend.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <bitset>

namespace amyplug
{
class NoteRouter
{
public:
    static constexpr int kNumChannels = 16;
    static constexpr int kNumNotes    = 128;

    NoteRouter() { sounding.fill(-1); }   // -1 = nothing sounding (0 is a valid note)

    void prepare() { allNotesOff(nullptr); }

    // Process one block of incoming MIDI, dispatching to `backend`.
    void process(const juce::MidiBuffer& midi, IAmyBackend& backend);

    // Force-release everything we think is sounding. Call on transport stop,
    // bypass, reset, mode switch, prepareToPlay, and the Panic button.
    void allNotesOff(IAmyBackend* backend);

    // Called every block with the host transport state; on a falling edge
    // (playing -> stopped) we flush, which kills sustained/hung notes.
    void updateTransport(bool isPlaying, IAmyBackend& backend);

    // Pitch-bend range in semitones (default ±2). AMY's pitch_bend is in octaves,
    // so we convert: octaves = normalizedWheel * semitones / 12.
    void setPitchBendRangeSemitones(float semitones)
    {
        bendOctaveScale = juce::jlimit(1.0f, 24.0f, semitones) / 12.0f;
    }

    // 0 = Poly, 1 = Mono (retrigger), 2 = Legato (glide, no retrigger). In Mono/
    // Legato only the most-recent held note sounds (last-note priority); releasing
    // it resumes the next-newest held note. (The synth is rebuilt to 1 voice so AMY
    // is truly monophonic — see AmyPlugProcessor::syncModelFromParams.)
    void setVoiceMode(int mode) { voiceMode = juce::jlimit(0, 2, mode); }

    bool anyActive() const { return activeCount > 0; }

private:
    void noteOn (int ch, int note, float vel, IAmyBackend& b);
    void noteOff(int ch, int note, IAmyBackend& b);
    void monoOn (int ch, int note, float vel, IAmyBackend& b);
    void monoOff(int ch, int note, IAmyBackend& b);
    void monoSound(int ch, int note, float vel, IAmyBackend& b);   // make `note` the sounding one
    void stackRemove(int ch, int note);

    // active[ch][note] == is this note currently sounding?
    std::array<std::bitset<kNumNotes>, kNumChannels> active {};
    std::array<std::bitset<kNumNotes>, kNumChannels> heldBySustain {}; // pending offs
    std::array<bool, kNumChannels> sustainDown {};
    int   activeCount = 0;
    bool  wasPlaying  = false;
    float bendOctaveScale = 2.0f / 12.0f;   // ±2 semitones default

    // Mono/Legato note-priority state (per channel).
    int voiceMode = 0;
    std::array<std::array<uint8_t, kNumNotes>, kNumChannels> heldStack {}; // order of held notes
    std::array<int, kNumChannels>  heldCount {};                            // depth of heldStack
    std::array<float, kNumNotes>   heldVel {};   // velocity per note (to resume)
    std::array<int, kNumChannels>  sounding {};  // currently sounding note, -1 = none
};
} // namespace amyplug
