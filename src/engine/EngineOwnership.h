// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// EngineOwnership — process-wide arbitration for AMY's single global engine.
//
// AMY is ONE static engine shared by every plugin instance in the process (see
// docs/ENGINE_NOTES.md §4). If two Software-mode instances both render and stream
// wire messages, they fight over the same synths and sample clock -> "audio mayhem"
// (e.g. duplicating a live track). The output of amy_simple_fill_buffer() is a
// single global mix, so N renderers can't coexist even by routing to different
// synths — someone has to own it.
//
// Rule: exactly ONE instance owns the software engine at a time. The owner renders
// and streams normally; every other instance goes fully silent and does NOT touch
// global AMY (no notes, no wires, no render) until it is either handed the engine
// (the owner releases / is destroyed) or the user explicitly takes it over.
//
// The token is an opaque `const void*` (the owning processor's `this`). All ops are
// lock-free atomics, so the claim/relinquish CAS is safe to run on the audio thread.

#include <atomic>

namespace amyplug::engineown
{
// One definition across all TUs (C++17 inline variable). nullptr == engine is free.
inline std::atomic<const void*> softwareOwner { nullptr };

// The physical AMYboard is ALSO a single shared resource: N instances all opening its
// MIDI/serial and firing notes stack the same note -> clipping/"bitcrush", and they fight
// over the serial port. So the board has its own single-owner token, mirroring software.
// Only the owner opens the port and drives the board; everyone else stays hands-off.
inline std::atomic<const void*> hardwareOwner { nullptr };

// Claim the engine iff it's free (or already ours). Returns true if `me` owns it
// after the call. RT-safe (single atomic CAS + load).
inline bool claimSoftwareIfFree(const void* me) noexcept
{
    const void* expected = nullptr;
    if (softwareOwner.compare_exchange_strong(expected, me))
        return true;
    return softwareOwner.load() == me;   // already ours?
}

// User-initiated hand-over: seize ownership unconditionally. The previous owner
// discovers it lost the engine on its next block and falls silent.
inline void forceClaimSoftware(const void* me) noexcept { softwareOwner.store(me); }

// Relinquish iff we currently hold it (mode switch to Hardware, or destruction).
inline void releaseSoftware(const void* me) noexcept
{
    const void* expected = me;
    softwareOwner.compare_exchange_strong(expected, nullptr);
}

inline bool ownsSoftware(const void* me) noexcept { return softwareOwner.load() == me; }

// --- hardware (AMYboard) ownership: same contract as software ---------------
inline bool claimHardwareIfFree(const void* me) noexcept
{
    const void* expected = nullptr;
    if (hardwareOwner.compare_exchange_strong(expected, me))
        return true;
    return hardwareOwner.load() == me;   // already ours?
}
inline void releaseHardware(const void* me) noexcept
{
    const void* expected = me;
    hardwareOwner.compare_exchange_strong(expected, nullptr);
}
inline bool ownsHardware(const void* me) noexcept { return hardwareOwner.load() == me; }
} // namespace amyplug::engineown
