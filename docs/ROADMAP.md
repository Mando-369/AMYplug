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
**Voicing fixes ✅ (2026-07-01):**
- **Amp-env hang with Unison.** Per-osc analog edits (amp env, level, tune, duty, LFO
  depth) streamed only to oscs 2/3, so with Unison>1 the extra copies kept their
  rebuild-time envelope — a shortened release left them ringing until a rebuild
  (which is why it "reset" on a Unison/Detune change). `streamAnalogParams` now loops
  every unison copy. Confirmed offline (stale copies ring at 0.0255 vs 0.0 fixed).
- **Glide gated to Mono/Legato.** AMY portamento glides a *reused* voice, so in Poly
  (fresh voice per note) it does nothing and could sweep each note in from a stale
  pitch. `toWireMessages` + `streamGlobalFx` now force portamento 0 in Poly.
- Note: AMY doesn't hard-retrigger a reused voice, so Mono and Legato both slur an
  overlapping legato line (Mono still re-attacks across a gap).
- **Detune dropout fixed:** `unisonDetune` is no longer structural — it only re-tunes
  existing oscillators, which streams live (no rebuild → no dropout on a sweep).
  `unisonVoices` (the osc *count*) still rebuilds, so changing the count still blips;
  eliminating that would mean pre-allocating max copies (CPU cost when unused).

**Voicing & performance ✅ (2026-06-30):**
- **OSC A/B Coarse + Fine** (analog): ±24 semitones / ±100 cents per oscillator, folded
  into the osc freq const (`baseHz·2^((coarse+fine/100)/12)`) at emit + stream time.
- **Glide** (all engines): AMY-native portamento broadcast as `i<ch>m<ms>` (the
  synth-layer `m` is `grab_midi_notes`=`i1im`; `i1m500` routes to the main parser's
  portamento). New Glide knob, recalled.
- **Voice Mode: Poly / Mono / Legato** in `NoteRouter` — per-channel held-note stack
  with last-note priority. Mono retriggers (off→on); Legato slurs (AMY does **not**
  retrigger on an overlapping note-on to a reused voice — confirmed offline). Mono/Legato
  rebuild the synth to 1 voice so AMY is truly monophonic. Regression tests in
  `NoteRouterTests` (priority, retrigger vs slur, panic-flush).
- **Unison (analog) ✅ (2026-06-30):** since AMY keys voices by the **rounded** note
  (`patches.c:928`) and ignores `r` voice-targeting when a synth is set (`:957`),
  note-fanning can't do subtle detune. Instead the analog voice stacks **U detuned
  copies of the OSC A+OSC B pair** as extra audio oscs, chained into the VCF
  (osc0←osc2←…←oscLast) — exactly how AMY's factory "unison" patches fatten a voice.
  `Unison` (1–4) + `Detune` (cents) in the VOICE rack section; both structural
  (rebuild). Analog engine only; FM/factory unison would need per-synth stacking.

**Bus-FX smoothing ✅ (2026-06-30):** fast knob moves on Echo Time (re-lengths the
delay line) and the EQ gains (recompute biquad coefs) crackled because the streamed
wire value jumped. `streamGlobalFx` now glides each bus-FX member toward its target
(one-pole, ~25 ms, coef from the host block rate) and keeps re-sending the full
`h/k/M/x` list until it settles, so the change spreads into small per-block steps.

**Bitcrusher ✅ (2026-06-30):** a retro sample-rate + bit-depth reducer on the output
(`src/dsp/BitCrusher.h`, ported from the FAUST_TX81Z effect — Faust `ba.bitcrusher` by
J.O. Smith III + `ba.downSample` by R. Michon, fused into one S&H). **Freq** = the
crushed sample rate in Hz (`bc_freq`, S&H every `int(SR/freq)` samples); **Bit** = the
amplitude resolution (`bc_bits`, 2..16, quantize `round(x·(2^bits−1))/(2^bits−1)`).
Both default to clean (16 bit + full rate = true bypass). Runs first in the host-side
MASTER stage; recalled via state + PatchModel; unit-tested (bypass, level-set,
sample-hold, silence); auval + pluginval strictness 7 pass.

