# Handoff: AMYplug — VST/AU editor UI (JUCE 8 CustomLookAndFeel)

## Overview
AMYplug is a plugin editor for the **AMY** synth engine + AMY hardware board. It is a fixed-size
desktop plugin window with a tabbed body:

`JUNO · DX7 1 · DX7 2 · DX7 3 · DX7 4 · FX-MASTER · AMYBOARD`

- **JUNO** = AMY *Analog* engine (4 oscillators + filter + envelopes + LFO + voice)
- **DX7 1–4** = AMY *FM* engine, one 6-operator DX7 voice split across four pages
- **FX-MASTER** = EQ / Chorus / Echo / Reverb + Bit Crusher + Distortion
- **AMYBOARD** = hardware status + on-screen keyboard

## About the design files
The files in this bundle are **design references authored in HTML** (a streaming component format).
They are the intended look/behavior, **not** production code to ship. The task is to **recreate this UI
in JUCE 8** as a `juce::LookAndFeel_V4` subclass (`AmyLookAndFeel`) plus custom-painted `juce::Component`s,
drawing everything as **vector graphics** (`juce::Graphics` / `juce::Path`). Do not rasterize; all knobs,
faders, arcs, diagrams, and the envelope graphs are procedural vectors.

Open `AMYplug.dc.html` in a browser to see it live. **Toggle the `SPEC` button (top-right)** for an
on-screen spec sheet (tokens, component anatomy with redlines, and a component→LookAndFeel map).

## Fidelity
**High-fidelity.** Colors, sizes, radii, and typography below are final. Match them.

## Canvas & coordinate system  ⚠ important
- Editor size (`AudioProcessorEditor::setSize`): **1280 × 800** logical px (1×).
- The HTML source is authored at **2× (2560 × 1600)** and scaled by 0.5 for crispness.
- **Every px value in the HTML source is 2×.** For JUCE, use **1× = source ÷ 2**.
- All measurements in this README are given in **1× logical px** unless labelled “(src 2×)”.
- Support HiDPI by letting JUCE scale; author all paint code in 1× logical units.

---

## Design tokens

### Colors
| Role | Hex |
|---|---|
| Shell base (window body top) | `#16191E` → gradient to `#101317` |
| Panel / card fill | `#0F1318` |
| Panel raised (buttons) | `#1A1F25` |
| Groove / inset (LCD, dropdown wells) | `#080B0E` |
| Hairline border | `#232C35` |
| Text primary | `#E7ECF2` |
| Text dim (labels) | `#8A95A2` |
| Text faint (carets, captions) | `#5A636E` |
| Engine cyan — **DX7 identity** | `#37C2D4` |
| Juno red — oscillators | `#D23B34` |
| Juno blue — envelopes | `#4F74E0` |
| Filter violet | `#8A5CD6` |
| LFO green | `#83CC9C` |
| Amber — master / VOICE / OUT GAIN | `#E8A13C` |
| Status green | `#5FD08A` |
| PANIC red | `#C23B36` (border `#D85049`) |
| Knob arc unfilled track | `#29323D` |

Accent usage is **functional color-coding**, per section (see layouts). Chrome (title bar, logo,
tab bar) is deliberately **neutral** — no engine color.

### Typography (Google Fonts; pick nearest bundled equivalents in JUCE)
| Use | Family / weight | Size (1×) | Tracking |
|---|---|---|---|
| Logo `AMYplug` | Barlow Semi Condensed 800 | 33 | +0.4% |
| Section header bars | Barlow Semi Condensed 700 | ~11–14 | +0.16em, UPPERCASE |
| Control labels | Barlow Condensed 600 | ~10 (see note) | +0.09em, UPPERCASE |
| Numeric readouts | IBM Plex Mono 400 | ~9–10 | — |
| LCD numerics (optional) | DSEG7 Classic | — | segmented |

> Note on label sizes: the source was intentionally enlarged for legibility. Current source label size
> is **20px (2×) → 10px (1×)**; readouts **19px (2×) → ~9.5px (1×)**. Treat these as the floor — don’t go smaller.

---

## Components → LookAndFeel methods

All sizes 1× (source 2× in parentheses).

### Rotary knob → `drawRotarySlider()`
- Outer diameter **⌀36** (src 72). Sweep **270°**, start angle **225°** (i.e. `rotaryStartAngle = 225°`,
  `rotaryEndAngle = 225° + 270°`). Pointer at `−135° + value·270°`.
- **Arc ring** in the outer ~4.5px (src 9) band: filled from start to `value` in the section accent color,
  remaining arc in track `#29323D`, and a **90° gap at the bottom** (270°→360° empty).
- **Face**: inset ~4.5px circle, radial gradient `#2C333D`→`#141A20`, 1px border `#0C1015`, subtle inner
  top highlight + bottom inner shadow.
