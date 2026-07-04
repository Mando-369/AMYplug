# Yamaha DX7 — complete parameter list of a "sound"

A DX7 **sound** = one **voice** (patch). A voice is defined by **155 parameters**
(the single-voice/VCED SysEx payload). Broken down:

- **6 operators × 21 parameters = 126** operator parameters
- **19** global / "common" parameters (pitch EG, algorithm, feedback, LFO, transpose)
- **10** voice-name characters
- = **155** stored parameters.

Plus one edit-only value — **Operator On/Off** (param 155) — that the DX7 transmits
while editing but does **not** store in the voice (so it's 156 in the edit buffer,
155 on disk/cartridge). "Function" (performance) parameters like mono/poly and
controller assignments are global to the instrument and are **not** part of the
stored sound — they're listed at the end for completeness.

Everything below is verbatim from the DX7 MIDI Data Format sheet (see Sources).

---

## Per-operator parameters (×6 — OP1…OP6, identical set)

The DX7 has 6 sine-wave operators. Each has these **21** parameters:

| # | Parameter | Range | Notes |
|---|-----------|-------|-------|
| 1 | EG Rate 1 | 0–99 | Envelope: rate of segment 1 (attack) |
| 2 | EG Rate 2 | 0–99 | rate of segment 2 |
| 3 | EG Rate 3 | 0–99 | rate of segment 3 |
| 4 | EG Rate 4 | 0–99 | rate of segment 4 (release) |
| 5 | EG Level 1 | 0–99 | level reached after rate 1 |
| 6 | EG Level 2 | 0–99 | level after rate 2 |
| 7 | EG Level 3 | 0–99 | level after rate 3 (sustain) |
| 8 | EG Level 4 | 0–99 | final level after rate 4 (usually 0) |
| 9 | Keyboard Level Scaling — Break Point | 0–99 | note where L/R scaling splits; C3 = 39 ($27) |
| 10 | Keyboard Level Scaling — Left Depth | 0–99 | scaling amount below break point |
| 11 | Keyboard Level Scaling — Right Depth | 0–99 | scaling amount above break point |
| 12 | Keyboard Level Scaling — Left Curve | 0–3 | 0 = −LIN, 1 = −EXP, 2 = +EXP, 3 = +LIN |
| 13 | Keyboard Level Scaling — Right Curve | 0–3 | same encoding |
| 14 | Keyboard Rate Scaling | 0–7 | speeds up EG for higher notes |
| 15 | Amplitude Modulation Sensitivity (AMS) | 0–3 | how much LFO AMD affects this op |
| 16 | Key Velocity Sensitivity | 0–7 | velocity → level |
| 17 | Operator Output Level | 0–99 | op volume (carrier loudness / modulator index) |
| 18 | Oscillator Mode | 0–1 | 0 = Ratio (tracks pitch), 1 = Fixed frequency |
| 19 | Oscillator Frequency Coarse | 0–31 | ratio integer / fixed decade |
| 20 | Oscillator Frequency Fine | 0–99 | fractional frequency |
| 21 | Oscillator Detune | 0–14 | 7 = 0 detune; 0 = −7, 14 = +7 |