**Output gain split ✅ (2026-06-30):** AMY's volume (`V`) is applied *inside* the engine,
so it can't sit at the end of the chain — the old "Master Volume" knob is now **Synth
Vol** (`master_volume`, unchanged ID/recall), and a true JUCE-side **Out Gain**
(`output_gain`, dB, block-ramped) was added at the very end, after the bitcrusher +
saturator. MASTER row reads Freq · Bit · Drive · Synth Vol · Out Gain; FX rack widened
to 330 px to fit. Recalled via state + PatchModel.

**Output saturation ✅ (2026-06-30):** an analog-warmth stage on the plugin output,
ported from the Kalos mastering plugin — a Wave Digital Filter antiparallel-diode
(LED) clipper (`src/dsp/WdfClipper.h`, a self-contained, RT-safe transcription of
Kalos's Faust `KalosSoftClipper.dsp`; diode WDF model by Dirk Roosenburg). A single
**Drive** knob (Kalos's "THD", `clip_drive`, ±24 dB) pushes the signal into the diodes
with built-in **gain compensation** (pre-multiply / post-divide) so the level stays
steady as you drive harder; 5 Hz DC-block + hard ceiling at 0 dBFS (doubles as a
safety limiter). Runs in `processBlock` after AMY's chain (Software mode only); Drive
sits in the FX rack's MASTER section. Recalled via state + PatchModel (host-side DSP,
not an AMY wire param). Unit-tested (silence, ceiling bound, saturation-vs-crest,
compensation level-steadiness); auval + pluginval strictness 7 pass.

**Effects deepened ✅ (2026-06-30):** the FX rack exposes AMY's full per-effect
parameter lists, not just the mix level — Reverb Size/Damping (`h`), Chorus
Rate/Depth (`k`), Echo Time/Feedback/Tone (`M`); buffer-size params are pinned to
AMY's defaults. Wired through Parameters + PatchModel + `streamGlobalFx` (full lists)
+ serialization + UI; verified AMY engages them (echo+feedback tail) and a wire/
round-trip unit test.

**Editor layout ✅ (2026-06-30):** both engine tabs now use a **two-column** section
layout (DX7: OP1│OP2 / OP3│OP4 / OP5│OP6; Juno: OSC A│OSC B / LFO│VCF / VCF ENV│AMP
ENV) via a small `TwoColumnPanels` container, and the DX7 tab's top row gathers the
algorithm diagram + algorithm selector + feedback knob. Window resized to 1280×740.

**M3 refinements ✅ (2026-06-29):**
- **DX7 release fix.** The ALGO controller osc no longer carries a master amp
  envelope (it gated the whole voice off before the operators released, so note-off
  tails were clamped to ~50–250 ms regardless of the Release knobs). It now has a
  constant velocity-sensitive amp (`a1,0,1,0,0,0`, matching `fm.py`) and the
  per-operator A/D/S/R own the voice's release. Regression test in `EngineRenderTests`.
- **Analog release fix ✅ (2026-06-30).** Same class of bug: the amp env sat on the
  silent VCA osc0, but AMY frees the sound-producing oscs on note-off the instant
  they have no release env of their own — so the chained audio was cut dead before
  osc0's VCA could fade it (release knob did nothing). The amp env now lives on the
  AUDIO oscs (osc2/osc3, `a<level>,0,0,<level>,0,0` + `A<adsr>`); osc0 keeps only the
  filter env. Both `emitAnalog` and `streamAnalogParams` updated; regression test
  (long release rings where short is silent) in `EngineRenderTests`.
- **Master Volume knob** added to the FX rack (the AMY `0.1*volume` scaler), and
  master volume + reverb/chorus/echo/EQ now stream live in **all** engines via a
  shared `streamGlobalFx()` (previously inconsistent per engine — Analog didn't
  stream them at all).
- **Algorithm menu + live diagram.** `fmAlgorithm` is now a 32-entry choice (was an
  int knob; same normalised stepping, so recall/automation are unchanged). The DX7
  tab draws the selected algorithm's operator graph (`AlgorithmDiagram`) — carriers
  vs modulators, connections and an **FB** tag on the feedback op — reconstructed
  from AMY's bus routing in `FmAlgorithms.h` (topology unit-tested for algos 1/2/22).

**M3d done ✅ (2026-06-29):** **DX7 `.syx` cartridge import.** An "Import DX7…"
button reads a standard DX7 SysEx file (32-voice packed bulk dump *or* single-voice
VCED, with/without the F0..F7 wrapper) and turns every voice into a named **FM-engine
user patch** in the PatchLibrary — load one and the DX7 tab populates. Imported voices
are filed under a **group folder named after the cartridge** (PatchLibrary now scans
one level of subfolders; the USER combo shows section headings) so they don't clutter
the user's own "My Patches" list. Also fixed an across-the-board **−20 dB level**: AMY
treats `volume=10` as unity (`0.1*volume`), so the master-volume default moved 1.0 → 4.0. `Dx7Import` is
a faithful C++ port of AMY's own `fm.py` converter (the script behind the factory DX7
bank): exact operator ratios (`coarse·(1+(fine+(detune−7)/8)/100)`, coarse 0 ⇒ 0.5),
output level (`2·2^((L−99)/8)`), feedback (`0.00125·2^fb`) and algorithm; the DX7
4-rate/4-level EG is approximated as our per-op A/D/S/R via AMY's own rate→time curve.
Operator order matches `emitFm`'s `O6,5,4,3,2,1` (our ops[0] = DX7 operator 1).
**Verified:** packed-bulk decoding is cross-checked against the VCED layout in a unit
test (no real cartridge needed); 28/28 ctest, auval + pluginval strictness 7 (AU+VST3)
pass. **Deferred:** LFO/vibrato, keyboard rate/level scaling, velocity sensitivity and
true fixed-frequency operators (approximated as a ratio) — see `Dx7Import.cpp`.

