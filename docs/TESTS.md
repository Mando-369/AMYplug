# What the tests cover, and why

One line per test: what it holds down, and **why it exists** — the bug it caught, or the
invariant that breaks silently without it. Read this to pick the narrowest selection that
covers a change, instead of running all 84.

**Not every change needs tests.** A font, colour, label, layout or copy change gets a build
and a look. Tests earn their minutes when a change touches the **engine, the note lifecycle,
state/recall, or a file format** — the four places where a break is silent.

Keep this file in the same commit as the test. `docs/TEST_PROTOCOL.md` is the release gate and
covers § 3, the manual checks no binary here can reach.

```bash
ctest --preset mac-release                  # all 81
<binary> "[tag]"                            # one area — tags below
<binary> "<part of the test name>"          # one test
```

Binaries live at `build/mac-release/tests/<name>_artefacts/Release/<name>`.

---

## `amyplug_tests` — pure logic, no JUCE, no AMY (11)

Instant. Run these on any change to the wire protocol or the master-stage DSP.

| # | Test | Why it exists |
|---|---|---|
| 1 | BitCrusher is a true bypass at defaults | 16 bit / full rate must be *bit-identical*, not "almost" — the crusher sits in the path of every patch, so a lossy default would colour the whole plugin |
| 2 | BitCrusher reduces bit depth to a small set of levels | Proves quantisation actually happens; an off-by-one in the level count is inaudible on a knob sweep |
| 3 | BitCrusher sample-rate reduction holds samples in groups | The hold, not a filter, is what makes the sound — a wrong group length reads as "quieter", not "broken" |
| 4 | BitCrusher passes silence through as silence | Any DC or bias offset here would be audible on every silent bar |
| 5 | WdfClipper passes silence through as silence | Same, for the diode stage — a nonlinear solver settling to non-zero is a classic WDF bug |
| 6 | WdfClipper never exceeds the output ceiling | The clipper is the plugin's 0 dBFS guarantee. If it can overshoot, nothing else stops it |
| 7 | WdfClipper drive adds saturation (crest factor falls as drive rises) | Proves drive does harmonic work rather than just gain |
| 8 | WdfClipper gain compensation keeps level steady | Drive must change *character*, not loudness, or the control is unusable |
| 9 | WireBuilder emits compact AMY wire strings | The letter codes are AMY's whole control surface; one wrong letter silently addresses another parameter |
| 10 | Wire messages terminate exactly once | A missing or doubled terminator makes AMY drop or merge messages — no error, just a patch that half-loads |
| 11 | SysEx wrapping uses AMY manufacturer id 00 03 45 | A wrong id means the board ignores every message and looks "not connected" |

## `amyplug_engine_tests` — drives the real AMY engine (10)

Slower (renders audio). Run on any change to `SoftwareBackend`, `PatchModel` emission, or
the note path. **These are the ones that prove sound actually comes out.**

| # | Test | Why it exists |
|---|---|---|
| 12 | AMY renders audible sound for note 60 on Juno patch 0 | The smoke test. If this fails, nothing below means anything |
| 13 | Filter-cutoff macro audibly changes timbre | Proves host automation reaches the engine and *changes the audio* — a parameter can move while the wire message never lands |
| 14 | Legato pitch-only change glides without retriggering (analog) | Legato is a changeNote, not a note-off/on; a regression here re-articulates every slur |
| 15 | FM operators reset phase per note-on | Without it the same note sounds different depending on when you play it |
| 16 | FM (ALGO) voice renders sound and silences on note-off | The FM half of #12, plus the note-off — a stuck FM voice is the worst-case bug |
| 17 | FM operator release controls the note-off tail (**regression**) | The tail once ignored release entirely |
| 18 | Analog amp release controls the note-off tail (**regression**) | Same failure on the analog engine |
| 19 | RESET_ALL_NOTES silences a held note | The panic / transport-stop path. Goal #1 of this plugin is no hanging notes |
| 20 | FM ALGO pitch: the EG0 pitch-env term raises the voice one octave | Pitch-EG maths is easy to get an octave wrong and hard to hear as *wrong* |
| 21 | Pitch bend moves the audio oscs but **not** the LFOs | A real measured bug: AMY's `ctrl_inputs[COEF_BEND]` is unguarded, so bend leaked into both engines' LFOs and made vibrato speed follow the wheel |

## `amyplug_logic_tests` — JUCE, no GUI (54)

The bulk. Fast. Tags let you run one area.

### `[router]` — the note lifecycle (34-46)
The anti-hanging-note brain. **Run all of `[router]` for any change to `NoteRouter`, voice
mode, or the mode switch.**

