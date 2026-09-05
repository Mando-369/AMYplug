// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstddef>

// Tooltips.h — every hover string in one place.
//
// Header-only on purpose: the table is pure data, and AmyPlugEditor.cpp / PresetsPage.cpp
// are compiled into six different targets (plugin, FX plugin, two snapshot tools, two test
// binaries). A .cpp would have to be added to all six, and forgetting one is a link error
// nobody sees until CI.
//
// House style for a tip: ONE line, no full stop unless a second sentence earns it, and it
// says what the control DOES — not what it is called. A tip that only re-reads the label
// ("Level: the level") is worse than none, so controls whose name already says it are
// deliberately absent from the table; `forParam` returns an empty string and JUCE then
// shows nothing.
namespace amyplug::tips
{
// --- header / global chrome ------------------------------------------------
inline constexpr auto help     = "Show or hide these tooltips";
inline constexpr auto size     = "Editor size - or drag the bottom-right corner";
inline constexpr auto preset   = "Loaded preset - click to browse. A * means it has been edited.";
inline constexpr auto prev     = "Previous preset - factory then user, wrapping round";
inline constexpr auto next     = "Next preset - factory then user, wrapping round";
inline constexpr auto save     = "Save the current sound as a user preset, in a bank you choose";
inline constexpr auto import   = "Import a DX7 .syx cartridge as named FM user patches";
inline constexpr auto toEditor = "Load this factory preset's settings into the editable Juno / DX7 tab";
inline constexpr auto engine   = "Which engine drives synth 1: Factory preset, Analog (Juno tab), or FM (DX7 tab)";
inline constexpr auto panic    = "All notes off on every channel - clears a stuck note";
inline constexpr auto outGain  = "Output level at the very end of the chain, after the FX and master stage";
inline constexpr auto status   = "What is making sound right now: this instance, another one, or the board";
inline constexpr auto takeover = "Move the one shared AMY engine to this instance";

// --- tabs, in the order they are added -------------------------------------
inline constexpr const char* tabs[] = {
    "Browse, save and organise presets and banks",
    "Analog engine: four oscillators, filter, LFO and envelopes",
    "FM: algorithm, feedback and per-operator tuning",
    "FM: the 4-rate / 4-level envelope of OP 1-3",
    "FM: the 4-rate / 4-level envelope of OP 4-6",
    "FM: pitch envelope, LFO, tremolo routing and transpose",
    "Global effects and the master output stage",
    "Drive a real AMYboard: MIDI carries the notes, serial carries the edits"
};

// --- PRESETS tab -----------------------------------------------------------
inline constexpr auto pSearch   = "Filter the list by name";
inline constexpr auto pTree     = "Pick a source or a bank to narrow the list";
inline constexpr auto pList     = "Click a preset to load it";
inline constexpr auto pSave     = "Overwrite the loaded user preset with what you hear now";
inline constexpr auto pSaveAs   = "Save the current sound under a new name, in a bank you choose";
inline constexpr auto pRename   = "Rename the selected user preset";
inline constexpr auto pMove     = "Move the selected user preset into another bank";
inline constexpr auto pDelete   = "Move the selected user preset to the Trash";
inline constexpr auto pNewBank  = "Create an empty bank - a folder in the preset library";
inline constexpr auto pReveal   = "Open the user preset folder in the Finder";

// --- AMYboard tab ----------------------------------------------------------
inline constexpr auto hwMidi     = "The board's USB-MIDI port - this is what carries the notes";
inline constexpr auto hwSerial   = "The board's serial REPL port - this is what carries patch and parameter edits";
inline constexpr auto hwRefresh  = "Rescan the MIDI and serial ports";
inline constexpr auto hwDetect   = "Probe the serial ports for the AMYboard's REPL";
inline constexpr auto hwConnect  = "Open both ports and switch to Hardware mode - the plugin itself goes silent";
inline constexpr auto hwDisconn  = "Close the ports and go back to the built-in engine";
inline constexpr auto hwSend     = "Push the patch you have here to the board";
inline constexpr auto hwFirmware = "Read the board's firmware and check GitHub for a newer AMYboard build";
inline constexpr auto hwFlash    = "Open the AMYboard WebSerial flasher to update the firmware";

namespace detail
{
struct Row { const char* key; const char* tip; };

// Whole parameter ids. Linear scan: this runs a few hundred times while the editor is
// built and never again, so a map would buy nothing but a static initialiser.
inline constexpr Row byId[] = {
    // --- voicing / levels (both engines) ---
    { "master_volume",  "AMY's engine volume, upstream of the effects. 10 is unity." },
    { "output_gain",    "Output gain at the very end of the chain, in dB" },
    { "num_voices",     "Polyphony - how many notes can sound at once" },
    { "voice_mode",     "Poly, Mono (each note retriggers) or Legato (glides, no retrigger)" },
    { "glide",          "Portamento: time to slide from the previous note, in ms" },
    { "unison_voices",  "Detuned copies stacked on every note. Analog engine only." },
    { "unison_detune",  "How far the unison copies spread, in cents" },

    // --- amp envelope (Analog; also the FM voice's master output envelope) ---
    { "amp_attack",     "Amplitude envelope: time to reach full level" },
    { "amp_decay",      "Amplitude envelope: time to fall from full level to sustain" },
    { "amp_sustain",    "Level the note holds for as long as the key is down" },
    { "amp_release",    "Amplitude envelope: time to fade out after key-up" },

    // --- filter ---
    { "filter_cutoff",  "Filter cutoff frequency" },
    { "filter_reso",    "Filter resonance - emphasis right at the cutoff" },
    { "vcf_type",       "Filter shape. LPF24 is the steeper (4-pole) low-pass." },
    { "vcf_kbd",        "Keyboard tracking: how far the cutoff follows the note you play" },
    { "vcf_env",        "How far the filter envelope opens the cutoff" },
    { "vcf_attack",     "Filter envelope: time to reach the full cutoff sweep" },
    { "vcf_decay",      "Filter envelope: time to fall from the peak to sustain" },
    { "vcf_sustain",    "Cutoff level the filter envelope holds while the key is down" },
    { "vcf_release",    "Filter envelope: time to fall back after key-up" },

    // --- analog LFO ---
    { "lfo_mode",       "Poly = one LFO per voice, Free = one shared, Key = restarts on each note, Sync = locked to the host tempo" },
    { "lfo_wave",       "LFO waveform" },
    { "lfo_freq",       "LFO speed in Hz. Ignored in Sync mode - the division below takes over." },
    { "lfo_sync_rate",  "Note division the LFO locks to. Sync mode only." },
    { "lfo_to_pitch",   "LFO to pitch - vibrato" },
    { "lfo_to_pwm",     "LFO to pulse width. Only audible on a Pulse waveform." },
    { "lfo_to_filter",  "LFO to filter cutoff - wobble" },

    // --- FM globals ---
    { "fm_algorithm",   "Which of the 32 DX7 algorithms wires the six operators together" },
    { "fm_feedback",    "Feedback on the algorithm's feedback operator. Capped where AMY stays stable." },
    { "fm_transpose",   "Transpose the whole voice, in semitones. Operators in Fixed mode do not move." },
    { "fm_lfo_speed",   "LFO speed, in the DX7's own 0-99 units" },
    { "fm_lfo_wave",    "LFO waveform" },
    { "fm_lfo_pmd",     "Pitch modulation depth. Silent until Vib Sens is above 0." },
    { "fm_lfo_pms",     "How sensitive the voice is to the pitch depth. 0 means no vibrato at all." },
    { "fm_lfo_amd",     "Tremolo depth. Reaches only the operators whose AMS is on, below." },

    // --- FX ---
    { "eq_low",         "Low shelf, in dB" },
    { "eq_mid",         "Mid band, in dB" },
    { "eq_high",        "High shelf, in dB" },
    { "echo_level",     "How much echo is mixed in" },
    { "echo_time",      "Delay between repeats, in ms" },
    { "echo_feedback",  "How much of each repeat is fed back in - how many you hear" },
    { "echo_tone",      "Tone of the repeats: right of centre darkens them, left thins them" },
    { "chorus_level",   "How much chorus is mixed in" },
    { "chorus_rate",    "Chorus LFO speed, in Hz" },
    { "chorus_depth",   "How far the chorus detunes" },
    { "reverb_level",   "How much reverb is mixed in" },
    { "reverb_size",    "Size and liveness of the space - how long the tail runs" },
    { "reverb_damping", "How fast the highs fade out of the tail" },
    { "bc_freq",        "Sample rate the crusher drops to. 48k is clean." },
    { "bc_bits",        "Bit depth the crusher drops to. 16 is clean." },
    { "clip_drive",     "Diode saturator drive, gain-compensated so the level holds. 0 dB is its natural character." },

    // --- FX on/off (the power switch in each card's header bar) ---
    { "reverb_on",      "Reverb on or off. Off is silent, not removed - the knobs stay where you left them." },
    { "chorus_on",      "Chorus on or off" },
    { "echo_on",        "Echo on or off" },
    { "eq_on",          "EQ on or off. Off is flat, not bypassed." },
    { "crush_on",       "Bit crusher on or off" },
    { "clip_on",        "Diode clipper on or off. Off still keeps the 0 dBFS ceiling." },
};

// Analog oscillators A-D: the same six controls four times over, so they are matched on
// the suffix rather than written out sixteen times.
inline constexpr Row oscSuffix[] = {
    { "wave",   "Waveform of this oscillator" },
    { "freq",   "This oscillator's pitch at A4 (note 69); it tracks the keyboard from there. 440 is normal, 220 an octave down." },
    { "coarse", "Transpose this oscillator, in semitones" },
    { "fine",   "Detune this oscillator, in cents" },
    { "duty",   "Pulse width. Only affects the Pulse waveform." },
    { "level",  "How much of this oscillator reaches the filter" },
};

// Per-operator FM controls: the field after `fm_op<N>_`.
inline constexpr Row fmOpField[] = {
    { "fixed",  "Ratio follows the note you play; Fixed holds one frequency whatever you play" },
    { "coarse", "Frequency ratio in whole steps. 0 is half the note's pitch." },
    { "fine",   "Frequency ratio between the coarse steps" },
    { "detune", "Slight detune against the other operators. 7 is dead centre." },
    { "outlvl", "Operator output level. A carrier at 0 is silent; a modulator at 0 stops modulating." },
    { "vel",    "How much key velocity scales this operator. 0 ignores velocity." },
    { "ams",    "How far the LFO tremolo reaches this operator. Off keeps it out." },
    { "r1",     "Rate 1: how fast the operator climbs to Level 1. Higher is faster." },
    { "r2",     "Rate 2: how fast it moves from Level 1 to Level 2" },
    { "r3",     "Rate 3: how fast it moves from Level 2 to Level 3" },
    { "r4",     "Rate 4: how fast it falls to Level 4 after key-up" },
    { "l1",     "Level 1: the peak the attack reaches" },
    { "l2",     "Level 2: where it settles after the first decay" },
    { "l3",     "Level 3: the level held while the key is down" },
    { "l4",     "Level 4: where the note ends. Normally 0." },
};

// Global pitch envelope: same 4R/4L shape, but 50 is "no shift" rather than silence.
inline constexpr Row fmPitchEgField[] = {
    { "r1", "Pitch envelope Rate 1. Higher is faster." },
    { "r2", "Pitch envelope Rate 2. Higher is faster." },
    { "r3", "Pitch envelope Rate 3. Higher is faster." },
    { "r4", "Pitch envelope Rate 4 - the fall after key-up" },
    { "l1", "Pitch envelope Level 1. 50 is no shift; above bends up, below bends down." },
    { "l2", "Pitch envelope Level 2. 50 is no shift." },
    { "l3", "Pitch envelope Level 3 - held while the key is down. 50 is no shift." },
    { "l4", "Pitch envelope Level 4 - where the note ends. 50 is no shift." },
};

template <std::size_t N>
inline juce::String lookUp(const Row (&table)[N], juce::StringRef key)
{
    for (const auto& r : table)
        if (key == juce::StringRef(r.key)) return r.tip;
    return {};
}
} // namespace detail

// ⚠️ Set a tip on a whole subtree, not just the component you name.
//
// The hovered component is whatever is topmost under the mouse, which for a compound
// control is a CHILD: a Slider's LCD value box, a ComboBox's text label, a ListBox's
// viewport. Setting the tip only on the parent means the tip appears over some parts of a
// control and not others, which reads as flaky rather than as missing.
//
// It also works around Slider seeding its value box from the owner's tooltip at
// CONSTRUCTION time and never again (juce_Slider.cpp:606) — by the time a caller can call
// setTooltip, that copy has already happened with an empty string.
inline void applyDeep(juce::Component& c, const juce::String& tip)
{
    if (tip.isEmpty()) return;
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(&c)) client->setTooltip(tip);
    for (auto* kid : c.getChildren()) applyDeep(*kid, tip);
}

// The tip for one parameter id, or an empty string if it has none (JUCE then shows no
// tooltip at all, which is the right answer for a control whose label already says it).
inline juce::String forParam(const juce::String& id)
{
    // Per-operator FM: fm_op<N>_<field>.
    if (id.startsWith("fm_op"))
        return detail::lookUp(detail::fmOpField, id.fromFirstOccurrenceOf("_", false, false)
                                                   .fromFirstOccurrenceOf("_", false, false));
    if (id.startsWith("fm_pitcheg_"))
        return detail::lookUp(detail::fmPitchEgField, id.fromLastOccurrenceOf("_", false, false));
    // Analog oscillators: osc_<a-d>_<field>.
    if (id.startsWith("osc_"))
        return detail::lookUp(detail::oscSuffix, id.fromLastOccurrenceOf("_", false, false));
    return detail::lookUp(detail::byId, id);
}
} // namespace amyplug::tips
