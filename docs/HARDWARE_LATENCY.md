# AMYboard MIDI→audio latency (~700 ms) — research & fixes

_Question: is the ~700 ms delay between MIDI-in and audio a known AMYboard problem, and can it be fixed? Is USB-C the issue?_

## ✅ RESOLVED (2026-07-07) — it was a host audio-clock loop, NOT the board or `latency_ms`

**Actual root cause: a macOS *Aggregate Device* (PreSonus Studio 1824c + FiiO K11 R2R) with
drift correction misconfigured so the OS was correcting the 1824c's clock against itself** — a
clock loop. That single misconfiguration produced BOTH observed symptoms:
- the "shifting sample rate / bitcrush" warble (the aggregate's two clocks drifting), and
- the large, variable monitoring latency (buffer bloat from the clock fight).

It appeared "lately" because a Universal Control update (now Fender-owned) deleted the user's
aggregates on re-authorization; rebuilding them re-enabled the bad drift-correction. Hooking the
1824c up directly (its own internal clock, no self-referential drift correction) fixed delay AND
bitcrush at once. Verified by ear from BOTH the plugin and shorepine's own web editor.

**What this means for the plugin — do NOT chase `latency_ms` again:**
- AMY's `latency_ms` **defaults to 0** and a directly-connected board plays instantly at 0 (the
  web editor confirms). It's a jitter cushion for *sequenced/networked* play, not something a live
  editor should raise. My earlier "the firmware forces ~700 ms" conclusion was **WRONG**.
- Our notes already take the fast path (native USB-MIDI note-on/off). Nothing to fix there.
- The plugin therefore **no longer touches `latency_ms`**: `rebuildFrom` does not call it, there is
  no transport/periodic re-assert, and `HardwareBackend::latencyMs` defaults to 0.
  `assertLatency()`/`setLatencyMs()` are retained only for a possible future *sequenced-hardware*
  feature and are never auto-called.
- If a DAW still reports plugin latency in Hardware mode, that's host plugin-delay-compensation for
  our silent-output instrument — a separate reporting concern, unrelated to the board.

The research notes below are kept for the transport/path detail (which is accurate and useful).

---

## Short answer

It is **not** inherent AMY/AMYboard audio latency, and it's almost certainly fixable.
AMY's own signal path is tiny: the audio block size is **128–256 samples** (≈3–6 ms at
44.1 kHz) and AMY's scheduling offset `latency_ms` **defaults to 0** (`third_party/amy/src/amy.c:438`).
There is no documented "700 ms" bug, but the *mechanism* for a large, consistent delay is
well understood: **it comes from which path the note takes to the board's synth engine**, not
from AMY rendering. A fixed ~700 ms (constant, not jittery) points to a buffered/deferred
software path or an out-of-date firmware/sketch — not the USB-C wire itself.

## How the AMYboard latency budget actually works

Over USB-C the board enumerates as **two** devices at once: a native **USB-MIDI** endpoint
*and* a **USB-serial (CDC) REPL** (VID/PID `caf0:4009`). There are three ways a command can
reach the synth, fastest to slowest:

1. **Native MIDI note-on/off/CC** → handled directly in C by AMY's MIDI layer. Fast (low ms).
2. **AMY wire message over MIDI-SysEx** (`F0 00 03 45 … F7`) → on AMYboard/Tulip this is **not**
   processed synchronously; it's copied into ring slots and run later on a **deferred
   MicroPython `mp_sched` callback** (`third_party/amy/src/amy_midi.h`). Slower, and can bunch up.
3. **AMY wire message over the serial REPL** → MicroPython receives the line over USB-CDC and
   `exec`/parses it in Python. Slowest, and subject to REPL buffering + GC pauses.

So: **notes via path 1 are fast; anything via path 2 or 3 can add tens–hundreds of ms**,
especially in bursts (a patch rebuild is dozens of messages).

## What your plugin actually does (grounded in `src/engine/HardwareBackend.cpp`)

- **Notes** are sent as **native USB-MIDI** note-on/off (`noteOn` → `MidiMessage::noteOn`, path 1). Good — this is the fast path.
- **Patch / param / osc edits** and **legato pitch changes** (`changeNote`) go over the **serial REPL** (path 3) — the code comment says serial is "the ONLY path that reaches the board's sound" on your firmware, with MIDI-SysEx treated as inert.
- The sender thread is **rate-limited**: `kBurstCap = 8` messages per wake, `wait(4 ms)` (1 ms while draining a backlog). A big burst (full patch = reset + osc graph + mix, or `allNotesOff` = 32 CC messages) therefore drains over several wakes.
- You already added **diagnostics**: `run()` logs `NOTEON … enq->wire=<ms> serialQ=<depth>` to `/tmp/amyplug_hw.log`. **Read that file first** — it tells you immediately whether the delay is on our side or the board's (see bisection below).

## Ranked likely causes (most to least likely)

