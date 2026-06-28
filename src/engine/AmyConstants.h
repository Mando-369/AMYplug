// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
// AmyConstants.h — typed mirror of AMY enum values we use from C++.
// Source of truth is third_party/amy/src + amy/constants.py. Keep in sync.
#include <cstdint>

namespace amyplug::amy
{
// `wave` (w) — oscillator/engine type. Values confirmed against amy.h:250-273.
enum class Wave : int {
    Sine = 0, Pulse = 1, SawDown = 2, SawUp = 3, Triangle = 4, Noise = 5,
    KS = 6, PCM = 7, Algo /*FM*/ = 8, Partial = 9, ByoPartials = 10,
    InterpPartials = 11, AudioIn0 = 12, AudioIn1 = 13, AudioExt0 = 14,
    AudioExt1 = 15, AmyMidi = 16, PcmLeft = 17, PcmRight = 18, Wavetable = 19,
    Silent = 20, Custom = 21, Off = 22 /* WAVE_OFF */
};

// `G`/filter_type. Confirmed against amy.h:244-248 (FILTER_*).
enum class Filter : int { None = 0, LPF = 1, BPF = 2, HPF = 3, LPF24 = 4 };

// Envelope generator type (T / X). Confirmed against amy.h:294-297 (ENVELOPE_*).
enum class EgType : int { Normal = 0, Linear = 1, Dx7 = 2, TrueExp = 3 };

// `S`/reset — these are BITMASK flags, not an ordinal enum. Confirmed against
// amy.h:305-313 (RESET_*). Combine with bitwise-or if ever needed.
enum class Reset : int {
    Sequencer = 4096,    // RESET_SEQUENCER
    AllOscs   = 8192,    // RESET_ALL_OSCS    (hard-reset all oscillators)
    Timebase  = 16384,   // RESET_TIMEBASE
    Amy       = 32768,   // RESET_AMY         (full engine reset)
    Events    = 65536,   // RESET_EVENTS
    AllNotes  = 131072,  // RESET_ALL_NOTES   (release every note on every synth)
    Synths    = 262144,  // RESET_SYNTHS      (tear down synths/voices/oscs pre-load)
    Patch     = 524288,  // RESET_PATCH
    Queue     = 1048576  // RESET_QUEUE
};

// ControlCoefficient source order for amp/freq/filter_freq/duty/pan vectors.
// index: const, note, vel, eg0, eg1, mod, bend, ext0, ext1
enum class Coef : int { Const = 0, Note, Vel, Eg0, Eg1, Mod, Bend, Ext0, Ext1, Count };
} // namespace amyplug::amy
