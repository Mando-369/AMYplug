# Contributing to AMYplug

Thanks for helping build the first AMY DAW plugin! 🎛️

## Ground rules
- By contributing you agree your contributions are licensed under **AGPL-3.0-or-later**
  (and, for your own new files, optionally MIT — keep the dual SPDX header).
- Read [`CLAUDE.md`](./CLAUDE.md) and [`docs/ARCHITECTURE.md`](./docs/ARCHITECTURE.md) first.
- **Never edit `third_party/amy` or `third_party/JUCE`.** Wrap, don't fork. Upstream
  fixes to AMY go to https://github.com/shorepine/amy.

## Workflow
1. Pick or open an issue tied to a [`docs/ROADMAP.md`](./docs/ROADMAP.md) milestone.
2. Branch: `feat/<short>`, `fix/<short>`, `docs/<short>`.
3. Keep PRs small and milestone-scoped. Conventional-commit titles.
4. Before pushing: `cmake --build --preset mac-debug && ctest --preset mac-debug`.
5. CI runs build + `pluginval` + `auval` on macOS (free on this public repo);
   green required to merge.

## Code style
- C++20, 4-space indent, `PascalCase` types / `camelCase` members.
- **Audio thread is sacred:** no allocations, locks, logging, or I/O in `processBlock`.
- Any change that affects sound must update `PatchModel` + serialization + the
  rebuild path together, or recall breaks. There is a test for this — keep it green.

## Good first issues
See the `M1` checklist in `docs/ROADMAP.md` and issues labeled `good first issue`.
