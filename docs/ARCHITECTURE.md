# Architecture

AMYplug is a JUCE plugin with a swappable AMY "backend". The plugin never talks to
AMY directly; it talks to `IAmyBackend`, and the mode switch chooses which concrete
backend is live.

```
                 ┌──────────────────────────────────────────────┐
   DAW  ───────▶ │ AmyPlugProcessor (juce::AudioProcessor)       │
 MIDI +          │  • APVTS (automatable params)                 │
 automation      │  • PatchModel (canonical, serializable state) │
                 │  • NoteRouter (note lifecycle / anti-hang)    │
                 └───────────────┬──────────────────────────────┘
                                 │  IAmyBackend (the seam)
                  ┌──────────────┴───────────────┐
                  ▼                              ▼
        ┌───────────────────┐        ┌──────────────────────────┐
        │ SoftwareBackend   │        │ HardwareBackend          │
        │  embeds libamy    │        │  MIDI + SysEx to AMYboard│
        │  renders audio    │        │  produces NO audio       │
        └───────────────────┘        └──────────────────────────┘
```

## Components

- **AmyPlugProcessor / AmyPlugEditor** — the JUCE plugin and its GUI.
- **IAmyBackend** — interface: `prepare`, `processBlock`, `sendWire`, note/CC helpers,
  `allNotesOff`, `rebuildFrom(PatchModel)`. Two implementations.
- **SoftwareBackend** — the only TU that includes AMY's C headers. Calls
  `amy_start` in library mode (`audio=NONE`, `midi=NONE`, single-threaded) and
  `amy_simple_fill_buffer()` to render. Converts int16→float and resamples if the
  host rate ≠ AMY's native rate.
- **HardwareBackend** — serializes notes to MIDI and wire messages to SysEx
  (`F0 00 03 45 … F7`) and sends them to the AMYboard's MIDI port (or the host
  MIDI-out). Clears the audio buffer.
- **NoteRouter** — owns "what is sounding". Guarantees note-offs; flushes on
  transport stop, bypass, reset, mode switch, and Panic. This is the core fix for
  the hanging-note problem.
- **PatchModel** — canonical serializable state and the source of truth. Both
  backends are rebuilt *from* it. `toWireMessages()` recreates the exact AMY state.
- **AmyWire** — the only place that knows AMY's letter codes; builds wire strings
  and SysEx frames.

## Threading model

- **Audio thread** (`processBlock`): Panic check → transport flush → `NoteRouter`
  translates MIDI → backend renders/forwards. No allocation, no locks, no I/O.
- **Message/UI thread**: parameter edits, patch loads, device selection, project
  load. These mutate `PatchModel` and call `rebuildFrom()` (not RT-safe) or hand
  small wire messages to the audio thread via a lock-free FIFO.
- **State save/load**: `get/setStateInformation` (de)serialize APVTS + PatchModel,
  then `rebuildEngineFromModel()` recreates the engine. Recall never depends on
  AMY's live internal memory surviving.

## State & recall invariant

> Every sound-affecting setting must live in `PatchModel` (and/or APVTS) and be
> reproduced by `toWireMessages()`. If you add a control, wire all three —
> parameter, model field, rebuild — in the same change, or recall silently breaks.

There is a unit test that loads a patch, serializes, deserializes, and asserts the
regenerated wire-message list is identical. Keep it green.

## Why a backend seam (not "just embed AMY")?

Because the user owns an AMYboard and may want the plugin to *drive the hardware*.
The seam lets Software mode (in-DAW audio, best recall/automation, the MVP) and
Hardware mode (editor/librarian for the physical board) share all the note-routing,
state, and UI code. Software mode is the default and the priority.
