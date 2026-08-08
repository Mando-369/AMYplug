# Roland Juno (106) — parameter list & AMY integration

AMY's analog engine models the **Roland Juno-106**. The reference implementation is
AMY's `amy/juno.py`, which decodes the Juno-106's **18-byte patch** and renders it as
an AMY voice. Everything below is cross-checked against that file, so the "AMY" column
reflects what the engine **actually** does — this is your source of truth for the Juno tab.

- A Juno-106 patch = **16 continuous sliders (0–127)** + **2 bytes of switches** = 18 bytes.
- AMY renders each Juno voice with **6 oscillators** (see layout below); a Juno synth in
  AMY is 6-voice polyphonic like the hardware.
- **Every parameter stored in the Juno-106 patch is implemented in AMY.** A few *front-panel/
  performance* controls that were never part of the stored patch are approximated or global —
  those are called out in the third table.

Legend: ✅ fully modeled · 🟡 approximated / global · ❌ not modeled.

---

## 1. Continuous parameters (the 16 sliders, each 0–127)

| # | Juno control | Section | What it does | AMY | How AMY realizes it |
|---|--------------|---------|--------------|-----|---------------------|
| 1 | LFO Rate | LFO | LFO speed (~0.5–30 Hz) | ✅ | `freq` of the triangle LFO osc; `to_lfo_freq()` curve |
| 2 | LFO Delay | LFO | fade-in before LFO acts (0–~3 s) | ✅ | LFO osc `bp0` delay envelope; `to_lfo_delay()` |
| 3 | DCO LFO | DCO | pitch-mod (vibrato) depth from LFO | ✅ | `freq` mod coefficient on the DCO oscs (`0.03 × value`) |
| 4 | DCO PWM | DCO | pulse-width-modulation depth | ✅ | `duty` coefficients on the pulse osc (const vs LFO slot) |
| 5 | DCO Noise | DCO | white-noise level | ✅ | `amp` of the NOISE osc |
| 6 | VCF Freq | VCF | filter cutoff | ✅ | `filter_freq` const; `to_filter_freq()` (≈13·2^(0.094·v)) |
| 7 | VCF Res | VCF | resonance (to self-oscillation) | ✅ | `resonance`; `to_resonance()` → Q 0.5–16 |
| 8 | VCF Env | VCF | envelope amount into cutoff | ✅ | EG coef on `filter_freq` (×11, signed by polarity) |
| 9 | VCF LFO | VCF | LFO amount into cutoff | ✅ | LFO (`mod`) coef on `filter_freq` (×1.25) |
| 10 | VCF KYBD | VCF | key-follow of cutoff (0–100%) | ✅ | `note` coef on `filter_freq` (with A4↔C4 offset fix) |
| 11 | VCA Level | VCA | overall level | ✅ | `amp` const on the control osc |
| 12 | ENV Attack | ENV | ADSR attack (1.5 ms–3 s) | ✅ | `bp0` breakpoint time; `to_attack_time()` |
| 13 | ENV Decay | ENV | ADSR decay (1.5 ms–12 s) | ✅ | `bp0` breakpoint time; `to_decay_time()` |
| 14 | ENV Sustain | ENV | ADSR sustain level (0–100%) | ✅ | `bp0` sustain value |
| 15 | ENV Release | ENV | ADSR release (1.5 ms–12 s) | ✅ | `bp0` release time; `to_release_time()` |
| 16 | DCO Sub | DCO | sub-oscillator level | ✅ | `amp` of the sub osc (PULSE one octave down) |

> The Juno's ADSR can be routed to the **VCA** (normal) or, in gate mode, the amp becomes a
> gate and the ADSR drives the **VCF** instead — AMY handles this by swapping which envelope
> (eg0/eg1) feeds amp vs filter (see VCA Mode below).

---

## 2. Switch / stepped parameters (the 2 bit-bytes)

| Juno control | Section | Values | AMY | How AMY realizes it |
|--------------|---------|--------|-----|---------------------|
| DCO Range 16′ / 8′ / 4′ | DCO | one of three | ✅ | base frequency ÷2 / ×1 / ×2 |
| Pulse on/off | DCO | 0/1 | ✅ | pulse osc `amp` on/off |
| Saw on/off | DCO | 0/1 | ✅ | saw osc `amp` on/off |
| PWM Mode (LFO / Manual) | DCO | 0/1 | ✅ | swaps constant vs LFO `duty` coefficient |
| VCF Polarity (+/−) | VCF | 0/1 | ✅ | sign of the env→cutoff coefficient |
| VCA Mode (ENV / GATE) | VCA | 0/1 | ✅ | ENV→VCA, or GATE amp + ENV→VCF (eg0/eg1 swap) |
| HPF | HPF | 0–3 | 🟡 | approximated with AMY's global 3-band **EQ** (no dedicated HPF stage) |
| Chorus | Chorus | Off / I / II / I+II | ✅ | AMY global `chorus` effect (level, freq, depth per mode) |

> **HPF caveat:** the real Juno-106 has a dedicated 4-position high-pass filter before the VCF.
> AMY has no per-voice HPF, so `juno.py` fakes the four positions by shelving the 3-band EQ
> (e.g. HPF 0 boosts lows, HPF 3 cuts lows and boosts mids/highs). Close, not identical.

