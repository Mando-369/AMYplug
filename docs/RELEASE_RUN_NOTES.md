# Release run — briefing for the machine doing the testing

Scratch notes for whoever (or whatever) picks up `docs/TEST_PROTOCOL.md` on the test machine.
Not part of the protocol; delete or update freely.

## ⚠️ The test machine runs the ZIP, not a source build

The M1 tests `dist/AMYplug-macOS.zip` via `install.sh` — it does not `git pull` or build.
Three consequences:

- **Only the zip's date says what is under test.** The zip that was on the dev machine until
  2026-09-02 22:01 was built on **2026-08-08** — before every commit on this branch. Any test
  done against it tested none of this work. Copy the **22:01** zip (or newer) over first.
- `install.sh` clears Gatekeeper quarantine and ad-hoc re-signs each bundle, so the unsigned
  archive does load. All five bundles are ad-hoc signed and universal (arm64 + x86_64).
- The plugin under test is then whatever the DEV machine built — JUCE **8.0.14**, not the
  pinned 8.0.13 CI uses. Record that in §4.3. To test the 8.0.13 build instead, build on the
  M1 per protocol §0.1 and package there.

Rebuild the zip after every change you want tested: `./scripts/package.sh` on the dev
machine (it stages `build/mac-release` — so build first).

## What is being tested

Branch `feat/ui-chrome-size-picker-and-engine-fixes`, six commits on top of `7ba9e51`:

| | |
|---|---|
| `9ed4ab8` | **fix(engine)** pitch bend no longer leaks into either engine's LFO |
| `ee1fdcc` | **fix(engine)** per-osc streaming gated on the built osc graph — was writing out of range on every engine switch |
| `069a970` | **feat(ui)** editor scaling + size picker, and restyled popup menus and dialogs |
| `849eb5b` | **test** the §2 stress suite |
| `5ac787a` | **docs** the release test protocol itself |
| `2607d01` | **docs** fresh-machine setup |
| `0306347` | **docs** this briefing |
| `3de4fe4` | **fix(preset)** the loaded preset's NAME now recalls with the sound; dirty mark |
| `e03d8ef` | **feat(preset)** bank operations — create / rename / move / delete, to the Trash |
| `1d3cf79` | **feat(fx)** on/off switch on every FX card |
| `3e77411` | **feat(preset)** PRESETS tab, one header field, bank-aware Save As and DX7 import |

AMY submodule stays at **1.2.16** (`dde55aac`). A bump to 1.2.162 was evaluated and **not
taken** — see the `amy-submodule-bump` note if you have the memory dir, otherwise the short
version is: it works, but it changes four test expectations and brings the drum kits, so it is
a separate decision.

## Already done on the dev machine — do NOT assume it transfers

Run on macOS with **JUCE 8.0.14**, AMY 1.2.16, universal build:

- §1.1 build clean · §1.2 **71/71** · §1.3 pluginval **8/8** clean at strictness 10 · §1.4 auval both
- §2 stress **67/67**; ASan/UBSan/TSan clean **in our code** (every finding was upstream in
  `third_party/amy/`); **RTSan NOT RUN** — Apple clang cannot do it
- §3 **entirely untouched** — that is the job

The test machine will build against a **different JUCE (8.0.13)**. That is deliberate: it
matches CI, and §3.8 (first-run on a machine that never had AMYplug) only means something on a
clean machine. Record the JUCE version in §4.3.

## What changed since the first test round (the six items you reported)

- **Name not recalled on reload** — fixed (`3de4fe4`); §3.2 now has a row for it.
- **Asterisk for an edited preset** — in the header field and the PRESETS info; full inclusion.
- **FX on/off toggles** — power button on each FX card; they dirty the preset; the clipper
  stays as the 0 dBFS ceiling when off. The AMY pop at level 0 is known and accepted.
- **Arrows for user presets** — ‹ › now walk factory and user presets as one list, wrapping.
- **Banks / subfolders** — the PRESETS tab; BANK is an editable box in Save As, Move, Import.
- **Sticky user-preset menu** — not reproduced; left as a watch item in §3.3.

## Where to concentrate

§3 is the whole point, and three of its groups exercise things changed on this branch — so
they are where a regression would actually live:

- **§3.1 no hanging notes** — the streaming-gate commit touches the engine-switch path. Do the
  Software↔Hardware switch row and the change-patch row properly.
- **§3.3 modal chrome** — all new. Drag the plugin window with a menu open; click a combo twice;
  close the window with a menu open; check menus scale **with** the panel at 150%.
- **§3.4 editor size** — all new. Corner-drag to an odd size, close and reopen the window, save
  and reopen the project.

§3.2 recall matters more than usual too: the editor size AND the loaded-preset identity are
new state on the `AMYplugState` root, so a project saved by the **previous** release must
still open — it should come up on whatever factory patch `patchA` says, clean.

**§3.8 is new and is the bulk of this round** — every one of your six reports lands there
or in §3.2. Budget it like §3.1.

## Known-good expectations, so a surprise reads as a surprise

- `ctest` = **77/77**. `[.stress]` = **67 assertions in 9 cases**.
- pluginval: an intermittent `Plugin state restoration` failure on a **bool** parameter is a
  documented race, not a bug — that is why it runs four times.
- A build warning in `src/` is only a finding if it is *new*; the tree already carries ~130
  pre-existing ones (mostly `-Wfloat-equal` and sign-conversion).

## If something fails

Capture the command and full output before changing anything. The three most likely causes of
a *spurious* failure, in order: a JUCE version difference (8.0.13 vs 8.0.14), the first auval
after a fresh build (re-run once), and the pluginval bool race above.
