# Checking for the latest AMYboard firmware

How the plugin can check online for the newest AMYboard firmware and compare it
to the version installed on a connected board.

> **Status — implemented.** The "Check for Firmware Update" button on the AMYboard
> tab does exactly this: it reads the board's build over the serial REPL
> (`tulip.version()`, via `HardwareBackend::requestFirmwareVersion`) *and* fetches
> the latest rolling release from GitHub on a background thread
> (`src/engine/FirmwareCheck.{h,cpp}` — `fetchLatestAmyboardFirmware()` +
> `firmwareUpdateAvailable()`), then shows "up to date" / "update available:
> &lt;date&gt;-&lt;hash&gt;". It never flashes — it points to amyboard.com/editor.

> **TL;DR** — Hit `GET https://api.github.com/repos/shorepine/tulipcc/releases/tags/amyboard`.
> AMYboard firmware is a **rolling release**, so there is *no* semantic version
> number. A build is identified by its **git commit hash + build date**. Compare
> those against the `YYYYMMDD-<shorthash>` string the board already reports.

---

## 1. Where the firmware lives

AMYboard firmware is built and published from the **[`shorepine/tulipcc`](https://github.com/shorepine/tulipcc)**
repository. Every push to `main` triggers `.github/workflows/amyboard-release.yml`,
which compiles the ESP32-S3 firmware and uploads it to a **rolling GitHub release
on a fixed tag literally named `amyboard`**.

Key consequences:

- The release tag never changes (`amyboard`), and it is kept **non-`latest`** on
  purpose. It is updated in place on every commit to `main`.
- There is **no incrementing version number** (no `v1.2.3`). The only stable
  identity of a build is the **git commit SHA** it was compiled from, plus the
  **UTC build date**.
- Stable asset names (do not change; consumed by the web flasher and on-device
  updater):
  - `amyboard-firmware-AMYBOARD.bin` — the OTA/app partition image (~2.7 MB).
  - `amyboard-sys.bin` — the `/sys` data partition (~3.1 MB).
  - `amyboard-full-AMYBOARD.bin` — full flash image for `esptool` (~16.7 MB).
  - `amyboard-full-AMYBOARD-YYYYMMDD.bin` — a date-coded copy of the full image
    (one per release, for manufacturing/provenance).

There is a second endpoint, `releases/latest` (the rolling `tulip` release), which
**mirrors** the same `amyboard-*` assets so that legacy boards (monthly-release
firmware, `<= v-jun-2026`) can still OTA. **Current boards should use
`releases/tags/amyboard`** — it is the canonical source.

---

## 2. How the board reports its installed version (already implemented)

Over USB-MIDI SysEx, running:

```
zPimport amyboard; amyboard.report_version()Z
```

makes the board reply with a **`V` frame**:

```
F0 00 03 45 'V' <ascii version string> F7
```

The version string is produced by `tulip.version()`:

```python
def version():
    # git hash + compiled date
    me = build_strings()
    return me[2].replace("-", "") + "-" + me[1].replace("-dirty", "")
```

So the format is:

```
<YYYYMMDD>-<git_short_hash>      e.g.  20260627-abc1234
```

- First half = UTC **build date**.
- Second half = **git short hash** (the commit the firmware was built from).

The plugin's `HardwareBackend` already retrieves this string.

---

## 3. How to get the latest version online

This is exactly what the board's own updater does. From `get_latest_release()` in
[`tulip/shared/py/tulip.py`](https://github.com/shorepine/tulipcc/blob/main/tulip/shared/py/tulip.py):

```
GET https://api.github.com/repos/shorepine/tulipcc/releases/tags/amyboard
```

The JSON response contains everything needed to compare:

| Field | Use |
| --- | --- |
| `target_commitish` | The **full 40-char commit SHA** the release was built from. |
| `body` | Text: `"Rolling AMYboard release, built from main @ <SHA>. …"` — same SHA, regexable. |
| `assets[].name` | Find `amyboard-firmware-AMYBOARD.bin` (OTA/app) and `amyboard-sys.bin`. |
| `assets[].updated_at` | ISO-8601 timestamp of when the asset was last built/uploaded (the board's own updater displays this as the "latest firmware" date). |
| `assets[].browser_download_url` | Direct download link for flashing. |
| `assets[].digest` | `sha256:…` content hash of the asset. |
| `published_at` / `updated_at` | Release-level timestamps. |

> **Note:** the shipped `amyboard.upgrade()` does **not** actually compare
> versions. It fetches the latest asset URL, prints its `updated_at`, and prompts
> `Upgrade firmware? (Y/N)` — always re-flashing if you say yes. The
> compare-before-offering logic below is something the plugin adds itself.

---

## 4. Comparing latest vs. installed

Because it is a rolling release, comparison is done on **hash** and **date**, not
on a version number.

**A. Same-or-different (precise).** Take the hash half of the board's string
(`abc1234`) and compare it to the first 7 chars of `target_commitish` (or the SHA
regexed out of `body`).

- Equal → **up to date**.
- Different → **an update exists** (hashes alone can't tell you which is newer —
  use the date for direction).

**B. Newer/older (human-friendly).** Compare the date half of the board's string
(`20260627`) against the asset's `updated_at` reformatted to `YYYYMMDD` (or parse
the date-coded asset name `amyboard-full-AMYBOARD-YYYYMMDD.bin`).

- Remote date > local date → **newer firmware available**.
- Same day → fall back to the hash to disambiguate (two builds can share a date).

**Recommended:** use both — the hash tells you *whether* it differs, the date
tells you *which direction* and how stale the board is.

---

## 5. Plugin implementation notes (JUCE / C++)

- **Never on the audio thread.** Do the HTTP request from a background
  `juce::Thread` / `ThreadPoolJob`, or `juce::URL::readEntireTextStream`, behind a
  "Check for updates" button or on editor open. Nothing network-related in
  `processBlock`.
- **GitHub API requires a `User-Agent` header** or it returns `403`. Also send
  `Accept: application/vnd.github+json`.
- **Rate limit:** unauthenticated = **60 requests/hour per IP**. Fine for a manual
  check. Cache the result (~once/day). Optionally send `If-None-Match` with the
  previous `ETag` to get cheap `304 Not Modified` responses. No token required.
- **Parse** with `juce::JSON` / `juce::var`.
- **Two endpoints:** use `releases/tags/amyboard` for current boards;
  `releases/latest` mirrors the same assets for legacy (`<= v-jun-2026`) boards.
- **Board version** comes from the `V` SysEx frame (`report_version()`) — already
  implemented in `HardwareBackend`.
- **Rolling cadence:** "update available" will trigger often (every push to
  `main`). Present the **date + short hash**, not an implied big version jump, and
  keep the check opt-in.
- **Don't flash from the plugin.** Surface "update available" and point the user
  to the WebSerial flasher at [amyboard.com/editor](https://amyboard.com/editor),
  or to on-device `amyboard.upgrade()` over Wi-Fi (interactive; needs credentials).

### Sketch: fetch + compare

```cpp
// Runs on a background thread. Returns true if the online build differs
// from the board's reported version string (e.g. "20260627-abc1234").
struct FirmwareInfo
{
    juce::String remoteSha;      // full 40-char commit SHA
    juce::String remoteDate;     // "YYYYMMDD" from asset updated_at
    juce::String downloadUrl;    // amyboard-firmware-AMYBOARD.bin
    bool ok = false;
};

FirmwareInfo fetchLatestAmyboardFirmware()
{
    FirmwareInfo info;

    juce::URL url ("https://api.github.com/repos/shorepine/tulipcc/releases/tags/amyboard");

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders ("User-Agent: AMYplug\r\n"
                                          "Accept: application/vnd.github+json")
                       .withConnectionTimeoutMs (10000);

    std::unique_ptr<juce::InputStream> stream (url.createInputStream (options));
    if (stream == nullptr)
        return info;

    auto json = juce::JSON::parse (stream->readEntireStreamAsString());
    if (! json.isObject())
        return info;

    info.remoteSha = json.getProperty ("target_commitish", {}).toString();

    if (auto* assets = json.getProperty ("assets", {}).getArray())
    {
        for (auto& a : *assets)
        {
            if (a.getProperty ("name", {}).toString() == "amyboard-firmware-AMYBOARD.bin")
            {
                info.downloadUrl = a.getProperty ("browser_download_url", {}).toString();

                // updated_at looks like "2026-07-08T16:12:06Z" -> "20260708"
                auto ts = a.getProperty ("updated_at", {}).toString();
                info.remoteDate = ts.substring (0, 10).removeCharacters ("-");
            }
        }
    }

    info.ok = info.remoteSha.isNotEmpty();
    return info;
}

// boardVersion is the string from the V frame, e.g. "20260627-abc1234".
bool updateAvailable (const juce::String& boardVersion, const FirmwareInfo& latest)
{
    if (! latest.ok)
        return false;

    auto dash        = boardVersion.indexOfChar ('-');
    auto boardDate    = boardVersion.substring (0, dash);              // "20260627"
    auto boardShaHash = boardVersion.substring (dash + 1);            // "abc1234"

    const bool hashDiffers = ! latest.remoteSha.startsWithIgnoreCase (boardShaHash);
    const bool remoteNewer = latest.remoteDate.getLargeIntValue()
                                 > boardDate.getLargeIntValue();

    // Different commit AND not older than what's installed -> offer the update.
    return hashDiffers && (remoteNewer || latest.remoteDate == boardDate);
}
```

---

## 6. Live example (as of 2026-07-08)

The `releases/latest` (`tulip`) release — which mirrors the AMYboard assets —
returned, for `amyboard-firmware-AMYBOARD.bin`:

- `updated_at`: `2026-07-08T16:12:06Z` → date code `20260708`
- `digest`: `sha256:e4ccc65c30ae7a422a9e7b8ec80e6f3ede9baa48d80a47196640b2fd5c407873`
- `browser_download_url`:
  `https://github.com/shorepine/tulipcc/releases/download/tulip/amyboard-firmware-AMYBOARD.bin`
- release `body`: `"Rolling Tulip release (TULIP4_R11), built from main @ 0804b4d…"`

---

## Sources

- [tulipcc — firmware upgrade docs](https://github.com/shorepine/tulipcc/blob/main/docs/amyboard/firmware.md)
- [tulipcc — AMYboard SysEx control API (`V` frame / `report_version()`)](https://github.com/shorepine/tulipcc/blob/main/docs/amyboard/control_api.md)
- [`tulip/shared/py/tulip.py` — `get_latest_release()`, `upgrade()`, `version()`](https://github.com/shorepine/tulipcc/blob/main/tulip/shared/py/tulip.py)
- [`.github/workflows/amyboard-release.yml` — rolling release + endpoints](https://github.com/shorepine/tulipcc/blob/main/.github/workflows/amyboard-release.yml)
- [GitHub Releases API — AMYboard release JSON](https://api.github.com/repos/shorepine/tulipcc/releases/tags/amyboard)
