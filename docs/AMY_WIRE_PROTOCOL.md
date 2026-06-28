# AMY wire-protocol cheat sheet

Distilled from AMY's `docs/api.md` + `docs/synth.md` + `docs/midi.md`. The
authoritative source is the AMY submodule under `third_party/amy/docs/`. Keep
`src/engine/AmyConstants.h` and `AmyWire.h` in sync with this.

## Mental model

`osc` (one oscillator) → `voice` (group of oscs = one note) → `synth` (pool of
voices + a patch, does voice-stealing). MIDI channels 1–16 map to synths 1–16.

A **wire message** is a compact ASCII string, one field per letter, terminated by
`Z`. Example: `v0w0f440l1Z` = osc 0, sine, 440 Hz, note-on velocity 1. You usually
build these via `WireBuilder` rather than by hand.

## Most-used fields

| Code | Param | Meaning |
| ---- | ----- | ------- |
| `v` | osc | which oscillator |
| `i` | synth | synth (voice pool); `iv`=num_voices, `in`=oscs_per_voice |
| `K` | patch | apply patch number to synth/voice |
| `w` | wave | 0 SINE,1 PULSE,2 SAW_DOWN,3 SAW_UP,4 TRIANGLE,5 NOISE,6 KS,7 PCM,8 ALGO(FM),9 PARTIAL,10 BYO_PARTIALS,11 INTERP_PARTIALS,…,19 WAVETABLE,21 OFF |
| `n` | note | MIDI note (fractional allowed) |
| `l` | vel | note-on velocity (>0 = note on, 0 = note off) |
| `f` | freq | frequency ControlCoefficients (default `0,1,0,0,0,0,1`) |
| `F` | filter_freq | filter cutoff ControlCoefficients |
| `G` | filter_type | 0 none,1 LPF,2 BPF,3 HPF,4 LPF24 |
| `R` | resonance | Q 0.5–16 |
| `d` | duty | pulse duty ControlCoefficients |
| `Q` | pan | pan ControlCoefficients (0 L … 1 R) |
| `a` | amp | amplitude ControlCoefficients (default `0,0,1,1`) |
| `A`/`B` | bp0/bp1 | EG0/EG1 breakpoints `t,v,t,v,…` (last pair = release) |
| `T`/`X` | eg0_type/eg1_type | 0 Normal,1 Linear,2 DX7,3 TrueExp |
| `L` | mod_source | osc used as modulator (becomes silent) |
| `o`/`O`/`I` | algorithm/algo_source/ratio | FM (ALGO) setup |
| `b` | feedback | FM feedback / KS / PCM loop |
| `p` | preset / num_partials | PCM/wavetable preset, or partial count |
| `s` | pitch_bend | global, in (fractional) octaves |
| `S` | reset | RESET_ALL_OSCS / RESET_TIMEBASE / RESET_AMY / RESET_PATCH / RESET_SEQUENCER |
| `V` | volume | per-bus final volume (≈0–10) |
| `h`/`k`/`M`/`x` | reverb/chorus/echo/eq | per-bus effects |
| `t`/`H`/`j` | time/sequence/tempo | scheduling + sequencer |

## ControlCoefficients (the modulation model)

`amp`, `freq`, `filter_freq`, `duty`, `pan` are each a vector of weights over
sources, in this order:

```
[ const, note, vel, eg0, eg1, mod, bend, ext0, ext1 ]
```

e.g. `filter_freq='50,,,,1'` = 50 Hz base + EG1. `amp` multiplies (not sums) its
nonzero terms. Two envelope generators per osc; any osc can modulate any other via
`mod_source`.

## Patch banks

| Range | Bank |
| ----- | ---- |
| 0–127 | Juno-6 analog presets |
| 128–255 | DX7 FM presets |
| 256 | additive piano |
| PCM presets 0–66 | drums/instruments (separate from `patch`) |
| 1024–1055 | **user patches** (runtime-defined; reset with `reset=RESET_PATCH`) |

## C API we call

```c
amy_config_t amy_default_config(void);
void  amy_start(amy_config_t c);       // audio=NONE, midi=NONE for embedding
void  amy_stop(void);
amy_event amy_default_event(void);
void  amy_add_event(amy_event* e);
void  amy_add_message(char* wire);     // ASCII wire string
output_sample_type* amy_simple_fill_buffer(void);  // one block, int16 interleaved
uint32_t amy_sysclock(void);
```

## MIDI / SysEx (Hardware mode)

- Notes, pitch bend, sustain (CC64), All-Notes-Off all work directly.
- Program change switches patch *within the synth's current bank*.
- **Wire messages over SysEx:** `F0 00 03 45 <ascii wire bytes> F7`. No encoding
  needed (wire is lower-ASCII). `zI` pings; board replies `F0 00 03 45 'O' 'K' F7`.
- MIDI clock (`F8`/`FA`/`FC`) drives AMY's sequencer (24 PPQ in → 48 PPQ).

> Verify exact numeric enum values and the source-file list against the AMY
> submodule before relying on them — AMY evolves (latest release 1.2.7, Jun 2026).
