# AMYplug editor UI

The editor is a **1280×830** `juce::AudioProcessorEditor` (resizable 60-150% about that
design surface — see below) implementing the
"editor v2" visual redesign (source spec in `visual/design_handoff_amyplug/`). It is a
pure view over the existing APVTS parameters — no parameter ids, ranges, or the
processor/streaming layer changed with the reskin.

## Visual identity — `AmyLookAndFeel`

`src/gui/AmyLookAndFeel.{h,cpp}` is a `juce::LookAndFeel_V4` subclass set on the editor
(and inherited by every child). Everything is vector-drawn:

- **Rotary knob** (`drawRotarySlider`) — a 270° arc with a 90° gap at the bottom
  (`225°→495°` in JUCE angles), an inset radial-gradient face, a glowing accent pointer,
  and a hub. The fill colour is the slider's `rotarySliderFillColourId`, so each section
  colour-codes its knobs.
- **LCD readout** — the slider's value box, styled in `drawLabel` (detected by the
  label's parent being a `Slider`): dark well + the bundled **DSEG7** segmented font.
  Values are tidied for the narrow well **display-only** (the parameter value is
  untouched): integers stay integer, `|v| ≥ 100` (Hz-range) drops decimals, else two
  decimals.
- **Combo boxes, buttons (+ PANIC), tabs** — all custom-painted from the token palette.
- **Section header bars** are painted by `ControlPanel`, not the LAF: a full-alpha accent
  bar with the **title in the dark background colour** (`headerTextOn` → `panel`) for a
  readable "cut-out" look on every accent.

Design tokens (colours, the accent-per-section mapping) live in `src/gui/AmyColours.h`.

## Editor size — picker, corner grip, custom sizes

The editor is a **1280x830 design surface** scaled as a whole. Everything lives inside
`ScaledContent` (`src/gui/ScaledContent.h`), which carries a single `AffineTransform`; all
layout code works in design coordinates and knows nothing about scaling. Reference:
`Code Repo/JUCE-UI-LnF__13-Editor-Size-Picker.md` (and `__02` for the content pattern).

- **The size lives on the processor**, not the editor: a host destroys and recreates the
  editor every time its window closes, so anything the editor remembers about itself dies
  with it. `AmyPlugProcessor::uiScalePercent()` clamps in the *setter* (60-150), and rides
  in `getStateInformation` as a `uiScale` property on the existing `AMYplugState` root.
  Defaults to 100, so a pre-picker session still opens at the design size.
- **Deliberately not a parameter.** It is a view preference; as a parameter it would appear
  in the host's automation lane, be hit by "randomise all parameters", and ride in every
  preset.
- **The menu is presets, not a cage.** `setResizable(true, true)` gives a bottom-right grip,
  and the window is freely draggable with a fixed aspect ratio. Menu choice, corner drag and
  a host-driven resize all land in `resized()`, which writes the percentage back — so all
  three are remembered, and the button is a live *readout* (drag to an odd size and it shows
  `113%`, with no menu item ticked, which is correct: none of the presets *is* that size).
- ⚠️ **Read the stored size BEFORE `setResizeLimits`.** It clamps the editor's *current*
  bounds — `0x0` during construction — to the minimum, firing `resized()`, which writes that
  back. Read it afterwards and the plugin opens at 60% forever while every menu item still
  works. `tests/EditorSizeTests.cpp` asserts the width the editor *opened* at; reintroducing
  the trap makes it fail with `60 == 125`.
- **`drawCornerResizer` is overridden.** The JUCE default is bright white/grey hatching,
  which reads as a scratch on a dark faceplate. Ours is dim, brightens on hover and goes
  accent while dragging — the same escalation as the rest of the panel.
- **Popup menus parent to `content`, not to the editor.** `PopupMenu::MenuWindow` skips its
  own auto-scale path entirely once `options.getParentComponent()` is set, so a menu parented
  to the unscaled editor would come up at 100% over a 150% panel.

## Presets — identity, dirty mark, banks, the PRESETS tab

Reference: `Code Repo/JUCE-UI-LnF__14-Preset-System.md`.

**The loaded preset is state on the processor, not the editor** (`PresetRef`: a factory
index, or a user bank + name). A host destroys the editor on every window close, so the
editor only *mirrors* it. It rides in the session on the `AMYplugState` root and is
re-resolved by **name** on restore — never re-loaded from disk, because the session's own
values are what the user last heard and a file of the same name on this machine may not be
the same file. An orphan shows its name. Before this existed, a reload brought the sound
back and left the field blank.