1. **Firmware out of date.** The AMYboard troubleshooting page explicitly says "we've fixed a lot
   of USB issues recently — upgrade first." USB-CDC/MIDI timing fixes land often. **Do this first.**
2. **A custom `sketch.py` on the board intercepting MIDI in Python.** If your sketch reads MIDI
   and calls `amy.send` (instead of letting AMY's C layer handle notes), MicroPython scheduling +
   GC can add a large, consistent delay. Reset to the default sketch (AMYboard Online → **Reset**,
   or delete `sketch.py`) and retest.
3. **Control/notes taking the slow path.** If in your current build any *note* timing depends on
   the **serial REPL** or **SysEx** path (e.g. legato `changeNote`, or a patch resend firing right
   before the note), MicroPython parsing + the `kBurstCap`/`wait` rate-limiter can stack up to
   hundreds of ms. Your `serialQ` diagnostic exists precisely to catch this backlog.
4. **AMY `latency_ms` set non-zero** by a sketch or a stray `N` wire message. It only applies when
   set (`amy.c:1885–1890`), but if something sets it, **every** event is delayed by exactly that
   many ms — a perfect match for a fixed offset. Send `amy.send(latency_ms=0)` / wire `N0` and grep
   your sketch for `latency`.
5. **USB-C cable / driver / wrong endpoint.** A **charge-only** cable, a missing **CH340** serial
   driver, or sending to the serial REPL when you meant the MIDI endpoint all cause trouble. Confirm
   the board shows up as USB-MIDI `caf0:4009` (macOS **Audio MIDI Setup**), not just a JTAG/serial
   debug unit (`303a:…`, which means it hasn't booted its firmware — press **BOOT**).

USB-C itself is **not** the problem — native USB-MIDI is a low-latency transport. What matters is
*which* of the two USB devices (MIDI vs serial REPL) your messages go to.

## Bisect it in 5 minutes

1. **Read `/tmp/amyplug_hw.log`.** If `enq->wire` is large or `serialQ` climbs, the delay is on
   **our** side (rate-limiter / serial backlog). If `enq->wire` is a few ms, the delay is on the
   **board** (firmware/sketch/USB) → go to step 2.
2. **Bypass the plugin entirely.** Play the board from a plain MIDI keyboard, or from **AMYboard
   Online** in a Web-MIDI browser (Chrome/Edge). Still ~700 ms? → it's the **board** (firmware or
   sketch). Instant? → it's the **plugin/DAW**.
3. **Upgrade firmware**, then re-test step 2.
4. **Reset the sketch** to default, re-test.
5. In the DAW, check any **external-instrument / plugin-delay-compensation** setting on the track.

## Concrete fixes

- **Board:** upgrade firmware; reset to the default sketch; ensure notes are handled by AMY's C MIDI
  layer (don't route note-ons through a Python sketch); verify `latency_ms == 0`.
- **Plugin (`HardwareBackend`):** keep all **time-critical events (notes, pitch bend, sustain) on
  native USB-MIDI**, never SysEx/serial. Give notes priority over bulk patch/param traffic — send
  notes immediately and rate-limit only the patch/knob SysEx/serial bursts (or use a separate,
  higher-priority queue for notes). Consider raising `kBurstCap` or dropping the `wait` for the note
  queue. Coalesce knob sweeps so a slider drag doesn't flood the serial REPL.
- **If patch edits over serial are the slow part:** batch them, throttle only during rebuilds, and
  avoid re-sending the whole patch on every small change.

## Get an authoritative answer

Since exact timing is firmware-version-specific, the fastest confirmation is the maintainers:
- **Discord `#amyboard` channel** — https://discord.gg/TzBFkUb8pG (the docs point here for USB/latency issues).
- **GitHub issues** — https://github.com/shorepine/tulipcc/issues (AMYboard firmware) and https://github.com/shorepine/amy/issues (engine). Search "latency"/"delay"; if nothing matches, file one with your firmware version and the `/tmp/amyplug_hw.log` numbers.

## Sources
- [AMYboard Getting Started](https://github.com/shorepine/tulipcc/blob/main/docs/amyboard/README.md) & [Troubleshooting](https://github.com/shorepine/tulipcc/blob/main/docs/amyboard/troubleshooting.md) — USB-MIDI vs serial, firmware, `caf0:4009`, sketch reset.
- [AMY MIDI docs](https://github.com/shorepine/amy/blob/main/docs/midi.md) & [API (`latency_ms`/`N`)](https://github.com/shorepine/amy/blob/main/docs/api.md).
- AMY source (local submodule): `src/amy.c` (`latency_ms` default 0, applied at 1885–1890), `src/amy.h` (`AMY_BLOCK_SIZE` 128/256), `src/amy_midi.h` (SysEx deferred via MicroPython on AMYboard/Tulip).
- Your `src/engine/HardwareBackend.cpp` — note path (USB-MIDI), control path (serial REPL), rate-limiter, and the `/tmp/amyplug_hw.log` diagnostics.
