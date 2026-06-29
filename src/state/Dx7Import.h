// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// Dx7Import — parse a standard DX7 SysEx (.syx) cartridge and convert each voice
// into our FM engine's PatchModel. Supports the 32-voice bulk dump (packed,
// 128 bytes/voice) and the single-voice VCED dump (155 bytes), with or without
// the F0..F7 SysEx wrapper.
//
// The conversion math is a faithful C++ port of AMY's own DX7 converter
// (third_party/amy/amy/fm.py) — the same code that generated AMY's factory DX7
// bank — so imported voices sound consistent with the built-ins. The operator
// ordering matches PatchModel::emitFm's `O6,5,4,3,2,1` (our FmParams.ops[0] is
// DX7 operator 1).
//
// v1 fidelity: operator ratios, output levels, algorithm and feedback are exact;
// the DX7 4-rate/4-level envelope is approximated as our per-op A/D/S/R; LFO/
// vibrato, keyboard scaling, velocity sensitivity and fixed-frequency operators
// are dropped or approximated (see the .cpp).

#include "PatchModel.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace amyplug
{
struct Dx7Voice
{
    juce::String         name;   // DX7 voice name (10 chars, trimmed)
    PatchModel::FmParams fm;     // converted 6-operator graph
};

class Dx7Import
{
public:
    // Parse a DX7 SysEx blob. Returns the voices found (empty if unrecognized).
    static std::vector<Dx7Voice> parse(const void* data, size_t size);

    // Convenience: read a .syx file from disk.
    static std::vector<Dx7Voice> parseFile(const juce::File& file);

    // Build a full FM-engine PatchModel from one converted voice.
    static PatchModel toPatchModel(const Dx7Voice& voice);
};
} // namespace amyplug