| # | Test | Why it exists |
|---|---|---|
| 34 | Every note-on is balanced by exactly one note-off | The core invariant of the whole plugin |
| 35 | Note-on with velocity 0 is treated as note-off | Many controllers send this instead of note-off; miss it and notes hang on that hardware only |
| 36 | Transport stop flushes held notes | Stop mid-note must silence, not sustain forever |
| 37 | Panic / allNotesOff releases everything and clears tracking | Releasing without clearing leaves phantom entries that break the *next* note |
| 38 | Sustain pedal defers note-offs and flushes on release | Deferred offs are where hanging notes hide |
| 39 | Sustain held at transport stop still flushes | The two deferral paths combined — the case that actually hangs |
| 40 | Notes route to the single AMY synth regardless of MIDI channel | AMY has one synth here; a channel-keyed lookup would drop notes from channel 2+ |
| 41 | Note transpose shifts the AMY note but tracks the original | Track the shifted note and the note-off addresses the wrong voice — a hang |
| 42 | Mono: last-note priority with retrigger | |
| 43 | Mono: releasing a held non-top note keeps the top sounding | The classic mono-stack bug |
| 44 | Legato: overlapping notes glide, do not retrigger | |
| 45 | Mono still flushes on panic / transport stop | The mono stack is separate state and was a second way to hang |
| 46 | allNotesOff clears the mono held-stack | No phantom note after a rebuild |

### `[state]` `[analog]` `[fm]` `[env]` `[lfo]` `[unison]` — patch model and engines (55-74)
**Run on any change to `PatchModel`, `Parameters`, or engine wire emission — this is where
recall breaks silently.**

| # | Test | Why it exists |
|---|---|---|
| 55 | PatchModel ValueTree round-trip is lossless | If this fails, projects do not reload the same sound |
| 56 | toWireMessages rebuilds in order: reset, patch, macros, FX | Order *is* semantics — AMY keeps the previous value for anything omitted, so a reordered rebuild silently inherits stale state |
| 57 | Effects emit AMY's full parameter lists and round-trip | A short list leaves the previous patch's FX values in place |
| 58 | amp-ADSR macro overrides bp0 for Juno, **not** DX7 | On DX7 bp0 is the pitch envelope; overriding it detunes the whole voice |
| 59 | Both engines' LFO oscillators ignore pitch bend | The offline half of #21. Guards the exact comma count in the coef string — an earlier fix used five commas and put the zero on `COEF_MOD` |
| 60 | Analog engine builds the 4-oscillator subtractive voice | |
| 61 | Analog params survive the ValueTree round-trip | |
| 62 | Analog LFO Free/Sync phase-lock the per-voice LFO; Poly/Key do not | Four modes, two behaviours; easy to wire the wrong pair |
| 63 | Analog LFO tempo-sync converts note division + BPM to Hz | |
| 64 | FM engine builds the 6-operator ALGO voice | |
| 65 | FM LFO: vibrato/tremolo emit as mod-coefs on the ALGO/operator oscs | |
| 66 | FM Velocity Sensitivity bakes **no** COEF_VEL | Velocity scales level live; baking it freezes the first note's velocity into the patch |
| 67 | velLevelScale: DX7 KVS scales operator level by velocity | The live half of #66 |
| 68 | FM Transpose is a note shift, **not** baked into the wire | Baked, it would double-apply on every rebuild |
| 69 | FM algorithm carriers match the known DX7 topologies | 32 algorithms; a wrong carrier set is a wrong patch, not a crash |
| 70 | FM algorithm topology reconstructs modulation + feedback | |
| 71 | FM params survive the ValueTree round-trip | |
| 72 | amp ADSR encodes as a 6-field bp0 breakpoint string | Field count is load-bearing — see #59 |
| 73 | Analog Coarse/Fine tune the osc; Glide emits portamento; perf recalls | |
| 74 | Analog unison stacks detuned osc copies (chained) | |

### `[factory]` `[fx]` — decode and switches (26-33, 75)

| # | Test | Why it exists |
|---|---|---|
| 26 | Factory wire decode maps a DX7 preset onto OP1..6 | "To Editor" is only useful if the decode is faithful |
| 28 | Factory wire decode rejects a non-FM (Juno) patch | Decoding a Juno patch as FM would produce a plausible-looking wrong patch |
| 29 | Factory analog decode maps a Juno preset onto OSC A/B/C/D + VCF/LFO | |
| 30 | Factory analog decode rejects an FM (DX7) patch | The mirror of #28 |
| 31 | DX7 4R/4L envelope decode↔emit round-trips the factory shapes | |
| 32 | DX7 pitch-EG decode↔emit round-trips | |
| 33 | AMS follows the DX7's non-linear sensitivity table | It is a lookup table, not a curve; interpolating it is wrong but sounds fine |
| 75 | FX switches: off emits the effect's neutral value and round-trips | "Off" is level 0 / EQ flat, not a bypass — and a patch with reverb off **is** a different patch, so it must recall |

### `[library]` `[preset]` `[names]` — the preset library (47-54)
**Run on any change to `PatchLibrary`, banks, or the preset catalogue.** These touch the
user's files on disk.

