# AMYplug editor UI

The editor is a fixed-size **1280×800** `juce::AudioProcessorEditor` implementing the
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
amyplug_snapshot <out.png> [tabIndex]   # 0 Juno · 1-4 DX7 · 5 FX-MASTER · 6 AMYboard
```

It constructs the processor + editor headlessly and paints into a `juce::Image` — no
window, so it's immune to the compositor/Spaces quirks that can stop the Standalone
window from appearing in a headless/remote session. Use it to iterate on the UI.
