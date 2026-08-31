# Release run — briefing for the machine doing the testing

Scratch notes for whoever (or whatever) picks up `docs/TEST_PROTOCOL.md` on the test machine.
Not part of the protocol; delete or update freely.

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

## Where to concentrate

§3 is the whole point, and three of its groups exercise things changed on this branch — so
they are where a regression would actually live:

- **§3.1 no hanging notes** — the streaming-gate commit touches the engine-switch path. Do the
  Software↔Hardware switch row and the change-patch row properly.
- **§3.3 modal chrome** — all new. Drag the plugin window with a menu open; click a combo twice;
  close the window with a menu open; check menus scale **with** the panel at 150%.
- **§3.4 editor size** — all new. Corner-drag to an odd size, close and reopen the window, save
  and reopen the project.

§3.2 recall matters more than usual too: the editor size is new state on the `AMYplugState`
root, so a project saved by the **previous** release must still open.

## Known-good expectations, so a surprise reads as a surprise

- `ctest` = **71/71**. `[.stress]` = **67 assertions in 9 cases**.
- pluginval: an intermittent `Plugin state restoration` failure on a **bool** parameter is a
  documented race, not a bug — that is why it runs four times.
- A build warning in `src/` is only a finding if it is *new*; the tree already carries ~130
  pre-existing ones (mostly `-Wfloat-equal` and sign-conversion).

## If something fails

Capture the command and full output before changing anything. The three most likely causes of
a *spurious* failure, in order: a JUCE version difference (8.0.13 vs 8.0.14), the first auval
after a fresh build (re-run once), and the pluginval bool race above.
