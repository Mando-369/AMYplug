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
