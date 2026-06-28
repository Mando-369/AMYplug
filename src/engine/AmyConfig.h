// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyConfig.h — compile-time facts about the embedded AMY engine.
//
// THESE ARE PLACEHOLDERS. First implementation task (CLAUDE.md §7): after
// `scripts/bootstrap.sh`, read third_party/amy/src/*.h and replace the values
// below with the real ones, then delete this notice. Grep:
//   grep -nE 'AMY_BLOCK_SIZE|AMY_SAMPLE_RATE|AMY_NCHANS|output_sample_type' \
//        third_party/amy/src/*.h
//
// We keep these in one place so the rest of the plugin never hard-codes them.

#include <cstdint>

namespace amyplug
{
// --- Confirm against AMY headers ------------------------------------------
inline constexpr int   kAmySampleRate = 44100;  // AMY_SAMPLE_RATE (compile-time in AMY)
inline constexpr int   kAmyBlockSize  = 256;    // AMY_BLOCK_SIZE (frames per fill)
inline constexpr int   kAmyChannels   = 2;      // AMY renders interleaved stereo
// AMY's sample type is typically int16_t; confirm `output_sample_type`.
using AmySample = std::int16_t;
// --------------------------------------------------------------------------

// AMY SysEx manufacturer id used to wrap wire messages for the AMYboard.
inline constexpr std::uint8_t kAmySysexId0 = 0x00;
inline constexpr std::uint8_t kAmySysexId1 = 0x03;
inline constexpr std::uint8_t kAmySysexId2 = 0x45;

// AMY patch-number bank layout (see docs/AMY_WIRE_PROTOCOL.md).
inline constexpr int kJunoPatchBase = 0;     // 0..127
inline constexpr int kDx7PatchBase  = 128;   // 128..255
inline constexpr int kPianoPatch    = 256;
inline constexpr int kUserPatchBase = 1024;  // 1024..1055
inline constexpr int kUserPatchMax  = 1055;
} // namespace amyplug