- **Pointer**: ~1.5×12 (src 3×25) rounded bar, accent color, soft outer glow (`accent` @ ~66% alpha).
- **Hub**: ~3.5px (src 7) dark center dot.
- Label above (dim, uppercase), LCD readout below.

### LCD readout → custom `Component` (or `Label` LAF)
- Fill `#080B0E`, 1px border `#212A33`, radius ~1.5px, mono text.
- Text color follows section accent for values; use `#A9C2CF` for the neutral cyan default.

### ComboBox (Wave / Type / Mode / LFO→OP) → `drawComboBox()` + `positionComboBoxText()`
- Height **24** (src 48), fill `#0A0D11`, 1px border `#2A333C`, radius ~3px, inset shadow.
- Value in Barlow Semi Condensed; `▾` caret in `#5A636E` at right.

### Section header bar → custom paint (not a stock control)
- Full-width filled bar, section accent background, **centered** UPPERCASE tracked title.
- Text is white on strong colors; on light accents (LFO green, amber VOICE) use a dark text
  (`#0F2C1C` on green, `#231605` on amber).

### Tab bar → `TabbedButtonBar` LAF (`drawTabButton`)
- Radius top corners only. Inactive: bg `#0E1116`, text `#7B8794`. Active: bg `#14181D`,
  text `#F2F5F8`, **neutral grey top indicator `#9AA5B2`** (2px inset), lifts +1px.

### Buttons (To Editor, Import DX7…, Save…, Delete, ‹ ›) → `drawButtonBackground()`
- Height 24 (src 48), fill `#1A1F25`, 1px `#2A333C`, radius ~4px, Barlow Condensed uppercase.
- **PANIC**: fill `#C23B36`, border `#D85049`, white, soft red glow.

### Envelope graph → custom `Component`
- Dark inset panel (`#0A0F14`, 1px `#232C35`, inner shadow). Panel **190×100** (src 380×200).
- Draw a `Path` polyline of the DX7 8-point rate/level envelope, stroke = engine cyan `#37C2D4`,
  ~1.5px (src 3), round joins/caps.
- Point model (src 380×200, p=16): start at bottom-left `(p, h−p)`, then to L1, L2, L3 (sustain,
  held across two x), then L4 at right edge. x’s: `p, p+44, p+120, p+222, p+274, w−p`.
  y maps level 0–99 → bottom→top inside padding. Pitch EG default is a **flat centered line**.

### Algorithm diagram (DX7 1) → custom paint
- Operator boxes **30×26** (src 60×52), radius ~3px. Non-carrier: fill `#1A222A`, 1px `#38434E`,
  text `#C3CCD6`. **Carriers** (ops feeding output): fill `#0D2A30`, 2px cyan border, cyan glow, cyan number.
- Connectors: 1px `#3A4550` lines between stacked ops; output rail + “output” label in cyan.
- Feedback loop on the top op drawn in amber `#E8A13C` (small bracket + `FB` chip).
- The shown patch is **Algorithm 1**: right column 6→5→4→3, left 2→1, carriers **1 & 3**.

---

## Layout — window chrome (all tabs)

1. **Title bar** (h 26, src 52): traffic-light dots + `Options` (left), `AMYPLUG` centered, `SPEC` toggle
   + window min/close (right). Neutral.
2. **Header row** (`display:flex; align-items:flex-start`):
   - **Brand**: `AMYplug` logo (”plug” in neutral grey `#9AA5B2`) + subtitle “AMY for your DAW · editor v2”.
   - **Patch browser** (fixed block, 410 wide src 820): two rows —
     `PATCH [dropdown 220] [‹][›] … [Import DX7…]` and `USER [dropdown 220] [Save…][Delete] … [To Editor]`.
     The trailing buttons are right-aligned so the block forms a clean rectangle.
   - **OUT GAIN** rotary (amber) with a vertical divider, immediately right of the patch block.
   - **Right cluster**: status `● SOFTWARE  ● {ENGINE}` on top; below it `To Editor`? no — actions are
     `ENGINE [Analog/FM ▾]` + `PANIC`.
   - **ENGINE badge is tab-driven**: shows **Analog** on the JUNO tab, **FM** on DX7 tabs.
3. **Tab bar**, then the **panel area** (rounded bottom, `#14181D`, 1px border).

---

## Layout — JUNO tab (AMY Analog engine)
Two equal columns of section cards (colored header bar + a row of knobs/dropdowns).

**Left column:** OSC A, OSC C, VCF, LFO
**Right column:** OSC B, OSC D, VCF ENV, AMP ENV, VOICE

| Section | Accent | Controls (left→right) |
|---|---|---|
| OSC A/B/C/D | red `#D23B34` | Wave▾, Freq, Coarse, Fine, Duty, Level |
| VCF | violet `#8A5CD6` | Freq, Reso, Kbd, Env, Type▾ |
| VCF ENV | violet `#8A5CD6` | A, D, S, R |
| LFO | green `#83CC9C` | Wave▾, Freq, Pitch, PWM, Filter |
| AMP ENV | blue `#4F74E0` | A, D, S, R |
| VOICE (master) | amber `#E8A13C` | Mode▾, Glide, Unison, Detune |

