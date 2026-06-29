# Roadmap

Milestones are ordered; each has a **Definition of Done (DoD)**. Software mode is
the priority; Hardware mode comes after the engine is solid.

## M0 — Scaffold ✅ (this repo)
Project skeleton, build system, docs, interfaces, stubs.
**DoD:** repo present; `CLAUDE.md` + `docs/` describe the plan; CMake references
resolve once submodules are present.

## M1 — It builds and makes sound ✅ (2026-06-28)
Done: AMY submodule fetched (JUCE reused from a local checkout via
`AMYPLUG_JUCE_DIR`); `AmyConfig.h` confirmed vs `amy.h` (256/44100/2/int16);
`AmyConstants.h` `Wave`/`Reset` corrected; `SoftwareBackend` drives AMY in library
mode (SPSC wire FIFO + alloc-free note path + Lagrange resampler); `amy_platform_stubs.c`
satisfies device hooks. **DoD met:** Release build of AU/VST3/Standalone succeeds;
`auval -v aumu Amyp Mand` PASS (render at 11025–192000 Hz + mono + MIDI); pluginval
strictness 7 PASS on AU + VST3; 5/5 ctest pass (incl. an engine render test proving
note 60 / Juno-0 is audible and silences on note-off). Single-instance limitation
confirmed + guarded (see ENGINE_NOTES §4). Remaining manual check: play in Logic/Live.

1. `scripts/bootstrap.sh`; grep AMY headers → fill real values in `AmyConfig.h`
   (`AMY_BLOCK_SIZE`, `AMY_SAMPLE_RATE`, channels, `output_sample_type`) and
   confirm AMY's source-file list in `cmake/amy.cmake`.
2. Empty JUCE plugin builds AU + VST3 + Standalone; loads in a host.
3. `SoftwareBackend`: `amy_start` in library mode; render Juno patch 0; a MIDI
   note from a keyboard is audible. Handle host-SR ≠ AMY-SR (resampler).
4. Resolve the **single-AMY-instance** question (see ENGINE_NOTES §4): confirm
   multi-instance behavior or document/guard it.
**DoD:** `cmake --build --preset mac-release` succeeds; `pluginval` strictness 7
passes; `auval -v aumu Amyp Mand` passes; you can play notes in Logic/Live.

## M2 — No hanging notes + recall ✅ (2026-06-28)
Done: 7 NoteRouter lifecycle tests prove every note-on is balanced (normal/
transport-stop/sustain/panic/multi-channel); PatchModel macro fields + lossless
ValueTree round-trip + ordered `toWireMessages` (RESET_SYNTHS → patch → i<ch>F/R/A
macros → global V/h/k/M); APVTS↔model sync + RT-safe macro streaming
(`streamWire` audio-thread path, AsyncUpdater for structural patch/voices); pitch-
bend range → octaves. Hardening from fuzzing: clamp patch to loadable ranges
(0..256 / 1024..1055), and use RESET_SYNTHS not RESET_AMY (the latter did
amy_stop()+amy_start() on the audio thread → crash). **DoD met:** 16/16 ctest incl.
a cutoff-sweep timbre test; `auval` + pluginval strictness 7 PASS on AU + VST3
(incl. parameter thread-safety + state-restoration fuzz). Remaining manual checks:
hold chord → stop → silence; save/reload → identical; automate cutoff in Logic.

Original tasks:
1. Finish `NoteRouter` (transport flush, sustain deferral, panic, defensive offs).
2. Finish `PatchModel` + `get/setStateInformation`; APVTS ↔ model sync.
3. Map core macros (cutoff, resonance, amp ADSR, reverb/chorus/echo, volume) to
   AMY and to automation.
**DoD:** automated tests for note lifecycle + state round-trip pass; by hand: hold
chord → stop transport → instant silence; save/reload project → identical sound;
automate cutoff → smooth, recorded, recalled.