**M3c done ✅ (2026-06-29):** the **DX7 tab** is now a real editable 6-operator FM
voice on AMY's ALGO engine — osc0 is the ALGO controller (algorithm 1–32 + feedback;
see the M3 release-fix note re: its amp), oscs 1–6 are sine operators each with a frequency ratio, output
level and full A/D/S/R. Operator amp = `a<level>,0,0,<level>,0,0` (= level·(1+eg0),
so level 0 is truly silent and note-offs stay clean), pitch = note·ratio (`I`), and
the operator list is emitted `O6,5,4,3,2,1` because AMY's algorithm table indexes
operators 6→1 (so our "OP 1" = DX7 operator 1). `PatchModel` gained `FmParams`/`FmOp`
+ `emitFm`; the `engine` param is now a 3-way Factory/Analog/FM choice (top-bar combo
+ tab auto-select + dimming); algorithm changes rebuild, everything else streams
RT-safe. **Verified:** an offline engine-render test proves the FM voice is audible
and silences on note-off; 25/25 ctest, auval + pluginval strictness 7 (AU+VST3, incl.
FM-param + engine-switch fuzzing) pass. **Deferred:** FM LFO/vibrato (operators take
no `L`), per-op fixed-frequency mode, keyboard scaling/velocity sens; AMYboard (M4).

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
**Core done 🔧 (2026-07-02, pending real-board test):** the **AMYboard tab** now picks a
MIDI-out device, Connect/Disconnect, a Software/Hardware **Mode** toggle, and **Send Patch
to Board**. `HardwareBackend` sends notes/CC/pitch-bend as MIDI and every patch edit as
wire **SysEx** (`F0 00 03 45 … F7`); `rebuildFrom` now re-sends the whole patch on load /
mode-switch / button. All outgoing MIDI is **enqueued and flushed by a dedicated sender
thread** (`HardwareBackend : juce::Thread`), so the audio thread never touches the blocking
MIDI driver; with no device open it falls back to the host MIDI-out (`collectHostMidi`).
Builds + auval + pluginval strictness 7 pass. **Still open:** verify against a real board;
`zI` ping **reply** detection (needs a MIDI-in — the "auto-detect" scope); pitch-bend-range
RPN. See HARDWARE_MODE.md.
**DoD:** flip to Hardware mode, play the physical AMYboard from the DAW, recall a
session onto the board.

## M5 — Depth & polish
- **Replace AMY's bus effects with host-side DSP (reverb/chorus/echo/EQ).** AMY's
  effects are limited: (a) the echo has a **single** feedback filter coefficient, so
  it can only be LPF *or* HPF — no independent "Dark + Thin" repeats; (b) cutting any
  effect level to exactly **0 pops** — AMY bypasses the effect on the final `→0` step
  with no internal ramp, so our streaming/smoothing can't prevent it. Own the effects
  (like the WDF clipper + bitcrusher) for better quality and control: a stereo echo
  with separate Dark(LPF)+Thin(HPF) feedback filters (+ ping-pong / tempo sync), a
  proper reverb, and click-free level automation. Leaves AMY doing synthesis only.
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
