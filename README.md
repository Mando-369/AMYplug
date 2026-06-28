# AMYplug

**An open-source Audio Unit / VST3 controller-instrument plugin for the [AMY synthesizer](https://github.com/shorepine/amy) and the [AMYboard](https://amyboard.com).**

AMYplug lets you load AMY inside your DAW as a normal instrument plugin: play it, design/edit/save patches, automate every parameter, and have the whole sound recall perfectly with your project. It runs in two switchable modes:

- **Software mode (default / MVP):** AMY's MIT-licensed C engine is compiled into the plugin and renders audio directly inside the DAW. Full automation, sample-accurate recall, and *no hanging notes* (the plugin owns note on/off in C++).
- **Hardware mode:** the plugin drives a physical **AMYboard** over MIDI, sending notes and AMY *wire messages* via SysEx. Patch state is re-sent on project load so hardware sessions recall too.

> Status: **scaffold / pre-alpha.** This repository is a fully laid-out project skeleton intended to be implemented with Claude Code. See [`CLAUDE.md`](./CLAUDE.md) and [`docs/ROADMAP.md`](./docs/ROADMAP.md).

---

## Why this exists

The AMY web app is great, but driving it live can produce hanging notes and other quirks. A native plugin solves this by:

1. Owning the MIDI note lifecycle in C++ (deterministic note-off, panic, transport-stop handling).
2. Storing a canonical, serializable patch model so **everything is recallable** with the DAW project.
3. Exposing AMY parameters as host-automatable plugin parameters.

There is currently **no** AMY VST3/AU plugin in existence — this is the first.

## Features (target)

- AU + VST3 (+ standalone) on macOS first; Windows VST3 later (see roadmap).
- All AMY engines reachable: Juno-6 analog, DX7-style FM, PCM/sampler + drums, additive partials, modular oscillator toolkit.
- Patch browser + editor; load/save user patches; import DX7 / Juno-6 SysEx.
- Per-parameter host automation with full state recall.
- Software ⇄ Hardware mode switch (drive a real AMYboard).
- MPE-friendly note routing, sustain pedal, pitch bend.

## Build (once implemented)

```bash
git clone --recurse-submodules https://github.com/Mando-369/AMYplug.git
cd AMYplug
./scripts/bootstrap.sh        # pulls JUCE + AMY submodules
cmake --preset mac-release
cmake --build --preset mac-release
```

Artifacts land in `build/AMYplug_artefacts/`. See [`docs/ARCHITECTURE.md`](./docs/ARCHITECTURE.md) for how the pieces fit together.

## License

The plugin links **JUCE 8**, whose free license is **AGPLv3**, so the distributed plugin is licensed under **AGPL-3.0** (see [`LICENSE`](./LICENSE)). AMYplug's own first-party source files are additionally offered under the MIT license where noted, but any binary that includes JUCE must comply with AGPL-3.0. Bundled AMY remains MIT. See [`NOTICES.md`](./NOTICES.md) for the full third-party breakdown.

## Credits

- [AMY](https://github.com/shorepine/amy) by DAn Ellis & Brian Whitman / Shore Pine Sound Systems (MIT).
- [AMYboard](https://amyboard.com) hardware.
- Built with [JUCE](https://juce.com) (AGPLv3).

This is an independent community project and is not affiliated with or endorsed by Shore Pine Sound Systems.