Example patch values shown: OSC A Saw/440.0/0/0/0.50/0.70; OSC B Pulse/…/Fine −3/…/0.50;
VCF 8.0k/0.70/0.00/0.00/LPF; AMP ENV 0.01/0.10/0.70/0.25; VOICE Poly/0/1/12.0.

## Layout — DX7 1 (algorithm + operator tuning)
- **Top row** (h 200, src 400): Algorithm diagram card (left), Algorithm ▾ + Feedback knob (amber) card,
  and a “DX7 / OPERATOR TUNING” label card (watermark `DX7` in neutral grey `#9AA5B2`, subtitle cyan).
- **Operator grid** 3×2: OP 1–6, each card = header `OP n` + `RATIO ▾` (Mode) and knobs
  **Coarse, Fine, Detune, Level, Vel** (cyan). Example: OP1 Coarse 1 / Fine 0 / Detune 7 / Level 99 / Vel 2.

## Layout — DX7 2 & DX7 3 (operator envelopes)
Centered page title **“DX7 ENVELOPES”** (DX7 grey `#9AA5B2`, ENVELOPES cyan) with subtitle
`OP 1 · OP 2 · OP 3` (DX7 2) / `OP 4 · OP 5 · OP 6` (DX7 3). Below, three stacked operator rows.
- neutral grey header bar `OP n` (`#141A20` fill, `#D4DBE2` text),
- **envelope graph** (190×100) on the left,
- **8 knobs**: R1 R2 R3 R4 L1 L2 L3 L4 (cyan) with numeric readouts.
- Example (OP1): R 99/66/40/55, L 99/80/65/0.

## Layout — DX7 4 (pitch EG + LFO + routing + global)
Centered page title **“DX7 GLOBAL”** (subtitle `Pitch EG · LFO · Routing · Transpose`). Four
**equal-height** sections, in order:
- **GLOBAL PITCH**: Transpose knob (0).
- **PITCH EG**: graph (flat centered) + R1–4/L1–4 (values 99/99/99/99 · 50/50/50/50).
- **LFO**: Speed knob (35), Wave▾ (Triangle), Vibrato, Vib Sens (3), Tremolo.
- **LFO → OP · TREMOLO**: six ▾ selectors OP 1–6 (all “Off”).

## Layout — FX-MASTER
- 2×2 grid: **EQ** (blue) Low/Mid/High · **CHORUS** (amber) Level/Rate/Depth · **ECHO** (violet)
  Level/Time/F.back/Tone · **REVERB** (green) Level/Size/Damp. Each header text + knobs use its accent.
- Bottom row, two cards: **BIT CRUSHER** (cyan) Freq/Bit · **DISTORTION** (red) Drive/Synth Vol.
  Headers carry a colored status dot.

## Layout — AMYBOARD
Hardware-control page (no synth params). Single panel:
- Title “AMYboard – Hardware Control”.
- **MIDI Out** row: label + device dropdown (“IAC Driver Bus 1”) + **Refresh** button (right-aligned).
- **Connect** (enabled) / **Disconnect** (disabled when not connected) buttons.
- **Send Patch to Board** button (disabled until connected).
- Status line: `Mode: SOFTWARE – plugin is sounding` (SOFTWARE in status green).
- Footer caption explaining that connecting routes MIDI to the board (plugin goes silent and pushes
  the current patch); disconnecting returns to the plugin’s own sound.

---

## Interactions & behavior
- Tabs switch the panel body (single active page). Keep panel-area size constant across tabs.
- Knobs: vertical/rotary drag → value; double-click → default; readout reflects value.
- Dropdowns open a menu (Wave, Type, Mode, Algorithm, LFO→OP, Engine, Patch/User).
- `‹ ›` step through patches; `PANIC` = all-notes-off.
- Engine badge follows the active engine (Analog on Juno, FM on DX7).
- No animated transitions required beyond standard control feedback.

## Files in this bundle
- `AMYplug.dc.html` — full editor (all tabs + the SPEC overlay). Primary reference.
- `Knob.dc.html` — the rotary knob component (arc + pointer geometry to mirror in `drawRotarySlider`).
- `Fader.dc.html` — vertical fader component (kept for reference; **not used** in the current UI).
- `support.js` — runtime for the .dc.html format (needed only to view the HTML in a browser).

### screenshots/
Full-frame renders of every tab (1× layout), for quick visual reference:
`01-juno` · `02-dx7-1` · `03-dx7-2` · `04-dx7-3` · `05-dx7-4` · `06-fx-master` · `07-amyboard` · `08-spec`.

Open the HTML and use the **SPEC** toggle as the quick visual reference alongside this document.