**Dirty mark.** `presetDirty` is set from wherever a parameter moves — including the audio
thread under host automation — so the watcher's body is atomic stores only, and the editor
polls it. Excluded: `mode` (where the sound comes from, not what it is), the bend range,
and `patchA` (that one *is* the identity; moving it means "load this factory patch").
⚠️ The watcher listens to the **parameters directly** and compares values itself: the APVTS
adapter fires on the *first* write to a parameter even when the value is unchanged, so a
host pushing current values after construction would mark every patch modified. Save and
load both end clean. Full inclusion: the FX on/off switches dirty it like any knob.

**Banks are subfolders** of the preset folder, one level. `PatchLibrary::banks()` lists the
*folder* tree, so a bank made in the Finder shows while still empty. Every operation
resolves a preset by its display name (the file name is a sanitised form of it), refuses
to clobber, and deletes to the **Trash**; an emptied bank is pruned, ignoring the
`.DS_Store` the Finder leaves behind.

**The PRESETS tab** (`src/gui/PresetsPage`) is tab 0: tree · list · info + actions. Click a
row to load. Right-click a user bank to rename or delete it. **The header** keeps one field
— `‹ › [ bank · name * ]` — whose menu is FACTORY and USER banks as submenus with the loaded
preset *ticked*, not disabled; ‹ › walk one flat catalogue (`PresetCatalog`) and wrap.
**BANK is an editable ComboBox** wherever a prompt needs one — Save As, Move to Bank, DX7
import (pre-filled with the cartridge's name, the choice entirely the user's).

**FX switches** (`reverb_on` … `clip_on`) are parameters with a `PowerButton` in each FX
card's bar; off streams the effect's neutral value while the knobs keep their settings.
The clipper is drive 0, not a bypass — it doubles as the 0 dBFS ceiling.

## Modal chrome — popup menus and dialogs

A `PopupMenu` and an `AlertWindow` are **not children of the editor**, so neither inherits
any of the above. Both need explicit wiring, and both are easy to leave stock without
noticing — the background reference is
`Code Repo/JUCE-UI-LnF__15-PopupMenu-in-a-Plugin-Editor.md`.

**Popup menus** (every `ComboBox` dropdown, including the 128-entry patch browser):

- `getOptionsForComboBoxPopupMenu` adds **`withParentComponent(editor)`**. A menu defaults
  to its own *desktop window*: it does not move, hide or die with the plugin window, so
  dragging the window in a DAW leaves the menu floating where it opened. `withTargetComponent`
  (JUCE's own default here) is what makes clicking the combo again *close* the menu.
- Setting the `PopupMenu::*` colour ids is **not** styling — it leaves stock fonts, item
  heights and ticks. `drawPopupMenuBackground` / `Item` / `SectionHeader`,
  `getIdealPopupMenuItemSize`, `getPopupMenuBorderSize` and `drawPopupMenuUpDownArrow` are
  all overridden. `PopupMenu::headerTextColourId` is easy to miss: bank headings otherwise
  draw in a default that happens to be legible.
- The menu window is **opaque** (JUCE makes it so whenever `backgroundColourId` is), so
  `drawPopupMenuBackground` lays the shell colour down first and paints the rounded card on
  top — the corners then read as faceplate rather than as undrawn pixels.
- A *parented* menu gets a stock resizable frame painted over its border, so
  `drawResizableFrame` is a deliberate no-op. Safe because the editor is fixed-size.
- Both editor destructors call `juce::PopupMenu::dismissAllActiveMenus()`. A popup is owned
  by the `ModalComponentManager` and outlives the editor; it does not crash (a LookAndFeel is
  held weakly) — it just repaints in stock grey, which is why it reads as a styling bug.

**Dialogs** go through one pair of helpers, `AmyPlugEditor::beginDialog` / `showDialog`, so a
prompt added later cannot forget any of it:

- `setLookAndFeel` is applied **before** any field or button. `AlertWindow::lookAndFeelChanged`
  re-runs `updateLayout`, and the title/message `TextLayout` bakes in whichever fonts were
  current when it was built — set it late and the text stays stock while the buttons don't.
- The window is then parented into the editor and re-centred (it is laid out while it still
  has a desktop peer, so its bounds are in *screen* coordinates until then).
- Lifetime: one `std::unique_ptr` member, moved out at the top of the modal callback, guarded
  by a `SafePointer` to the editor — closing the plugin dismisses the dialog and fires the
  callback asynchronously, after the editor is gone.
- Buttons carry **explicit return-value ids**; every other way a prompt can end (escape, the
  editor closing) yields `0`. `styleDialogChrome` gives the confirming button (id `1`) the
  accent fill with a dark cut-out label, and text fields real indents — neither is reachable
  from a LookAndFeel.
- `AlertWindow::showMessageBoxAsync` is **not** used: that static goes through the *default*
  LookAndFeel, so the box would come up stock on our faceplate.
- `drawAlertBox` replaces `LookAndFeel_V4`'s 80px glyph with a compact badge, drawn into the
  fixed 80px column `AlertWindow::updateLayout` reserves for any non-`NoIcon` type.

⚠️ **None of the *behaviour* here is verifiable headlessly** — parenting, click-to-close and
dismissal all need a real modal state and a window to drag. `amyplug_snapshot … chrome`
verifies the **drawing** only; confirm the rest in a host.

## Fonts — bundled, `DeletedAtShutdown`

`src/gui/AmyFonts.{h,cpp}` loads five OFL-licensed typefaces bundled as `BinaryData`
(see `assets/fonts/` + its `OFL-*.txt` / `DSEG-LICENSE.txt`, wired via
`juce_add_binary_data(AmyPlugAssets …)`):

- Barlow Semi Condensed 800 (logo) · 700 (section headers / page titles)
- Barlow Condensed 600 (control labels) · IBM Plex Mono (captions) · DSEG7 Classic (LCD)

The typeface cache is a **`juce::DeletedAtShutdown` singleton** so the CoreText faces are
released during JUCE's GUI shutdown — a plain function-local `static` would destruct at
`__cxa_finalize`, after CoreText is gone, and abort on quit.

## Layout

`ControlPanel` stacks titled **section cards**; any height above the content minimum is
distributed **evenly across the section bodies**, and each control sits in a fixed-height
band (`setControlHeight`) centred vertically so knobs keep their size with even margin.
Gaps sit **between** sections only (no trailing gap), so a one-section panel ends flush
like a column's last section. `preferredHeight()` returns *content* height (the
`reduced(4)` margin is applied separately in `sectionBoxes()`).

