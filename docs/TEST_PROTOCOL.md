# AMYplug release test protocol

Run this end-to-end before tagging a release. It covers **two** plugins built from this
repo — `AMYplug` (instrument, `aumu Amyp Mand`) and `AMYplugFX` (effect, `aufx Amfx Mand`).

The protocol is split by **what a machine can prove**. Sections 1–2 are automated and gate
the release outright. Section 3 is manual, and it is not optional padding: it holds the
project's own #1 guarantee (no hanging notes) plus every modal and resize behaviour, none of
which can be verified without a real host, a real window and a real modal state.

> Record the outcome in the sign-off table at the bottom, and keep the filled-in copy with
> the release tag. A protocol nobody signed is a protocol nobody ran.

---

## 0. Before you start

| | |
|---|---|
| Working tree | clean (`git status`), on the release commit |
| JUCE | note the exact version you built against — see § 4.3 |
| AMY submodule | note the commit; a bump changes DSP behaviour |
| Hardware | an AMYboard on USB for § 3.5 (skip only if the release notes say Hardware mode is untested) |
| Host | at least one real DAW for § 3; two if you can (hosts differ most on window and modal behaviour) |

```bash
git status --short && git log --oneline -1
git -C third_party/amy log --oneline -1
```

### 0.1 On a machine that has never built this — read this first

⚠️ **JUCE is deliberately NOT vendored.** `.gitmodules` declares it, but there is no gitlink
in the tree, so `git submodule update --init` does nothing for JUCE. `scripts/bootstrap.sh`
papers over that by running `git submodule add -b master …/JUCE`, which pulls **whatever JUCE
tip is today** and **dirties the working tree** — both wrong for a release run, and §0 above
requires a clean tree.

Clone a **pinned** JUCE and point the build at it, exactly the way CI does:

```bash
git clone --depth 1 --branch 8.0.13 https://github.com/juce-framework/JUCE.git "$HOME/JUCE"
export JUCE="$HOME/JUCE"

git clone https://github.com/Mando-369/AMYplug.git && cd AMYplug
git checkout <release-branch-or-tag>
git submodule update --init third_party/amy     # AMY only; JUCE comes from $JUCE

cmake --preset mac-release -DAMYPLUG_JUCE_DIR="$JUCE"
```

Every later `cmake --preset mac-release` in this document inherits `AMYPLUG_JUCE_DIR` from the
cache, so it only has to be passed once.

> **Three JUCE versions are currently in play** — CI pins **8.0.13**, the primary dev machine
> runs **8.0.14**, and `bootstrap.sh` would give a release machine **master**. Pin deliberately
> and record what you used in §4.3; "AMYplug 0.x" without a JUCE version is an unactionable
> bug report.

Also needed on a fresh machine:

| | |
|---|---|
| **Xcode** (full, not just Command Line Tools) | the `mac-release` preset uses the Xcode generator, and `auval` needs a working `xcode-select` |
| **CMake** | `brew install cmake` |
| **pluginval** | download the macOS build from <https://github.com/Tracktion/pluginval/releases> and unzip it; §1.3 assumes `$PLUGINVAL` points at the binary |
| **Homebrew LLVM** *(only for the RTSan row of §2)* | `brew install llvm` — Apple clang cannot do RTSan |

The build is **universal (arm64 + x86_64)** by default, so expect roughly double the compile
time. For a faster smoke run — but **not** for the artefacts you ship — add
`-DCMAKE_OSX_ARCHITECTURES=arm64`.

---

## 1. Automated gate — all of this must be green

### 1.1 Build both presets, no errors

```bash
cmake --preset mac-release && cmake --build --preset mac-release
```

New warnings are not automatically a blocker, but read them. A warning in `src/` that was
not there last release is a finding until someone says otherwise.

### 1.2 Unit tests — 81 cases, ~1700 assertions

```bash
ctest --preset mac-release --output-on-failure
```