---

## 3. Front-panel / performance controls (NOT in the stored patch)

These exist on the Juno-106 hardware but were never part of the 18-byte patch memory, so they
are global, approximated, or absent in AMY's Juno model:

| Juno control | AMY | Notes |
|--------------|-----|-------|
| Portamento Time | ✅ | `juno.py` has a `portamento` field → AMY `portamento` (ms) on DCO oscs; exponential 10 ms–2.56 s. Not in the 18-byte patch — treat as a plugin-side extra. |
| Portamento switch | 🟡 | implied by portamento time > 0 |
| Bend Sens (DCO) | 🟡 | AMY applies a **global** `pitch_bend`; per-destination bend depth isn't separately modeled |
| Bend Sens (VCF) | ❌ | pitch bend → cutoff routing not modeled in `juno.py` |
| LFO manual trigger / KEY TRIG | ❌ | Juno-106 LFO free-runs; AMY LFO has delay+key-sync but no manual trigger button |
| Master Tune / Transpose | 🟡 | global, handled at the plugin/MIDI level, not per-patch |
| Filter slope (24 dB) | ✅ | AMY uses `FILTER_LPF24` (4-pole, matches the 106); a nonstandard "cheap" flag can switch to 2-pole `FILTER_LPF` |

---

## 4. How AMY builds a Juno voice (osc layout)

`juno.py` allocates 6 oscillators per voice and chains them so one note-on drives them all:

```
osc 0  ctl / VCA + VCF   (SILENT carrier; holds amp env eg0 + filter env eg1, resonance, cutoff)
osc 1  LFO               (TRIANGLE; modulates the others via mod_source)
osc 2  Pulse / PWM       (PULSE; duty modulated by LFO)          ── chained_osc chain ──▶
osc 3  Saw               (SAW_UP)
osc 4  Sub               (PULSE, one octave below)
osc 5  Noise             (NOISE)
```

- `chained_osc` links 0→2→3→4→5 so a single note/velocity event plays the whole stack.
- The VCF (low-pass, 24 dB) runs once on the head of the chain and applies to the summed DCOs.
- `mod_source = osc 1` on each DCO gives the shared LFO for vibrato/PWM/filter-sweep.

This is the exact structure your `Juno` tab should edit — each panel control writes AMY wire
params on the corresponding osc(s).

---

## 5. Hardware-accurate value scaling (for matching slider feel)

`juno.py` maps the 0–127 slider to musical units with these curves (use them so your knobs feel
like the real thing rather than linear):

| Control | Mapping (val = 0..1) |
|---------|----------------------|
| Attack time | `6 + 8·val·127` ms |
| Decay time | `80·2^(0.085·val·127) − 80` ms (time to ~0.05) |
| Release time | `70·2^(0.066·val·127) − 70` ms |
| LFO freq | `0.6·2^(0.04·val·127) − 0.1` Hz |
| LFO delay | `18·2^(0.066·val·127) − 13` ms |
| Resonance | `0.7·2^(4·val)` → Q ≈ 0.7–11+ |
| VCF cutoff | `13·2^(0.0938·val·127)` Hz |

---

## 6. The 18-byte Juno-106 patch (SysEx payload)

Order used by `juno.py` (and the hardware bulk dump):

```
bytes 0–15  (0–127 each), in this order:
  0 lfo_rate   1 lfo_delay   2 dco_lfo   3 dco_pwm   4 dco_noise
  5 vcf_freq   6 vcf_res     7 vcf_env   8 vcf_lfo   9 vcf_kbd
 10 vca_level 11 env_a      12 env_d    13 env_s    14 env_r    15 dco_sub

byte 16  bits: 0=range16′ 1=range8′ 2=range4′ 3=pulse 4=saw
              bits 5–6 = chorus (encoded: ~chorus / I-not-II)
byte 17  bits: 0=pwm_manual 1=vcf_neg 2=vca_gate
              bits 3–4 = HPF (flipped sense)   bit 5 = "cheap filter" (AMY extension)
```

Total = 18 bytes. AMY's `JunoPatch.from_sysex()` reads this and `init_AMY()` emits the wire
messages; `to_sysex()` round-trips it back. Your `PatchModel` for the analog engine should carry
these same 18 fields so recall matches AMY bit-for-bit.

---

## Sources
- [AMY `amy/juno.py`](https://raw.githubusercontent.com/shorepine/amy/main/amy/juno.py) — the authoritative Juno-106 → AMY converter (parameter list, ranges, mappings, curves).
- [AMY synth docs](https://github.com/shorepine/amy/blob/main/docs/synth.md) & [wire API](https://github.com/shorepine/amy/blob/main/docs/api.md) — CtrlCoefs, filters, effects used above.
- [Roland Juno-106 Owner's Manual (PDF)](https://cdn.roland.com/assets/media/pdf/JUNO-106_OM.pdf) & [Technical Specifications](https://support.roland.com/hc/en-us/articles/201966419-Juno-106-Technical-Specifications) — the hardware panel and ranges.
- [Vintage Synth Explorer — Juno-106](https://www.vintagesynth.com/roland/juno106) — panel/parameter overview.