| # | Test | Why it exists |
|---|---|---|
| 47 | PatchLibrary saves and reloads a named preset | |
| 48 | Groups (subfolders) keep imports out of the user list | A 32-voice DX7 import must not flood the top-level list |
| 49 | PatchLibrary rejects empty names | An empty name writes a dotfile the user cannot see or delete |
| 50 | Lists banks from the folder tree, including empty ones | A bank you just created must appear before it has contents |
| 51 | Bank operations: create, move, rename, delete | All four go to the **Trash**, never `deleteFile` — this is user data |
| 52 | Prunes an emptied bank even with a Finder `.DS_Store` | Otherwise a bank looks non-empty forever and never cleans up |
| 53 | Built-in patch names are generated for all 258 patches | A gap here shows as a blank row in the browser |
| 54 | PresetCatalog: factory first, user after, wrapping steps | What the ‹ › arrows walk |

### `[fm]` DX7 import (22-25, 27)

| # | Test | Why it exists |
|---|---|---|
| 22 | DX7 VCED single voice converts with correct operator order | DX7 stores operators in reverse; getting it wrong yields a valid-looking, wrong patch |
| 23 | Packed bulk decodes identically to VCED | Cross-checks the bit-offset maths against a second format — the only way to catch a shared misreading |
| 24 | DX7 import builds a recallable FM PatchModel | Import is worthless if it does not survive a project reload |
| 25 | BRASS 2 decode→emit reproduces the factory operators | One known-good patch as an end-to-end anchor |
| 27 | Dx7Lfo conversions round-trip (speed/wave/depths) | |
| — | **every DX7 factory operator ratio round-trips** | The one that was missing. `ratioToCoarseFineDetune` inverted fm.py's `coarse × (1 + (fine + (detune−7)/8)/100)` by rounding, but coarse is **ambiguous** — 3.14 is not coarse 3 (which cannot reach it) but coarse 2, fine 57. It also had an epsilon too tight to survive the float32 parse the ratios arrive through, so 0.99125 → x = −0.8750021 missed the guard and the **detune inverted** (0 → 8). 43 of the bank's 752 ratios were wrong, the worst by 32%. On E.PIANO 1 — two near-unison operators beating — a 6-note phrase sat **82% mean-abs** away from the same preset in the browser; 0.08% after. Walks all 128 DX7 patches |

## `amyplug_ui_tests` — real editor + processor + libamy + fonts (8)

Slowest (builds a live editor). **Run on any change to the editor's construction, the preset
identity, or the tooltip table.** Tags: `[ui]`, `[preset]`.

| # | Test | Why it exists |
|---|---|---|
| 76 | editor size picker | The bug is in *construction*: `setResizeLimits` clamps the editor's 0×0 bounds to the minimum, firing `resized()`, which writes that back — read the stored size after it and the plugin opens at 60% forever while every menu item still works. Asserts the width the editor **opened** at |
| 77 | loaded preset identity and dirty mark | Reloading a project restored the sound but not the **name**. Also guards the dirty watcher: the APVTS adapter fires on the first write even when nothing changed, which marked every patch modified |
| 78 | tooltip table resolves generated parameter ids | `fm_op<N>_<field>` and `osc_<x>_<field>` are built at runtime, so nothing else in the build spells them out — they can stop matching silently |
| 79 | no tooltip is drawn wider than the cap | Walks **every** parameter plus the chrome strings; an over-long tip added later is clipped, not wrapped |
| 80 | hover help is wired into a live editor | `setTooltip` alone does nothing. Six calls sat in the editor with no `TooltipWindow` anywhere — dead code that read as a working feature. Asserts a **parented** window exists and that tips reached the controls on all eight tabs |
| 81 | the help preference survives a session round-trip | The host destroys the editor on every window close, so the preference has to live on the processor |
| 82 | a factory preset brings its own filter, envelope and chorus | `toWireMessages` broadcasts filterFreq, resonance and bp0 **on top of** the baked patch, so every Juno preset played through cutoff 8000, resonance 0.7 and a 5 ms attack whatever the patch said — and To Editor, which decodes the real values, then sounded like a different instrument. Patch 20's own attack is 582 ms and its own cutoff is 299 Hz |
| 83 | reopening a session does not re-adopt over what it saved | The trap in the above: adoption keys off the patch NUMBER, so a restored session looks like a fresh selection unless the guard is seeded from the restored state — and would stamp factory values over the ones the user tuned and saved. Silent recall loss |

## Hidden: `[.stress]` — not in ctest

`amyplug_ui_tests "[stress]" --order decl` — 67 assertions, minutes to run. Block-size
invariance, sample rates, NaN soak, automation, per-engine sound-and-silence, the osc-range
regression. Release only; see `docs/TEST_PROTOCOL.md` § 2, which also documents the three
traps that make an offline harness pass while proving nothing.
