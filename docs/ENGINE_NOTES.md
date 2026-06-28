# Engine notes (Software mode)

Practical issues when embedding libamy in a realtime plugin. Read before
implementing `SoftwareBackend`.

## 1. Sample rate mismatch (the big one)

AMY's sample rate is a **compile-time constant** (`AMY_SAMPLE_RATE`, typically
44100). The DAW may run at 48000/88200/96000. Options, in order of preference:

1. **Resample AMY's output** to the host rate inside `SoftwareBackend`
   (start with `juce::LagrangeInterpolator`/`juce::Interpolators`, one per channel).
   Simple, robust, slight CPU cost. **MVP choice.**
2. **Rebuild AMY at the host rate** — only if AMY exposes the rate as a `-D` macro
   you can set per-configuration. AMY is one fixed rate per binary, so this would
   mean shipping multiple builds or recompiling — not realistic at runtime.

Pick (1). Confirm the constant name by grepping the AMY headers.

## 2. Block-size adaptation

AMY renders fixed blocks of `AMY_BLOCK_SIZE` frames; the host asks for arbitrary
`numSamples`. Keep a small ring/scratch buffer: pull AMY blocks as needed to fill
the host buffer, retaining the remainder for next time. Don't assume host block ==
AMY block.

## 3. Sample format & channels

`amy_simple_fill_buffer()` returns `output_sample_type*` (confirm: usually
`int16_t`), interleaved stereo. Convert to float (`/ 32768.0f`) and deinterleave
into the JUCE buffer. Confirm channel count (`AMY_NCHANS`).

## 4. Library mode config (no device I/O)

```c
amy_config_t cfg = amy_default_config();
cfg.audio = AMY_AUDIO_IS_NONE;     // we own audio
cfg.midi  = AMY_MIDI_IS_NONE;      // we own MIDI
cfg.platform.multicore   = 0;      // never spawn threads inside a plugin
cfg.platform.multithread = 0;
// features.default_synths = 0;  // we set up synths ourselves from PatchModel
amy_start(cfg);
```

> AMY has a global/static state model (it's a microcontroller library). Assume
> **one AMY instance per process**. If two plugin instances load, they may share
> AMY's globals. Investigate early: either (a) confirm AMY supports multiple
> contexts, or (b) guard a single shared engine and route per-instance via
> distinct `synth` numbers, or (c) accept single-instance for v1 and document it.
> This is a known risk — see ROADMAP M1.

## 5. Realtime safety

- No `malloc`/`free` on the audio thread. AMY's `amy_add_message` parses strings;
  prefer pushing pre-built wire bytes through a lock-free FIFO and draining them at
  the top of `processBlock`. Verify AMY's add-event path doesn't allocate; if it
  does (user patches use `malloc`), do those operations off-thread in `rebuildFrom`.
- Do all patch building / sample loading on the message thread.

## 6. Hanging notes — the design that prevents them

This is the project's reason for existing. Guarantees enforced by `NoteRouter`:

- Every tracked note-on has a matching note-off path.
- On **transport stop** (falling edge of `isPlaying`) → `allNotesOff`.
- On **bypass / reset / prepareToPlay / mode switch** → `allNotesOff`.
- **Panic** button → `allNotesOff` (also send AMY `RESET_ALL_OSCS` as a hammer).
- **Sustain pedal** defers note-offs and flushes them on release — pedal-down at
  transport stop still flushes.
- Defensive: after a global all-notes-off, also emit explicit per-note offs for
  everything we believe is sounding.

Prove it with an automated test and by ear: hold a chord, stop the transport mid-
note → instant silence; toggle bypass mid-note → silence.
