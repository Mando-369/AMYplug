# Hardware mode (driving a physical AMYboard)

In Hardware mode the plugin produces **no audio**. It acts as an editor/librarian
and live controller, sending MIDI + AMY wire-messages (over SysEx) to a real
[AMYboard](https://amyboard.com). The board makes the sound; you route its audio
into your interface like any external instrument.

## Transport

- **Notes / pitch bend / sustain (CC64) / all-notes-off** → standard MIDI on
  channels 1–16 (= AMY synths 1–16).
- **Patch edits / structural changes** → AMY wire messages wrapped in SysEx:
  `F0 00 03 45 <ascii wire> F7`. AMY needs no encoding (wire is lower-ASCII).
- **Liveness check** → send `zI` (ping); a live board replies
  `F0 00 03 45 'O' 'K' F7`. Use a `juce::MidiInput` callback to observe it.

## Connecting

The AMYboard is an ESP32-S3 with MIDI in/out (3.5 mm TRS) and native USB. **Verify
on real hardware** how it enumerates to macOS:

1. Plug in over USB. Check Audio MIDI Setup → MIDI Studio for a MIDI device, and
   `ls /dev/tty.usb*` / `ioreg` for a USB-serial device.
2. If it presents as **USB-MIDI**, `HardwareBackend::availableOutputs()` will list
   it — pick it in the UI and we send directly.
3. If it presents only as **USB-serial** (CDC) running MicroPython/`amy` REPL, we
   may need a serial transport instead of MIDI (AMY wire messages over the serial
   link, the same way the Python `amy` lib talks to it). Add a `SerialTransport`
   alongside the MIDI path if so.
4. DIN/TRS MIDI via a normal USB-MIDI interface also works for the MIDI path.

> Action item (M4): confirm (2) vs (3) on Tom's board and document the exact port
> name + baud. Until then, the MIDI path is the assumed default.

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
