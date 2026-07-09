// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// FirmwareCheck — asks GitHub for the latest AMYboard firmware and compares it to the
// build a connected board reports. AMYboard firmware is a *rolling* release (no semver):
// a build is identified by "<YYYYMMDD>-<git short hash>". See docs/FIRMWARE_UPDATE_CHECK.md.
//
// The board's own version comes from tulip.version() (HardwareBackend reads it over the
// serial REPL); this module supplies the "what's the latest online" half + the compare.

#include <juce_core/juce_core.h>

namespace amyplug
{
struct FirmwareInfo
{
    juce::String remoteSha;    // commit SHA the release was built from (full or short)
    juce::String remoteDate;   // "YYYYMMDD" from the firmware asset's updated_at
    bool ok = false;           // true if the query succeeded and yielded an identity
};

// Blocking HTTP GET against the tulipcc rolling release. MUST run off the audio/message
// thread (call from a background juce::Thread). Returns ok=false on any failure.
FirmwareInfo fetchLatestAmyboardFirmware();

// True if the online build differs from the board's "YYYYMMDD-<shorthash>" string AND is
// not older than it — i.e. an update is worth offering. False if unknown or up to date.
bool firmwareUpdateAvailable(const juce::String& boardVersion, const FirmwareInfo& latest);

// "YYYYMMDD-abc1234" for display (date + 7-char short hash), or "" if not ok.
juce::String firmwareShortId(const FirmwareInfo& latest);
} // namespace amyplug
