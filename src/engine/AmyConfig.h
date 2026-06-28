// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// AmyConfig.h — compile-time facts about the embedded AMY engine.
//
// We keep these in one place so the rest of the plugin never hard-codes them.
// Values confirmed against third_party/amy/src/amy.h on 2026-06-28 for the
// desktop (non-MCU, non-Emscripten) configuration we build:
//   AMY_BLOCK_SIZE 256 · AMY_SAMPLE_RATE 44100 · AMY_NCHANS 2 ·
//   typedef int16_t output_sample_type   (amy.h:218)
// If AMY's headers ever change these defaults, re-grep and update here:
//   grep -nE 'AMY_BLOCK_SIZE|AMY_SAMPLE_RATE|AMY_NCHANS|output_sample_type' \
//        third_party/amy/src/*.h

#include <cstdint>

namespace amyplug
{
// --- Confirmed against AMY headers (amy.h) --------------------------------
inline constexpr int   kAmySampleRate = 44100;  // AMY_SAMPLE_RATE
inline constexpr int   kAmyBlockSize  = 256;    // AMY_BLOCK_SIZE (frames per fill)
inline constexpr int   kAmyChannels   = 2;      // AMY_NCHANS (interleaved stereo)
using AmySample = std::int16_t;                 // output_sample_type
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
