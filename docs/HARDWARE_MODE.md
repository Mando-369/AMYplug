# Hardware mode (driving a physical AMYboard)

> # ⚠️ CHECK YOUR SAMPLE MASTER CLOCK FIRST ⚠️
>
> **Before you debug ANY "random" glitchy audio, drifting pitch / "shifting sample
> rate", crunch/bitcrush, or fluctuating latency in Hardware mode — check your macOS
> audio clock / Aggregate Device setup in _Audio MIDI Setup_.**
>
> A **macOS Aggregate Device** combining two interfaces (e.g. a PreSonus 1824c + a
> FiiO K11) with **drift correction misconfigured** (correcting a device's clock
> against itself, or two free-running clocks) produces *exactly* these symptoms — and
> it does so **intermittently**, because clock drift is nondeterministic ("works once,
> broken the next time"). The board audio and the web editor share this same final
> audio path, so a bad clock there **mimics a board-firmware or plugin bug** and will
> send you down a rabbit hole. It cost us a full session (2026-07-07).
>
> **The fix is usually one checkbox:** use the interface **directly** on its own
> internal clock, or set exactly **one** clock master + drift correction on the
> *other* device only. AMY's `latency_ms` defaults to 0 and the board plays instantly;
> the plugin does **not** touch it. See [HARDWARE_LATENCY.md](HARDWARE_LATENCY.md).

In Hardware mode the plugin produces **no audio**. It acts as an editor/librarian
and live controller, sending MIDI + AMY wire-messages (over SysEx) to a real
[AMYboard](https://amyboard.com). The board makes the sound; you route its audio
into your interface like any external instrument.

## Transport

- **Notes / pitch bend / sustain (CC64) / all-notes-off** → standard MIDI on
  channels 1–16 (= AMY synths 1–16). **Verified working on real hardware.**
- **Patch edits / structural changes** → AMY wire strings sent over the **serial
  REPL** as `amy.send_raw("<wire>")` (see Connecting below). NOT via MIDI-SysEx — the
  `F0 00 03 45 <wire> F7` path is deferred to the board's MicroPython handler and is
  **inert for per-synth control** on real hardware.
- **Liveness check** → the C SysEx handler DOES answer `zI` (ping) with
  `F0 00 03 45 'O' 'K' F7` (`amy_midi.c:249-252`) — a `juce::MidiInput` callback can
  observe it. (Or just parse the serial REPL banner, which also proves liveness.)

## Connecting — VERIFIED ON REAL HARDWARE (2026-07-06, Tom's board)

The AMYboard (VID `0xcaf0` / PID `0x4009`, mfr "SPSS") is an ESP32-S3 that enumerates
as a **composite USB device exposing BOTH**:

- **USB-MIDI** (interface class 1 / subclass 3, MIDIStreaming). CoreMIDI claims it and
  it shows up as a MIDI port literally named **`AMYboard`** →
  `HardwareBackend::availableOutputs()` lists it. `juce::MidiOutput` opens it directly.
- **USB-serial / CDC-ACM** MicroPython `amy` REPL. macOS binds `AppleUSBACMData`; the
  callout device is **`/dev/cu.usbmodem11201`** (the name's `11201` derives from the
  board's USB location id `0x0112_0000` — not stable across ports/hubs, so resolve it
  at runtime by matching the "AMYboard" USB product node, don't hard-code it). It opens
  as a raw MicroPython prompt: `MicroPython … on … AMYboard with ESP32S3 / >>>`.
  Native-USB CDC has no auto-reset circuit, so opening the port does NOT reboot the board.

### Which transport carries what — THE key finding

The two paths are handled by **completely different code on the board**, and only MIDI
is honored in C:

- **MIDI (notes, Program Change, Bank-Select CC0, pitch-bend, sustain CC64, all-notes-off)**
  → handled directly in C (`third_party/amy/src/amy_midi.c:150-155`). **Works.** MIDI
  channel N → AMY synth N (ch1 → synth 1). PC on ch N loads a patch onto synth N.
- **AMY wire-over-SysEx** (`F0 00 03 45 <wire> F7`, e.g. `i1K…`, `i1F…`) → on
  `TULIP`/`AMYBOARD` builds the payload is deferred to a MicroPython callback
  (`amy_midi.c:281`, `tulip_amy_send_sysex`, board firmware not in this repo). **Empirically
  INERT for per-synth control** — patch-loads and filter changes targeting the synth that
  is voicing MIDI notes had NO audible effect. **Do not rely on the SysEx wire path.**

**Full patch/param/osc editing MUST go over the serial REPL.** The on-board `amy`
MicroPython module drives the *same* engine the MIDI notes play, and accepts our exact
wire strings verbatim:

```python
amy.send_raw("i1iv4K20Z")   # load patch 20 on synth 1  -> WORKS (changed the sound)
amy.send_raw("i1F40Z")      # slam synth-1 filter        -> WORKS (note went dark)
amy.send(synth=1, patch=20, num_voices=4)   # structured form also works
```

So `PatchModel::toWireMessages()` output needs **no change** — the plugin just needs a
**serial transport** that writes `amy.send_raw("<wire>")\r\n` to the CDC port. The `amy`
module also exposes `get_synth_commands(synth)` / `retrieve_patch` (state read-back the C
API lacked) and `inject_midi_bytes`.

### Target Hardware-mode architecture

- **Note events** (on/off, vel, pitch-bend, sustain, panic) → USB-MIDI to the `AMYboard`
  port (current `HardwareBackend` MIDI path — already correct).
- **Patch load + all param/osc edits + recall (`rebuildFrom`)** → serial REPL,
  `amy.send_raw("<wire>")` per message. New `SerialTransport` alongside the MIDI path.
- DIN/TRS MIDI via a normal USB-MIDI interface also drives notes if not using native USB.

> ⚠️ **Throttle patch/structural bursts hard.** A back-to-back burst of ~16 patch-loads
> (no pacing) knocked the board fully off the USB bus and required a power-cycle. Heavy
> ops (patch loads / `rebuildFrom`) are scheduled on the board's MicroPython loop and must
> be spaced (the REPL's per-line ACK gives natural flow-control — wait for `>>>` between
> heavy sends rather than firing blindly).

## Recall in Hardware mode

The board has no project memory. On project load (and mode switch), the plugin
re-sends the entire `PatchModel` via `rebuildFrom()` so the board matches the
session. Treat the board as a stateless renderer of the canonical model.

## Latency & sync

- There's round-trip latency over USB/MIDI; the plugin can't sample-align hardware
  audio. For tight timing, prefer Software mode; use Hardware mode for sound design
  on the real board and live play.
- Optionally forward the DAW transport as MIDI clock (`F8`/`FA`/`FC`) to drive
  AMY's internal sequencer.

## Safety

- On any stop/bypass/disconnect, send All-Notes-Off on all 16 channels so the board
  doesn't drone.
- Throttle SysEx bursts (e.g. when sweeping a knob) so we don't flood the board's
  MIDI buffer; coalesce rapid parameter changes.