Four targets, and knowing which one broke tells you where to look:

| Target | Cases | What it proves | Needs |
|---|--:|---|---|
| `amyplug_tests` | 11 | Wire-message builder (`AmyWire`), SysEx framing, BitCrusher, WDF clipper | nothing — pure logic |
| `amyplug_engine_tests` | 10 | The **real AMY engine** renders, silences on note-off, honours panic; FM octave tuning; pitch bend moves the audio oscs but **not** the LFOs | libamy |
| `amyplug_logic_tests` | 54 | `NoteRouter` lifecycle (every note-on balanced, sustain, mono stack, transport stop), `PatchModel` round-trip, DX7 import/decode, patch library + **bank operations**, both engines' wire emission, **FX switches**, the preset catalogue | JUCE, no GUI |
| `amyplug_ui_tests` | 6 | Editor **construction** (opens at the stored size, corner grip, a dragged size survives a round-trip, **constructing the editor dirties nothing**), the **loaded-preset identity + dirty mark** at the real processor, and **hover help** (a parented `TooltipWindow` exists, every tab's controls carry tips, the `?` toggle round-trips, no tip is drawn wider than the 200px cap) | full editor + libamy + fonts |

⚠️ **A green `ctest` does not mean the plugin works.** These are logic and construction
tests. Everything a user actually touches is in § 3.

### 1.3 pluginval — strictness 10, run four times

```bash
# e.g. export PLUGINVAL=/Applications/pluginval.app/Contents/MacOS/pluginval
VST3=$(find build/mac-release -name "AMYplug.vst3" -maxdepth 6 | head -n1)
for i in 1 2 3 4; do
  "$PLUGINVAL" --strictness-level 10 --validate "$VST3" 2>&1 | tail -3
done
```

Repeat for `AMYplugFX.vst3`.

> **CI currently runs strictness 7.** For a release, run 10 locally. If 10 surfaces
> something CI never saw, that is the point of doing it.

⚠️ **`Plugin state restoration` failing on a *bool* parameter, intermittently, is not our
bug.** pluginval sets a bool to a random normalised value, restore is a no-op because the
value already matches, and the only remaining notification is deferred to the message
thread — so the read races and fails roughly half the time on a different subset of bools
each run. Do **not** "fix" it by pushing parameters at the host after a restore; that calls
`updateHostDisplay()` from `setStateInformation`, which JUCE explicitly warns against.
Four runs is the point: a single green run only means the coin came up heads.

Anything else failing at strictness 10 is a real finding.

### 1.4 auval — both plugins

```bash
auval -v aumu Amyp Mand      # AMYplug   (instrument)
auval -v aufx Amfx Mand      # AMYplugFX (effect)
```

`COPY_PLUGIN_AFTER_BUILD` installs the components, so a fresh build is enough. A non-zero
result on the *first* registration after a build is common; re-run before treating it as a
failure.

---

## 2. Stress and sanitizer passes

Not part of every build, but required for a release.

The first five rows are automated in `tests/StressTests.cpp`, tagged `[.stress]` so Catch2
hides them from `ctest` (they are slower and they are a release gate, not a per-commit one):

```bash
cmake --build --preset mac-release --target amyplug_ui_tests
build/mac-release/tests/amyplug_ui_tests_artefacts/Release/amyplug_ui_tests "[stress]" --order decl
```

⚠️ **Three traps when writing anything that drives the processor offline**, each of which
makes a test pass while proving nothing:

1. **The engine rebuild is deferred.** `prepareToPlay` and every structural parameter change
   call `triggerAsyncUpdate()`; `handleAsyncUpdate()` does the work on the *message* thread.
   A console harness has a `MessageManager` but nothing pumping it, so without a
   `runDispatchLoopUntil` the patch never loads and the plugin renders pure silence — which
   satisfies "stays finite" perfectly.
2. **The first block after claiming the engine is deliberately silent and clears the MIDI.**
   Send your note-on in block one and it is swallowed. Prime with a couple of empty blocks.
3. **AMY is one global engine and its state outlives a processor.** The first instance in a
   process renders from genuinely zeroed FX buffers; every later one does not. Comparing a
   reference taken first against variants taken later shows an identical divergence for
   every variant, which looks exactly like a block-size bug and is not one.

| Pass | How | Assert |
|---|---|---|
| **Edge block sizes** | render with N = 0, 1, 7, 13, 4096 | no crash, no assert, audio identical to a steady-N render |
| **Sample rates** | 44.1 / 48 / 88.2 / 96 / 192 kHz, plus a change mid-stream | in tune, no clicks — AMY's own SR is a compile-time constant, so this exercises the resampler (`docs/ENGINE_NOTES.md`) |
| **NaN / denormal soak** | +60 dB noise for several minutes through reverb + echo feedback | output stays `std::isfinite`; CPU does not climb |
| **Automation stress** | host writes fast automation on cutoff, resonance, FX sends | no zipper, no coefficient-swap bursts |
| **Bypass null** | bypassed output vs dry input | nulls |
| **RTSan** | `-fsanitize=realtime`, `[[clang::nonblocking]]` on `processBlock` | **no allocation, no lock, no I/O on the audio thread** — golden rule #1 |
| **TSan / ASan / UBSan** | separate build dir per sanitizer; run the test targets under each | clean |

⚠️ **Apple clang does not ship RTSan** (`unsupported argument 'realtime'`). It needs a
Homebrew LLVM (`/opt/homebrew/opt/llvm/bin/clang`, currently clang 22) for the whole build,
which is a different toolchain against the macOS SDK — budget time for it, and record it as
NOT RUN rather than silently skipped if it does not happen.

```bash
cmake -S . -B build/san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DAMYPLUG_JUCE_DIR="$JUCE" \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/san --target amyplug_tests amyplug_engine_tests amyplug_logic_tests
```

⚠️ AMY allocates internally for user patches (`malloc`). RTSan is the pass most likely to
find something real here — see the open risk in `docs/ROADMAP.md`.

---

## 3. Manual — none of this can be automated

Each item says *why* it needs hands, so nobody "optimises" it into a script that proves
nothing.

### 3.1 No hanging notes — the project's reason to exist

The unit tests prove `NoteRouter`'s bookkeeping. They cannot prove the audio stopped.

- [ ] Hold a chord, **stop the transport** mid-note → silence
- [ ] Hold a chord, **engage bypass** → silence
- [ ] Hold a chord, **close the plugin window** → silence
- [ ] Hold a chord, **switch Software ↔ Hardware** → silence, no stuck note on either side
- [ ] Hold a chord, hit **PANIC** → silence
- [ ] Sustain pedal down, release keys, **stop transport** → silence
- [ ] Hold a chord, **change patch** → no note survives the rebuild
- [ ] Mono/Legato: overlapping notes glide, and releasing the top note leaves nothing stuck

### 3.2 Recall — save/load must be bit-for-bit

- [ ] Build a sound on each engine (Factory / Analog / DX7), save the project, reopen → **identical sound**
- [ ] …and the header field and PRESETS info show the **same preset name**, with the same `*` state, after the reload
- [ ] Same, with a **user patch** loaded from the browser
- [ ] Same, after a **DX7 `.syx` import**
- [ ] Editor size persists (see § 3.4)
- [ ] Two instances with different patches both recall correctly
- [ ] A project saved by the **previous release** still opens (`uiScale` and any new property must default sanely)

### 3.3 Modal chrome — needs a real modal state and a window to drag

Reference: `Code Repo/JUCE-UI-LnF__15`. The `chrome` snapshot mode proves the *drawing*;
this proves the *behaviour*, and the two fail independently.

- [ ] Open a combo dropdown, **drag the plugin window** → the menu moves with it (it must not float where it opened)
- [ ] Click the combo again while its menu is open → the menu **closes and stays closed** (not two clicks, not three)
- [ ] Open a menu, **close the plugin window** → no orphaned menu left on screen
- [ ] Patch browser: bank headings styled, 128 entries scroll, scroll arrows styled
- [ ] **Save…** dialog: text field focused, Return saves, Escape cancels, name lands in the USER list
- [ ] **Import DX7…**: result dialog is ours (not a stock system box), and it is centred in the plugin window
- [ ] At 150%: menus and dialogs scale **with** the panel, not at 100% over it
- [ ] ⚠️ *Watch item:* the preset field's menu was once reported as "opens and closes immediately" on an M1, not reproduced since. If it recurs, note the editor size and the host before anything else.

### 3.3b Hover help — the only part of tooltips a machine cannot check

⚠️ **This is the one that has already shipped broken once.** Before the `?` toggle existed,
`setTooltip` was called all over the editor with no `TooltipWindow` anywhere, so not a single
tip could ever appear — and nothing failed. `tests/TooltipTests.cpp` now proves a parented
window exists and that 722 of 749 tooltip-capable components carry a tip; it **cannot** prove
a hover shows one, because `TooltipWindow` refuses to answer unless the process is in the
foreground. So hover, here, in a host.

- [ ] Hover a knob for ~half a second → a tip appears, in the AMYplug palette (not stock grey)
- [ ] Hover the knob's **LCD read-out** → the *same* tip (this is the `Slider` value-box trap)
- [ ] Hover a **combo's text**, a tab button, PANIC, the preset field → each has its own tip
- [ ] **Drag a knob** → no tip appears while the button is down
- [ ] Click **`?`** → it goes dark, and no tip appears anywhere afterwards
- [ ] Click `?` again → tips are back
- [ ] Save the project with `?` off, reopen → still off; a project saved before this build opens with tips **on**
- [ ] At 150%: the tip scales **with** the panel, and never draws outside the plugin window
- [ ] A long tip (e.g. an oscillator's Freq) wraps to two lines and is not clipped

### 3.4 Editor size — needs a window to drag

Reference: `Code Repo/JUCE-UI-LnF__13`.

- [ ] The size button is **exactly under SAVE** — same width, same left edge — with `?` beside it
- [ ] Menu presets 75 / 100 / 125 / 150 % all apply
- [ ] **Drag the corner** to an odd size → button reads that size (e.g. `113%`), no menu item ticked
- [ ] Close and reopen the plugin window → it comes back at the size you left it
- [ ] Save the project, reopen → same size
- [ ] Host-driven resize (where the host offers one) is remembered too
- [ ] Corner grip is visible but dim; brightens on hover, accent while dragging

### 3.5 Hardware mode — needs a real AMYboard

Reference: `docs/HARDWARE_MODE.md`. **Check the sample master clock first.**

- [ ] Detect finds the board's serial REPL (not the dock's other usbmodem port)
- [ ] Connect → plugin goes silent, board makes the sound
- [ ] Notes over USB-MIDI; patch/param **edits** over serial (SysEx wire is inert on real hardware)
- [ ] Send Patch pushes the current patch
- [ ] Disconnect frees the board; a second instance can then take it
- [ ] Firmware check reports a build and does not hang the UI
- [ ] Note lifecycle § 3.1 repeated **in Hardware mode**

### 3.6 Multi-instance — AMY's engine is global

- [ ] Two instances: the second reads `SILENT · engine in use by another instance`
- [ ] **USE ENGINE HERE** hands the engine over cleanly, no stuck notes on either side
- [ ] Deleting the owning instance releases the engine
- [ ] Both instances still save/recall their own state while silent

### 3.7 AMYplugFX

- [ ] Loads as an effect on an audio track in both AU and VST3
- [ ] All six cards audibly do something; defaults are a true bypass where documented
- [ ] State recalls with the project

### 3.8 Presets, banks and FX switches — needs a real host and the file system

The unit tests cover the library and the identity at the processor. This is the user's side.

- [ ] Turn any knob → `*` appears in the header field, the PRESETS info reads **Modified**; Save clears it
- [ ] Load a user preset, reload the project → the **name** comes back (the bug that started this)
- [ ] Delete that preset's file in the Finder, reload → the name still shows (an orphan), the sound is the session's
- [ ] ‹ › step through factory AND user presets, and wrap at both ends
- [ ] The field's menu: FACTORY and USER banks as submenus, the loaded one ticked; clicking the field again closes it
- [ ] PRESETS tab: click a row loads it; the tree filters; search filters; the loaded row stays marked
- [ ] Save As… with a **new bank typed** into the BANK box → the bank appears in the tree, still there after a rescan
- [ ] Make a folder by hand in the presets folder → it shows in the tree while empty
- [ ] Rename / Move to Bank / Delete a preset — each confirms, each is undoable from the **Trash**
- [ ] Right-click a user bank → Rename Bank / Delete Bank; deleting the last preset in a bank prunes the bank
- [ ] Import DX7: the BANK box pre-fills with the cartridge's name; change it to an existing bank → the voices merge there
- [ ] Every FX card's power button: off is audibly silent for that effect, the knob keeps its value, on restores it; it recalls with the project; toggling it marks the preset `*`

### 3.9 First-run experience

- [ ] Install on a machine that has **never** had AMYplug → both formats found by the host
- [ ] Default patch makes sound on the first note with no configuration
- [ ] No console spam, no missing-font fallback, no crash on quit

---

## 4. Release build and packaging

### 4.1 Package

```bash
cmake --preset mac-release && cmake --build --preset mac-release
./scripts/package.sh          # -> dist/AMYplug-macOS.zip
```

- [ ] Archive contains AU + VST3 + Standalone for AMYplug, AU + VST3 for AMYplugFX
- [ ] `install.sh`, `README.md`, `LICENSE`, `NOTICES.md`, `licenses/` present
- [ ] Unzip on a clean machine and run `install.sh` → § 3.9 passes

### 4.2 Known gaps at time of writing

These are **not** protocol failures — they are the state of `M6` in `docs/ROADMAP.md`, and
the release notes should say so rather than let a user discover them:

- **No codesign / notarization.** macOS Gatekeeper will quarantine the archive; the install
  path needs documenting until this lands.
- **macOS only.** No Windows VST3 in CI yet.

### 4.3 Record what you built

Put this in the release notes — a bug report against "AMYplug 0.x" is unactionable without it:

| | |
|---|---|
| Git tag / commit | |
| JUCE version | ⚠️ CI pins **8.0.13**; local dev uses **8.0.14**. State which one shipped. |
| AMY submodule commit | |
| macOS + Xcode version | |
| pluginval strictness | |

---

## 5. Sign-off

| § | Area | Result | Who / when | Notes |
|---|---|---|---|---|
| 1.1 | Build | | | |
| 1.2 | Unit tests (71) | | | |
| 1.3 | pluginval ×4 @ 10 | | | |
| 1.4 | auval (both) | | | |
| 2 | Stress + sanitizers | | | |
| 3.1 | **No hanging notes** | | | |
| 3.2 | Recall | | | |
| 3.3 | Modal chrome | | | |
| 3.3b | Hover help | | | |
| 3.4 | Editor size | | | |
| 3.5 | Hardware mode | | | |
| 3.6 | Multi-instance | | | |
| 3.7 | AMYplugFX | | | |
| 3.8 | Presets, banks, FX switches | | | |
| 3.9 | First run | | | |
| 4.1 | Package | | | |

**Release blocked by any failure in § 1, § 3.1 or § 3.2.** Everything else is a judgement
call the release notes have to be honest about.
