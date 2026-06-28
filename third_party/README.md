# third_party

These are **git submodules**, populated by `scripts/bootstrap.sh`. Do not edit
their contents — wrap them in `src/` instead.

| Path | Repo | License | Role |
| ---- | ---- | ------- | ---- |
| `JUCE/` | https://github.com/juce-framework/JUCE | AGPL-3.0 (free tier) | Plugin framework (AU/VST3/Standalone) |
| `amy/`  | https://github.com/shorepine/amy | MIT | The synthesizer engine (Software mode) |

If they are empty, run:

```bash
git submodule update --init --recursive
# or, first time:
./scripts/bootstrap.sh
```
