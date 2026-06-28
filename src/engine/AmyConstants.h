// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
// AmyConstants.h — typed mirror of AMY enum values we use from C++.
// Source of truth is third_party/amy/src + amy/constants.py. Keep in sync.
#include <cstdint>

namespace amyplug::amy
{
// `wave` (w) — oscillator/engine type.
enum class Wave : int {
    Sine = 0, Pulse, SawDown, SawUp, Triangle, Noise, KS, PCM, Algo /*FM*/,
    Partial, ByoPartials, InterpPartials, AudioIn0, AudioIn1, AudioExt0, AudioExt1,
    AmyMidi, PcmLeft, PcmRight, Wavetable, Custom, Off
};

// `G`/filter_type.
enum class Filter : int { None = 0, LPF, BPF, HPF, LPF24 };

// Envelope generator type (T / X).
enum class EgType : int { Normal = 0, Linear, Dx7, TrueExp };

// `S`/reset_osc special values (confirm exact numeric values in AMY headers).
enum class Reset : int { Osc = 0, AllOscs, Timebase, Amy, Sequencer, Patch /* RESET_PATCH */ };

// ControlCoefficient source order for amp/freq/filter_freq/duty/pan vectors.
// index: const, note, vel, eg0, eg1, mod, bend, ext0, ext1
enum class Coef : int { Const = 0, Note, Vel, Eg0, Eg1, Mod, Bend, Ext0, Ext1, Count };
} // namespace amyplug::amy
