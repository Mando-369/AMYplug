# Third-party notices

AMYplug is distributed under **AGPL-3.0** (see `LICENSE`). It incorporates the
following third-party components.

## JUCE 8 — AGPL-3.0 (free tier) or commercial
- https://github.com/juce-framework/JUCE
- The free JUCE license is AGPL-3.0. This is the reason the AMYplug binary is
  AGPL-3.0. If the project ever needs a non-copyleft binary, a commercial JUCE
  license would be required (out of scope for this open-source project).
- Vendored as a git submodule under `third_party/JUCE` (not modified).

## AMY — MIT
- https://github.com/shorepine/amy  · Copyright (c) DAn Ellis, Brian Whitman,
  Shore Pine Sound Systems.
- Vendored as a git submodule under `third_party/amy` (not modified). MIT text
  is preserved in that submodule. AMY is compatible with AGPL-3.0.

## AMYplugFX DSP (src/amyfx/) — derived from AMY · MIT
The `src/amyfx/` module is AMY's bus DSP (from AMY's `filters.c` and `delay.c`),
transcribed to float as standalone, per-instance blocks so they run in the AMYplugFX
effect without the AMY engine or its global state:
- `AmyBiquad.h` — the RBJ/AMY biquad coefficient generators (`dsps_biquad_gen_*`).
- `AmyFilter.h` — the analog VCF, incl. the 24 dB/oct "twice" cascade (the Juno slope).
- `AmyEq.h` — the 3-band parametric bus EQ (`parametric_eq_process`, 800/2500/7000 Hz).
- `AmyReverb.h` — the stereo reverb (`stereo_reverb`, a Stautner-Puckette design).
- `AmyChorus.h` — the bus chorus, reconstructed from AMY's parameters + method
  (triangle-LFO modulated fractional delay); AMY generates the LFO with its
  oscillator engine, so this reproduces the behavior rather than byte-copying it.

Coefficient generation and the DF-I / split-feedback biquad topologies are copied from
AMY. AMY is MIT (see the AMY entry above); these files carry AMY's copyright/permission
notice and additionally `SPDX-License-Identifier: AGPL-3.0-or-later OR MIT`.
`third_party/amy/` itself is unmodified — this is a copy, not an edit. The envelope
follower (`src/amyfx/EnvelopeFollower.h`) is first-party.

## AMYplug FX DSP (BitCrusher, WdfClipper) — ported from Faust · STK-4.3
The two output effects that ship in both the instrument's master bus and the
**AMYplugFX** plugin — the bitcrusher (`src/dsp/BitCrusher.h`) and the WDF
antiparallel-diode saturator (`src/dsp/WdfClipper.h`) — are C++ transcriptions of
Faust source:
- **Bitcrusher / downsample** — the Faust standard-library functions
  `ba.bitcrusher` (Julius O. Smith III) and `ba.downSample` (Romain Michon).
- **WDF diode clipper** — `KalosSoftClipper.dsp` (Thomas Mandolini); its diode
  wave-digital-filter model is by **Dirk Roosenburg**.

These Faust-library building blocks are provided under the **STK-4.3** license — a
permissive, MIT-style license (Copyright (c) 1995–2017 Perry R. Cook and Gary P.
Scavone), full text vendored at [`licenses/STK-4.3.txt`](licenses/STK-4.3.txt) and
upstream at
https://github.com/grame-cncm/faustlibraries/blob/master/licenses/stk-4.3.0.md.
STK-4.3 is compatible with AGPL-3.0. Separately, code emitted by the Faust compiler
is exempt from the compiler's GPL (Faust's code-generation exception), so these
ports carry no copyleft obligation from Faust itself. The transcribed files in this
repo additionally carry `SPDX-License-Identifier: AGPL-3.0-or-later OR MIT`.

## Catch2 — BSL-1.0 (tests only)
- https://github.com/catchorg/Catch2 — fetched by CMake for the test target.

## pluginval — GPL-3.0 (CI tooling only, not linked)
- https://github.com/Tracktion/pluginval — used to validate builds in CI.

---

### AMYplug first-party code
Files authored in this repository (under `src/`, `cmake/`, `scripts/`, `tests/`)
carry `SPDX-License-Identifier: AGPL-3.0-or-later OR MIT`, meaning you may reuse
those individual files under MIT if you extract them from the JUCE-linked binary.
The combined, distributed plugin remains AGPL-3.0.

This project is independent and not affiliated with or endorsed by Shore Pine
Sound Systems or the JUCE / PACE teams.