> Note: whether an operator is a **carrier** (heard) or a **modulator** (shapes another
> op's pitch) is not a per-op flag — it's determined by the **Algorithm** (below).

---

## Global / common parameters (one set per voice)

| # | Parameter | Range | Notes |
|---|-----------|-------|-------|
| 1 | Pitch EG Rate 1 | 0–99 | global pitch envelope (affects all ops) |
| 2 | Pitch EG Rate 2 | 0–99 | |
| 3 | Pitch EG Rate 3 | 0–99 | |
| 4 | Pitch EG Rate 4 | 0–99 | |
| 5 | Pitch EG Level 1 | 0–99 | |
| 6 | Pitch EG Level 2 | 0–99 | |
| 7 | Pitch EG Level 3 | 0–99 | |
| 8 | Pitch EG Level 4 | 0–99 | |
| 9 | Algorithm | 0–31 | the 32 operator routings (displayed as 1–32) |
| 10 | Feedback | 0–7 | feedback amount on the algorithm's feedback op |
| 11 | Oscillator Key Sync | 0–1 | reset op phase on note-on |
| 12 | LFO Speed | 0–99 | |
| 13 | LFO Delay | 0–99 | fade-in time before LFO acts |
| 14 | LFO Pitch Modulation Depth (PMD) | 0–99 | vibrato depth |
| 15 | LFO Amplitude Modulation Depth (AMD) | 0–99 | tremolo depth |
| 16 | LFO Key Sync | 0–1 | reset LFO phase on note-on |
| 17 | LFO Waveform | 0–5 | 0 Triangle, 1 Saw Down, 2 Saw Up, 3 Square, 4 Sine, 5 Sample&Hold |
| 18 | LFO Pitch Modulation Sensitivity (PMS) | 0–7 | mod-wheel/PMD → pitch amount |
| 19 | Transpose | 0–48 | semitones, center 24 = C3 (±2 octaves) |
| — | Voice Name | 10 × ASCII | 10-character patch name |

**Edit-buffer only (not stored in the voice):**

| Parameter | Range | Notes |
|-----------|-------|-------|
| Operator On/Off | 6 bits | bit5 = OP1 … bit0 = OP6; sent only while editing |

---

## Function / performance parameters (instrument-global, NOT part of the sound)

These are transmitted with SysEx parameter-group `g=2`. They affect how you *play*
the DX7, not the stored voice, so a patch file doesn't carry them:

| Parameter | Range | Notes |
|-----------|-------|-------|
| Mono/Poly Mode | 0–1 | 0 = Poly |
| Pitch Bend Range | 0–12 | semitones |
| Pitch Bend Step | 0–12 | quantized bend steps |
| Portamento Mode | 0–1 | 0 = Retain, 1 = Follow (mono glissando behaviour) |
| Portamento Gliss | 0–1 | |
| Portamento Time | 0–99 | |
| Mod Wheel Range | 0–99 | |
| Mod Wheel Assign | 0–7 | bit0 pitch, bit1 amp, bit2 EG bias |
| Foot Control Range | 0–99 | |
| Foot Control Assign | 0–7 | same bit encoding |
| Breath Control Range | 0–99 | |
| Breath Control Assign | 0–7 | same bit encoding |
| Aftertouch Range | 0–99 | |
| Aftertouch Assign | 0–7 | same bit encoding |

---

## Count summary

| Group | Count |
|-------|-------|
| Operators: 6 × 21 | 126 |
| Pitch EG (4 rate + 4 level) | 8 |
| Algorithm, Feedback, Osc Key Sync | 3 |
| LFO (speed, delay, PMD, AMD, key sync, wave, PMS) | 7 |
| Transpose | 1 |
| Voice name | 10 |
| **Stored total** | **155** |
| Operator On/Off (edit buffer only) | (+1 = 156) |
| Function params (global, not stored) | 14 |

---

## How this maps to AMY (for the AMYplug DX7 tab)

AMY implements FM as the `ALGO` wave type. The DX7 → AMY correspondences:

- **Algorithm** → AMY `algorithm` (`o`, 1–32) on the `ALGO` osc; **operators** are
  `SINE` oscs wired via `algo_source` (`O`).
- **Op frequency** → `ratio` (`I`) when Osc Mode = Ratio, or explicit `freq` when Fixed;
  **Detune / Fine** fold into the ratio/freq.
- **Op Output Level** → op amplitude (`amp` CtrlCoefs).
- **Operator EG (rates/levels)** → AMY per-osc breakpoints `bp0` (`A`) — note AMY uses
  time-deltas + target values, so the DX7 rate/level pairs must be converted, not copied.
- **Feedback** → AMY `feedback` (`b`) on the ALGO osc.
- **LFO (speed/PMD/AMD/wave)** → a modulator osc + `mod_source`, or pitch/amp CtrlCoefs.
- **Keyboard level/rate scaling** → CtrlCoefs (`note`) and rate scaling.

AMY ships a reference converter, `amy/fm.py`, that turns any DX7 `.syx` patch into an
AMY patch. `src/state/Dx7Import.cpp` mirrors its logic. The 155-parameter layout above
is exactly what you parse from a `.syx` (packed 128-byte-per-voice bulk format; see the
Dexed spec for the bit-packing).

### Two deliberate deviations from `fm.py` (more DX7-accurate)

`fm.py` is a convenient reference, not a faithful DX7. Two places where mirroring it
sounds wrong, so AMYplug corrects them (see `src/state/Dx7Osc.h`):

- **Detune magnitude (param 21).** `fm.py` maps detune as `(detune-7)/8` *percent* of the
  ratio — about **±15 cents** at the extremes. The real DX7 detune is only **±2 cents**
  (DX7II manual; [Dexed issue #88](https://github.com/asb2m10/dexed/issues/88)). Mirroring
  `fm.py` turns micro-detuned patches (e.g. DX7 BRASS 2, which uses the full detune range
  across four equal-level carriers) into a fast, deep amplitude *beating* the hardware
  never had. `coarseFineRatio` applies the real ±2-cent curve as a proportional
  `2^(cents/1200)` factor; `splitFineDetune` (factory-patch decode) attributes sub-percent
  ratio offsets to *detune*, not a whole *fine* step, so the corrected curve actually
  applies. Result: a slow, lush chorus (~0.4 Hz) instead of a ~2.5 Hz pump.

- **Key Velocity Sensitivity (param 16).** `fm.py` reads `keyvelsens` and **discards it** —
  AMY's DX7 patches have no per-operator velocity. And AMY can't add it via the amp `vel`
  CtrlCoef: in the `ALGO` engine every operator is an `algo_source` that never receives the
  note velocity (`amy.c` skips `SYNTH_IS_ALGO_SOURCE` for velocity), so a `vel` coef there
  drives the operator's `amp_combine` to **zero** — the op vanishes even at full velocity.
  Instead AMYplug does what the DX7 actually does: **velocity scales the operator's output
  level** at note-on (`velLevelScale` + `AmyPlugProcessor::streamFmParams`, driven by the
  captured note-on velocity). On a carrier that's loudness; on a **modulator** it's FM index
  → **brightness** (harder = brighter), the core FM velocity feel. `velSens 0` is a no-op,
  so factory/default patches are unchanged. (Carrier *loudness*-with-velocity also comes for
  free per-voice via the ALGO controller's `a1,0,1,..` coef, which `render_algo` uses to
  scale the carriers.) **Limitation:** the operator level is broadcast across a synth's
  voices, so velocity→brightness is per-note-on — monophonically exact; polyphonically it
  follows the most recent hit. Carrier loudness stays per-voice.

---

## Sources
- [DX7 SysEx / MIDI Data Format (Dexed docs, `sysex-format.txt`)](https://raw.githubusercontent.com/asb2m10/dexed/master/Documentation/sysex-format.txt) — the authoritative parameter numbering, ranges, and packed bulk format.
- [Dave Benson's DX7 SysEx format mirror](https://homepages.abdn.ac.uk/d.j.benson/pages/dx7/sysex-format.txt)
- [Yamaha Black Boxes — DX7 patches & SysEx reference](https://yamahablackboxes.com/collection/yamaha-dx7-synthesizer/patches/)
- Cross-checked against the DX7 MIDI Data Format Sheet (compiled by E. Macpherson; Steve DeFuria, _Keyboard_, Jan 1987).
