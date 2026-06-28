# CLAUDE.md — working brief for AMYplug

This file is the entry point for Claude Code (and humans). Read it fully before writing code.

Repo: https://github.com/Mando-369/AMYplug · License: AGPL-3.0 (links JUCE) · Lang: C++20 + AMY's C.

---

## 1. What we are building

**AMYplug** is an AU + VST3 instrument plugin that hosts the **AMY** synthesizer (https://github.com/shorepine/amy) inside a DAW. It is the first AMY plugin to exist. Two switchable backends:

| Mode | What renders audio | Use |
| ---- | ------------------ | --- |
| **Software** (MVP) | AMY's C engine compiled into the plugin, rendered in `processBlock` | Default. Full automation + recall, no hanging notes. |
| **Hardware** | A physical **AMYboard** over MIDI/SysEx; DAW receives its audio via an interface | Drive real hardware; plugin acts as editor/librarian. |

The user's goals, in priority order:

1. **No hanging notes** — own the note lifecycle deterministically (this is the #1 reason the plugin exists).
2. **Everything recallable** — patch + parameter state saves/loads with the DAW project, bit-for-bit.
3. **Host automation** of AMY parameters.
4. **Create / edit / save patches** across all AMY engines.

## 2. Golden rules

- **Realtime-safety in `processBlock`:** no allocation, no locks, no file/MIDI I/O on the audio thread. Pre-allocate. Use lock-free FIFOs to pass events between threads.
- **AMY runs in "library" mode** inside the plugin: `audio=AMY_AUDIO_IS_NONE`, `midi=AMY_MIDI_IS_NONE`, `platform.multicore=0`, `platform.multithread=0`. *We* drive rendering; AMY must never open its own audio/MIDI.
- **One source of truth for state:** the canonical `PatchModel` (plain serializable data). The AMY engine and the AMYboard are both *projections* of that model. On `setStateInformation`, rebuild AMY from the model (reset + replay wire messages) — never rely on AMY's internal state surviving.
- **Determinism over cleverness** for v1. Correct recall and clean note-offs beat fancy DSP features.
- **Keep AMY a pristine submodule.** Never edit files under `third_party/amy/`. Wrap it.
- **Every parameter that affects sound must be reachable from the saved state**, or recall will silently break. If you add a control, add it to `PatchModel` + serialization + the rebuild path in the same change.

## 3. AMY in 5 minutes (what you must know to wrap it)

AMY is a small C library, no external deps — you just compile `third_party/amy/src/*.c`. Key facts distilled from AMY's docs (full cheat sheet in `docs/AMY_WIRE_PROTOCOL.md`):

- **Control surface = "wire messages":** compact ASCII strings, one field per letter, e.g. `v0w0f440l1Z` = "osc 0, sine, 440 Hz, note-on velocity 1". You rarely hand-build these — use `amy_event` (C struct) or our `AmyWire` helper.
- **Hierarchy:** `osc` (one oscillator) → `voice` (a group of oscs = one note of polyphony) → `synth` (a pool of voices with a `patch`, handles voice-stealing). MIDI channels 1–16 map to synths 1–16.
- **Patches:** `0–127` Juno-6, `128–255` DX7, `256` piano, PCM presets `0–66`. User patches live at `1024–1055`. A patch is just a stored list of wire commands.
- **Engines** selected by `wave`: `SINE/PULSE/SAW_*/TRIANGLE/NOISE/KS/PCM/ALGO(FM)/PARTIAL/BYO_PARTIALS/INTERP_PARTIALS/AUDIO_IN*/WAVETABLE/...`.
- **Modulation:** "ControlCoefficients" — `amp`, `freq`, `filter_freq`, `duty`, `pan` are each a vector of weights over sources `[const, note, vel, eg0, eg1, mod, bend, ext0, ext1]`. Two envelope generators (`bp0`/`bp1`) and any osc can modulate any other (`mod_source`).
- **Effects** are per-bus: reverb, chorus, echo, 3-band EQ, plus global `volume`.
- **Sequencer/clock:** AMY's internal clock is driven by samples rendered. We will mostly bypass it and use the DAW transport, but AMY can sync to MIDI clock if needed.

### Core C API we call (see `docs/api.md` mirror in the AMY repo)

```c
amy_config_t amy_default_config(void);
void         amy_start(amy_config_t c);     // c.audio=NONE, c.midi=NONE for us
void         amy_stop(void);
amy_event    amy_default_event(void);
void         amy_add_event(amy_event *e);   // schedule/play a structured event
void         amy_add_message(char *msg);    // schedule/play a wire string
output_sample_type* amy_simple_fill_buffer(void); // render ONE block of int16 interleaved
uint32_t     amy_sysclock(void);            // AMY time = total samples / SR
```

Two ways to get audio out:
- **Pull:** call `amy_simple_fill_buffer()` repeatedly to get blocks of `AMY_BLOCK_SIZE` frames (int16, stereo interleaved), convert to float, copy into the JUCE buffer. Simple; start here.
- **Push:** set `amy_config.write_samples_fn` to a callback AMY calls per block. Use later if it fits better.

> **Unknowns to confirm in code, not from memory:** `AMY_BLOCK_SIZE`, `AMY_SAMPLE_RATE`, channel count, and `output_sample_type`. Grep the AMY headers (`third_party/amy/src/*.h`) after `bootstrap.sh`. See `docs/ENGINE_NOTES.md` for the sample-rate-mismatch plan (AMY's SR is a compile-time constant; the host SR may differ → resample or rebuild AMY at host SR).

### Hardware mode transport

AMY accepts wire messages **over MIDI SysEx**: prefix the ASCII wire string with manufacturer id `0x00 0x03 0x45`, wrap in `0xF0 … 0xF7`. Example: `v0f440l1` → `F0 00 03 45 76 30 66 34 34 30 6C 31 F7`. Plain MIDI note-on/off/CC/pitchbend also work directly. So HardwareBackend = pick a MIDI out device (the AMYboard's USB-MIDI port) and emit notes + SysEx. `zI` (ping) → board replies `F0 00 03 45 'O' 'K' F7`; use it to detect a live board. **Verify the AMYboard's USB-MIDI enumeration on real hardware** — see `docs/HARDWARE_MODE.md`.

## 4. Architecture map

```
DAW ──MIDI/automation──▶ AmyPlugProcessor (processBlock, RT thread)
                              │  translate MIDI+params → events
                              ▼
                          NoteRouter  ── owns note on/off, panic, transport-stop
                              │
                              ▼   (lock-free)
                        IAmyBackend  ◀── PatchModel (canonical, serializable)
                          ╱        ╲
             SoftwareBackend     HardwareBackend
        (libamy, renders audio)   (MIDI SysEx → AMYboard)
```

- `AmyPlugProcessor` / `AmyPlugEditor` — JUCE `AudioProcessor` + GUI.
- `IAmyBackend` — interface both backends implement: `prepare`, `processBlock`, `sendEvent`, `loadPatch`, `allNotesOff`, `rebuildFrom(PatchModel)`.
- `SoftwareBackend` — wraps libamy; fills audio.
- `HardwareBackend` — serializes events to MIDI/SysEx; no audio.
- `AmyWire` — builds wire strings / `amy_event`s from typed params; the only place that knows AMY's letter codes.
- `NoteRouter` — the anti-hanging-note brain (see rule #1). Tracks active notes per channel; guarantees a note-off for every note-on; flushes on `prepareToPlay`, transport stop, mode switch, and a "Panic" button.
- `PatchModel` + `Parameters` — state. APVTS for automatable params; `PatchModel` for structural patch data.

See `docs/ARCHITECTURE.md` for detail and `docs/ROADMAP.md` for the milestone order.

## 5. Build / test / run

```bash
./scripts/bootstrap.sh                 # init submodules (JUCE, AMY)
cmake --preset mac-release             # or mac-debug
cmake --build --preset mac-release
ctest --preset mac-release             # unit tests
```

- JUCE plugin defined via the JUCE CMake API (`juce_add_plugin`). Formats: `AU VST3 Standalone`.
- libamy built by `cmake/amy.cmake` as a static lib from the submodule `src/`.
- Validate the built plugin with `pluginval` (wired into CI) and AU validation (`auval -v aumu Amyp Mand`). CI is enabled (`.github/workflows/build.yml`) and free on this public repo.
- Tests use Catch2 (fetched by CMake). Start with `tests/WireMessageTests.cpp`.

## 6. Conventions

- C++20. JUCE coding style-ish; 4-space indent. Classes `PascalCase`, members `camelCase`, no Hungarian.
- Keep AMY's C behind `extern "C"` includes in one translation unit boundary (`SoftwareBackend`).
- Plugin codes: manufacturer `Mand`, plugin `Amyp` (AU type `aumu`). Bundle id `com.mando369.amyplug`.
- Conventional commits (`feat:`, `fix:`, `docs:`…). Small PRs aligned to roadmap milestones.
- Don't break recall: any sound-affecting change updates `PatchModel` + serialize + rebuild together.

## 7. First tasks (M0 → M1, see ROADMAP)

1. `./scripts/bootstrap.sh`, then grep AMY headers to fill the constants in `engine/AmyConfig.h` (block size, sample rate, channels, `output_sample_type`).
2. Get an empty JUCE plugin building for AU+VST3 (`AmyPlugProcessor` passes audio through silence).
3. `SoftwareBackend`: `amy_start` in library mode, render one synth, hear `note 60` from a MIDI keyboard. Handle host-SR ≠ AMY-SR.
4. `NoteRouter`: prove no hanging notes (hold notes, stop transport mid-note → silence; bypass → silence).
5. Minimal `PatchModel` + `get/setStateInformation` round-trip: load Juno patch 0, save/reload project, identical sound.

Each milestone has a "definition of done" in `docs/ROADMAP.md`. Update tasks as you go.
