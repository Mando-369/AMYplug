# AMYplug — Code Review & Switching-Bug Diagnosis

_Review date: 2026-07-03 · Scope: first-party `src/`, build, tests, structure (third_party excluded) · 45 commits, ~4,800 LOC._

## TL;DR

- **Structure & best practices: strong (A-).** Clean separation of concerns, correct APVTS usage, dual-tree state recall, disciplined realtime audio path, real test suite (48 cases), and CI with `pluginval` + `auval`. This is unusually solid for a first-of-its-kind plugin.
- **The "buggy when switching engines/tabs" behaviour is OUR code, not AMY.** It is a small number of concrete integration bugs in the *rebuild* and *UI-sync* paths, all fixable. AMY itself executes wire messages deterministically.
- **Four root causes** produce the glitches, stuck notes, and laggy/sticky feel. All are listed below with fixes, in priority order.

---

## Is it AMY or our code?

**Our code.** AMY is a deterministic wire-message interpreter; the only *inherent* AMY hazards are (a) it has global static state (≈one instance per process) and (b) changing a patch/engine resets oscillators, which naturally cuts sounding voices. Both are expected and manageable. The problems you feel come from how we drive AMY around a switch, specifically:

1. We rebuild AMY's voice graph **without pausing audio and without telling the note tracker**, so voices get torn down mid-render while the plugin still "thinks" notes are held.
2. We reconcile the tabs and the engine selector by **polling 15×/second and cross-writing host parameters**, which is feedback-prone and laggy.
3. The AMY global-state guard we wrote (`EngineOwnership`) **is never actually consulted**, so a second plugin instance can stomp the shared engine.

None of these require changing AMY.

---

## Root causes of the switching bug (fix these, in order)

### 1. Rebuild is not fenced against the audio thread — CRITICAL
`AmyPlugProcessor` — `handleAsyncUpdate()` → `rebuildEngineFromModel()` (~lines 329–334, 710–718).
The plain structural rebuild (engine change, patch change, voice-mode change) runs on the message thread and calls `active->rebuildFrom(model)` **without `suspendProcessing()`**, while `processBlock` is concurrently draining wires and pulling audio. `setMode()` already fences correctly (it calls `suspendProcessing` + `router.allNotesOff`), but the ordinary rebuild path does not.
**Fix:** wrap every rebuild in `suspendProcessing(true) … suspendProcessing(false)` (mirror `setMode`), or route the reset+rebuild wires through the existing RT wire-FIFO so they apply atomically at the top of a block.

### 2. Rebuild doesn't flush the NoteRouter → stuck notes — CRITICAL
`AmyPlugProcessor` rebuild path vs `NoteRouter::allNotesOff` (router flushed only in `setMode`, ~line 293).
A rebuild queues `RESET_SYNTHS`/`RESET_ALL_OSCS` and can recreate the synth with fewer voices (e.g. Poly→Mono forcing `numVoices=1`), silencing live AMY voices — but `NoteRouter.active[]`/`activeCount` are never cleared, so the matching note-offs go to dead voices and the tracker stays "stuck on". This is the stuck-notes-on-switch you're hearing.
**Fix:** call `router.allNotesOff(active)` at the **start of every rebuild** (engine/patch/voice-mode/state-load), not only on mode switch. Also reset the mono/legato stacks (`heldStack/heldCount/sounding`) there.

### 3. Editor reconciles tabs ↔ engine by polling + host writes — HIGH
`AmyPlugEditor::timerCallback()` (~lines 671–761), 15 Hz timer.
Tab changes and engine-param changes are detected by polling and then cross-written with `setValueNotifyingHost`. This causes: up to ~66 ms latency ("sticky"), feedback bounce between the two `lastTab`/`lastEngine` guards, spurious automation writes / project-dirty flags, and fights with host automation. A coincident engine change *during* a mode switch is silently dropped (the mode-vs-rebuild branch, ~lines 714–717).
**Fix:** replace polling with events — override `TabbedComponent`'s tab-changed callback and add an `APVTS` parameter listener for `engine`; guard against re-entrancy with a `bool syncing` flag so a UI-driven engine change doesn't ping-pong the tab. Keep `timerCallback` only for read-only status text.