## M3 — Patch system & editor v1
**M3b done ✅ (2026-06-29):** tabbed editor (Juno · DX7 · AMYboard) matching the
AMYboard online controller. The **Juno tab** is a real editable analog engine — a
4-oscillator subtractive voice (osc0 VCF/VCA, osc1 LFO, osc2/3 OSC A/B) built from
AMY's "amyboard default" template, with OSC A/B · LFO+depths · VCF · VCF-ENV · AMP-ENV
panels and a global EQ/Chorus/Reverb/Echo rack — all automatable + recalled. An
"Analog" engine toggle switches synth 1 between Factory presets and the analog voice.
PatchModel gained Engine + AnalogParams + 3-band EQ; osc-level streaming is RT-safe.
DX7 & AMYboard tabs are placeholders. 22/22 ctest, auval + pluginval strictness 7
(AU+VST3, incl. engine-switch + analog-param fuzzing) pass. **Deferred:** DX7 FM
operator editor (M3c); per-osc detune/octave; hardware control (M4).

**M3a done ✅ (2026-06-28):** built-in patch browser with real names (parsed from
AMY's `patches.h` comments at build time via `scripts/gen_patch_names.py` → 258
named patches, grouped Juno/DX7/Piano/AMYboard) + prev/next; user-patch save/load/
delete to an on-disk `PatchLibrary` (`~/…/AMYplug/Patches/*.amypatch`); a real
editor layout with labelled macro knobs (cutoff/reso/ADSR/FX/volume/voices/bend).
`patchA` extended to 0..257. 19/19 ctest, auval + pluginval strictness 7 (AU+VST3)
pass. **Deferred (M3b+):** deep per-engine editors (FM operator graph, PCM/drums,
modular osc graph) and DX7/Juno SysEx import (converters not in this AMY build).

Original tasks:
1. Patch browser for built-in banks (Juno 0–127, DX7 128–255, piano, PCM).
2. Load/save **user patches** (1024–1055); import DX7 + Juno-6 SysEx (AMY ships
   `fm.py`/`juno.py` references — port the mapping or call AMY's converters).
3. Editor panels per engine (start: Juno analog; then FM operators; then PCM/drums;
   then modular osc graph).
**DoD:** can pick a preset, tweak, save as user patch, reload with the project.

## M4 — Hardware mode
1. `HardwareBackend` device selection UI; confirm AMYboard USB enumeration
   (MIDI vs serial — see HARDWARE_MODE.md) on real hardware.
2. Notes + SysEx wire messages reach the board; `zI` ping detects it.
3. `rebuildFrom()` re-sends full state on load/mode-switch; All-Notes-Off safety.
**DoD:** flip to Hardware mode, play the physical AMYboard from the DAW, recall a
session onto the board.

## M5 — Depth & polish
- Full modulation editor (CtrlCoefs, dual EGs, mod routings, LFOs).
- **LFO modes** (M3b ships per-voice/free only; add a mode selector):
  - **Free** — global, free-running (one shared LFO, not retriggered).
  - **Key** — retrigger phase on each note-on.
  - **Poly** — per-voice independent LFO (current M3b behaviour).
  - **Sync** — lock LFO rate to host tempo (uses the DAW transport BPM).
- MPE; per-note expression; sequencer/clock sync option.
- Wavetable loading (waveeditonline), sampler (`load_sample`/`disk_sample`).
- Preset pack; resizable/scalable UI; metering.

## M6 — Release
- Windows VST3 (add CI matrix); codesign/notarize macOS; installer.
- Versioned releases; user docs; demo video.
**DoD:** tagged release with notarized AU/VST3 and a clean first-run experience.

---

### Known risks / open questions
- **AMY global state** vs multiple plugin instances (M1).
- **Sample-rate** reconciliation quality (M1) — Lagrange may need upgrading.
- **AMYboard transport** MIDI vs serial (M4).
- **Realtime allocation** inside AMY for user patches (`malloc`) — keep off the
  audio thread (M2/M3).