Tabs (fixed size, no scrolling needed):

| Tab | Component | Content |
|---|---|---|
| JUNO | `JunoPage` | "JUNO" title + VOICE card (top row) over two synth columns (OSC A/C/VCF/LFO · OSC B/D/VCF-ENV/AMP-ENV). One shared section height across all five stacked sections. |
| DX7 1 | `Dx7TabComponent` | Algorithm diagram · Algorithm+Feedback card · "DX7 / OPERATOR TUNING" watermark (sized to an OP cell) · OP 1–6 grid |
| DX7 2 / 3 | `TabPage` | "DX7 ENVELOPES" title · per-op envelope graph + R1–4/L1–4 (OP 1–3 / 4–6) |
| DX7 4 | `TabPage` | "DX7 GLOBAL" title · GLOBAL PITCH · PITCH EG · LFO · LFO→OP |
| FX-MASTER | `TabPage` | "FX-MASTER" title · EQ·CHORUS / ECHO·REVERB / BIT CRUSHER·DISTORTION |
| AMYboard | `HardwarePanel` | MIDI-out select + Connect/Disconnect/Send (buttons span the selector width) |

`TabPage` draws a centered two-tone page title (grey word + accent word + subtitle) above
a content component that fills the rest.

## Headless snapshot tool (UI dev)

`tools/Snapshot.cpp` builds an optional console app `amyplug_snapshot` (guard
`-DAMYPLUG_BUILD_SNAPSHOT=ON`, off by default) that renders any tab straight to a PNG:

```
amyplug_snapshot <out.png> [tabIndex] [algo] [scale%]   # 0 Presets · 1 Juno · 2-5 DX7 · 6 FX · 7 AMYboard
amyplug_snapshot <out.png> chrome                       # popup menu states + both dialogs
```

`scale%` sets the stored editor size *before* the editor is built, which is the path the
size-picker regression lives in — `amyplug_snapshot out.png 0 0 75` renders the whole UI at
75%. `chrome` renders the modal chrome, which can never appear in a tab snapshot: it drives the
same LookAndFeel hooks `PopupMenu::MenuWindow` calls, and paints two real `AlertWindow`s
offscreen. It proves the drawing, not the modal behaviour.

The FX plugin has its own target (both plugins define `createPluginFilter()`, so their
sources can't share one binary):

```
amyplugfx_snapshot <out.png>            # the AMYplugFX 4x2 rack
```

Both construct the processor + editor headlessly and paint into a `juce::Image` — no
window, so they're immune to the compositor/Spaces quirks that can stop the Standalone
window from appearing in a headless/remote session, and they call
`Process::setDockIconVisible(false)` so they never flash a Dock icon or steal focus.
Use them to iterate on the UI; the README screenshots in `docs/screenshots/` are
generated this way.
