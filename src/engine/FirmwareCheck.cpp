// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "FirmwareCheck.h"

namespace amyplug
{
namespace
{
// The canonical endpoint for CURRENT boards is the fixed `amyboard` tag; `releases/latest`
// mirrors the same assets for legacy boards. Try the canonical one first, then fall back.
const char* kEndpoints[] = {
    "https://api.github.com/repos/shorepine/tulipcc/releases/tags/amyboard",
    "https://api.github.com/repos/shorepine/tulipcc/releases/latest",
};

// A 40-char (or 7+) lower/upper hex run — used to validate target_commitish and to pull the
// SHA out of the release body ("built from main @ 0804b4d…") when needed.
bool isHex(juce::juce_wchar c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool looksLikeSha(const juce::String& s)
{
    if (s.length() < 7) return false;
    for (auto c : s) if (! isHex(c)) return false;
    return true;
}

juce::String shaFromBody(const juce::String& body)
{
    const int at = body.indexOf("@ ");
    if (at < 0) return {};
    juce::String run;
    for (int i = at + 2; i < body.length(); ++i)
    {
        const juce::juce_wchar c = body[i];
        if (isHex(c)) run += juce::String::charToString(c);
        else break;
    }
    return looksLikeSha(run) ? run : juce::String();
}

FirmwareInfo parseRelease(const juce::var& json)
{
    FirmwareInfo info;
    if (! json.isObject()) return info;

    info.remoteSha = json.getProperty("target_commitish", {}).toString();
    if (! looksLikeSha(info.remoteSha))                       // e.g. a branch name ("main")
        info.remoteSha = shaFromBody(json.getProperty("body", {}).toString());

    if (auto* assets = json.getProperty("assets", {}).getArray())
        for (auto& a : *assets)
            if (a.getProperty("name", {}).toString() == "amyboard-firmware-AMYBOARD.bin")
            {
                // updated_at "2026-07-08T16:12:06Z" -> "20260708"
                const auto ts = a.getProperty("updated_at", {}).toString();
                info.remoteDate = ts.substring(0, 10).removeCharacters("-");
            }

    info.ok = info.remoteSha.isNotEmpty();
    return info;
}
} // namespace

FirmwareInfo fetchLatestAmyboardFirmware()
{
    for (auto* endpoint : kEndpoints)
    {
        juce::URL url(endpoint);
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders("User-Agent: AMYplug\r\n"
                                             "Accept: application/vnd.github+json")
                           .withConnectionTimeoutMs(10000);

        std::unique_ptr<juce::InputStream> stream(url.createInputStream(options));
        if (stream == nullptr) continue;

        const auto info = parseRelease(juce::JSON::parse(stream->readEntireStreamAsString()));
        if (info.ok) return info;
    }
    return {};
}

bool firmwareUpdateAvailable(const juce::String& boardVersion, const FirmwareInfo& latest)
{
    if (! latest.ok || boardVersion.isEmpty()) return false;

    const int dash = boardVersion.indexOfChar('-');
    if (dash < 0) return false;
    const auto boardDate = boardVersion.substring(0, dash);       // "20260627"
    const auto boardSha  = boardVersion.substring(dash + 1);      // "abc1234"

    const bool hashDiffers = ! latest.remoteSha.startsWithIgnoreCase(boardSha);
    // Compare date halves only when both are present; a rolling build always has a date.
    const bool remoteNewer = latest.remoteDate.isNotEmpty() && boardDate.isNotEmpty()
                             && latest.remoteDate.getLargeIntValue() > boardDate.getLargeIntValue();
    const bool sameDay     = latest.remoteDate == boardDate;

    // Different commit AND not older than what's installed -> offer the update.
    return hashDiffers && (remoteNewer || sameDay);
}

juce::String firmwareShortId(const FirmwareInfo& latest)
{
    if (! latest.ok) return {};
    const auto sha7 = latest.remoteSha.substring(0, 7).toLowerCase();
    return latest.remoteDate.isNotEmpty() ? (latest.remoteDate + "-" + sha7) : sha7;
}
} // namespace amyplug