### 4. Uncoalesced rebuild trigger + engine-branched streamers race the rebuild — HIGH
`AmyPlugProcessor::parameterChanged` (~835–841) fires a bare `triggerAsyncUpdate()` for *every* structural param, and `streamMacrosToBackend` (~417–418) branches on the *live* engine atomic while a rebuild for a different engine is still pending — so for a block or two the audio thread streams Analog wires into an FM osc graph (or vice-versa). Loading a preset fires ~50 of these in a burst.
**Fix:** add a generation counter that only advances after `rebuildFrom` completes; streamers skip until the graph matches. Debounce `triggerAsyncUpdate` (coalesce a burst into one rebuild) and batch preset application behind a "suspend notifications" flag.

### 5. `EngineOwnership` is never consulted — HIGH (multi-instance)
`EngineOwnership.h` exists but `SoftwareBackend` never calls `ownsSoftware(this)` before `noteOn`/`streamWire`/`processBlock`/`rebuildFrom`. A second plugin instance therefore streams into the shared global AMY engine.
**Fix:** gate all AMY-touching methods on the ownership check the header was written to provide; non-owners render silence (the UI already has a "take over" affordance).

---

## Best-practice assessment

**Structure — strong.** `engine/` (AMY behind one TU), `midi/NoteRouter` (note lifecycle), `state/` (PatchModel, Parameters, PatchLibrary, Dx7Import, FmAlgorithms), `dsp/` (header-only, dependency-free), processor/editor at root. Nothing misplaced. The two big files (`AmyPlugProcessor.cpp` 886, `AmyPlugEditor.cpp` 836) are getting large — later, extract a `MacroStreamer` and a `StateSerializer` from the processor.

**Realtime safety — mostly strong.** `processBlock` allocates nothing: SPSC `juce::AbstractFifo` wire queue, `std::atomic` flags, gain via `applyGainRamp`, and `WireBuilder` confirmed to use a fixed 256-byte stack buffer (no heap on the note path). The gaps are the *coordination* bugs above (rebuild fencing + router flush), not per-sample allocation. One nit: `processBlock` does a `dynamic_cast<HardwareBackend*>` every block (~line 263) — cache the typed pointer at mode-switch.

**Parameters & recall — correct.** APVTS with stable string IDs and an explicit "append-only, never renumber" rule; state persists *both* APVTS and PatchModel and rebuilds AMY from the model on load. Good.

**Tests — good, with gaps.** 48 cases: NoteRouter lifecycle (11), state round-trip (13), DSP, DX7 import, engine render. Missing: a **full-plugin** recall test (construct processor → set params → `getStateInformation` → new instance → `setStateInformation` → assert equality), HardwareBackend SysEx framing, and — most relevant here — a **stuck-notes-across-switch** regression test.

**Hygiene — one real issue.** `build/`, `dx7_sounds/` (32 MB), `CMakeUserPresets.json`, `.DS_Store` are all correctly untracked/ignored. But **`testaudio/` (3.8 MB of .wav) is untracked AND not in `.gitignore`** — it shows as `??` and risks being `git add .`-ed into history. Add it to `.gitignore`. The JUCE submodule being un-checked-out is a deliberate, documented choice (CI fetches pinned JUCE), not a problem.

---

## Recommended fix sequence

1. Write a **failing test first**: hold notes → switch engine/patch mid-note → assert `NoteRouter.activeCount == 0` and no residual voices. This pins the bug and proves the fix.
2. Fix **#2 (router flush on rebuild)** and **#1 (fence rebuild)** together — they are the audible glitch + stuck notes.
3. Fix **#4 (generation counter + debounce)** so streamers can't race the rebuild.
4. Replace **#3 (editor polling)** with tab/param callbacks + a re-entrancy guard.
5. Wire up **#5 (EngineOwnership)** so multi-instance is safe.
6. Add `testaudio/` to `.gitignore`; add the full-plugin recall test.

---

## Which Claude Code tool to use

This is a targeted debugging + refactor job, so drive it as one focused Claude Code session:

