# Draft issue for shorepine/amy — the 3-band EQ is a parallel sum, so one band's gain notches another

**Status: DRAFT. Not submitted.** Measured against AMY 1.2.16 (`dde55aac`) on macOS, arm64,
`AMY_SAMPLE_RATE` 48000, driven from a host application that renders AMY in library mode.

## Summary

`x<low,mid,high>` does not behave as an EQ. Boosting a single band by **+1 dB** puts a
**−7.7 dB notch** an octave and a half above it. At +6 dB the notch is **−15.1 dB**. The
bands are not independent, and the response is not monotonic in the control.

## Measurement

White noise through a single voice, filter wide open, all other effects off. Averaged
magnitude at nine frequencies, in dB **relative to the same signal with the EQ at 0/0/0**:

| setting | 60 | 150 | 400 | 800 | 1500 | 2500 | 4000 | 7000 | 12000 |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| **low +1** | +0.8 | +0.8 | +0.1 | **−5.9** | **−7.7** | +0.1 | −0.4 | +0.4 | +0.6 |
| **low +6** | +5.9 | +5.9 | +5.3 | +0.9 | **−15.1** | −0.6 | −0.5 | +0.4 | +0.6 |
| **mid +6** | −0.1 | −0.1 | −1.6 | **−11.3** | +0.9 | +6.1 | +4.4 | +2.8 | +1.4 |
| **high +6** | −0.2 | −0.0 | −1.0 | **−6.8** | **−6.4** | +1.1 | +1.8 | +4.9 | +6.1 |
| **all +6** | +5.8 | +5.8 | +4.9 | −1.4 | −1.3 | +6.1 | +5.5 | +6.3 | +6.6 |

Note the last row: when all three move **together** the response is +6 dB and essentially
flat. The design is self-consistent for a single tilt control and breaks as soon as the
three gains differ.

## Cause

`parametric_eq_process` (`src/filters.c`) runs three biquads in **parallel** and sums them:

```c
cblock[i] = y00 - y10 + y20;      // filters.c:575   LPF - BPF + HPF
```

built from (`filters.c:496-498`)

```c
dsps_biquad_gen_lpf_f32(coeffs[0], EQ_CENTER_LOW  /SR, 0.707);   //  800 Hz
dsps_biquad_gen_bpf_f32(coeffs[1], EQ_CENTER_MED  /SR, 1.000);   // 2500 Hz
dsps_biquad_gen_hpf_f32(coeffs[2], EQ_CENTER_HIGH /SR, 0.707);   // 7000 Hz
```

with the user gain folded into each stage's forward gain (`filters.c:543-549`).

LPF − BPF + HPF is a **complementary decomposition**: the three paths reconstruct the input
only because their phase responses are arranged to cancel correctly *at equal gain*. Scaling
one path alone destroys that relationship, and the residual is a deep notch wherever two
paths were relying on each other — here around 800–1500 Hz, between the low and mid centres.
Magnitudes do not add; complex responses do.

A second, smaller issue: `amy.c:1921` skips EQ processing entirely when all three gains are
exactly 1.0, so the **first** dB a user dials in is not a small change — it switches the
signal from an untouched bypass to the full three-path sum. That is the −5.9 dB at 800 Hz in
the "low +1" row.

## Proposed fix — cascade the bands instead of summing them

Replace the parallel sum with three biquads **in series**: a low shelf, a peaking bell, and
a high shelf, each carrying its own gain.

```
in → lowShelf(800 Hz, g_l) → peaking(2500 Hz, Q≈0.9, g_m) → highShelf(7000 Hz, g_h) → out
```

Why this is the right shape here:

- **Magnitudes multiply**, so each control moves only its own region and the response is
  monotonic in the control. No cancellation between bands, because there is one path.
- **Same cost.** Three biquads either way — arguably cheaper, since the current code runs
  three parallel state updates plus a sum per sample.
- **Same public API.** `x<low,mid,high>` in dB, `config_eq(bus, l, m, h)`, the `EQ_L/M/H`
  event fields and `10^(dB/20)` conversion at `amy.c:1369` all stay exactly as they are.
- **Unity falls out for free.** At 0/0/0 every stage is exactly unity, so the existing
  fast-path bypass becomes a pure optimisation rather than a behavioural discontinuity.

Shelving and peaking coefficients are the standard Audio-EQ-Cookbook forms and need the same
`dsps_biquad_gen_*` machinery already in `filters.c` — one new generator each for
`lowShelf`, `peaking` and `highShelf`.

The change is not bit-compatible: patches that set a non-flat EQ will sound different. Given
that the current behaviour puts an unrequested 15 dB notch in the midrange, that seems like
the right trade, but it is the maintainers' call — it could equally ship as a new command or
behind a compile flag.

## Offer

Happy to submit this as a PR against `main` — the biquad generators plus a swap of
`parametric_eq_process` — with the measurement above as a regression test if that is useful.

## How the measurement was taken

Rendered through AMY in library mode (`audio=AMY_AUDIO_IS_NONE`, `midi=AMY_MIDI_IS_NONE`),
one noise oscillator, 8192-sample Hann-windowed frames, 24 frames averaged, single-frequency
DFT at each listed frequency, expressed relative to the 0/0/0 run. The probe is short and can
be included in the PR.