- **`/code-review`** on the working branch/diff to get an inline pass, and hand it this document as the spec.
- **Plan subagent (Task tool → "Plan")** for causes #1/#2/#4 — the rebuild-fencing + router-flush + generation-counter change touches the processor's threading model and benefits from a written plan before editing.
- **Test-first with the existing harness:** add the failing stuck-notes test, then `ctest --preset mac-release`.
- **`pluginval --strictness-level 10`** (bump from the CI's 7) plus a run in **JUCE's AudioPluginHost**, automating the engine/patch params, to catch RT/threading violations the unit tests can't.
- Keep AMY pristine — every fix here is in our wrapper, never in `third_party/amy`.

A good one-line kickoff for Claude Code:
> "Read docs/CODE_REVIEW.md. Add a failing test that switching engine/patch while notes are held leaves NoteRouter.activeCount==0, then fix root causes #1, #2 and #4: flush the NoteRouter and suspendProcessing around every rebuild, and add an engine generation counter so macro streamers skip until rebuildFrom completes. Run ctest and pluginval strictness 10."

---

## Review outcome — verified against source (2026-07-03)

A follow-up pass verified each cause against the current code. Verdicts:

- **#1 (fence rebuild) — REFUTED.** `SoftwareBackend::rebuildFrom` does **not** call AMY;
  it pushes wire strings into the SPSC `wireFifo`, drained on the audio thread. The FIFO
  already confines `amy_add_message` to a single thread, so there is **no data race** and
  `suspendProcessing` is unnecessary (and would *regress* — it stalls the audio callback on
  every patch/engine change). `setMode` fences only because it calls `amy_start/stop`.
  - *Underlying real observation (deferred):* `amy_add_message` allocates **synchronously**
    for structural messages (`amy_add_message → amy_add_event → amy_event_to_deltas_queue →
    ensure_osc_allocd/malloc`, and `patches_load_patch` for patch loads). So draining a
    rebuild **allocates on the audio thread** — a violation of "no alloc in processBlock",
    but benign on desktop (patch changes are user-rate; malloc rarely underruns). Truly
    fixing it needs a double-buffered engine / worker that owns AMY. **Note for later.**
- **#2 (flush NoteRouter on rebuild) — CONFIRMED, FIXED.** `rebuildEngineFromModel` now sets
  an atomic `routerFlushRequested`; `processBlock` consumes it and calls
  `router.allNotesOff` (RT-safe — the router is not touched off the audio thread).
  `allNotesOff` already fully clears the mono/legato stacks. Guarded by a new NoteRouter test.
- **#3 (editor polling) — PARTIAL.** The "engine change dropped during a mode switch"
  sub-claim is **false** (`setMode` ends with `rebuildEngineFromModel`, which re-reads
  `engine` from the model). The 15 Hz polling is a quality smell worth replacing with
  tab/param callbacks. **Note for later (improve).**
- **#4 (streamers race rebuild) — MOSTLY REFUTED.** JUCE's `AsyncUpdater` already coalesces
  a burst into one rebuild, and only structural params are listeners — so "~50 rebuilds on
  preset load" doesn't happen. Residual: ~1 block where `streamMacros` streams new-engine
  wires onto the old graph before the rebuild drains. A generation counter would close it.
  **Note for later (nice-to-have, not critical).**
- **#5 (EngineOwnership unused) — RESOLVED** by `0bf7753`: consulted in
  `AmyPlugProcessor::processBlock` (non-owners render silence and touch nothing global).
  - *Follow-up fixed here:* a non-owner still ran `rebuildEngineFromModel`, pushing into a
    FIFO it never drains → overflow → corrupt patch on take-over. `rebuildEngineFromModel`
    now skips the push unless this instance owns the engine.

### Deferred (note-for-later, not blocking)
1. **#4 generation counter** — skip macro streaming until `rebuildFrom` for the new engine lands.
2. **#3 editor event-driven sync** — replace the 15 Hz poll with `TabbedComponent`/APVTS callbacks + a re-entrancy guard.
3. **Structural allocation on the audio thread** (from #1) — a double-buffered/worker-owned AMY engine would remove it; folds naturally into the planned host-side engine rework.
4. **Cache the `dynamic_cast<HardwareBackend*>`** at mode-switch instead of per block.
5. **Full-plugin recall + switch-while-held regression test** — needs a `juce_audio_processors`-linked harness (the logic-test target can't construct `AmyPlugProcessor`).
