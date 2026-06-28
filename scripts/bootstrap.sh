#!/usr/bin/env bash
# Initialize submodules (JUCE, AMY) and fetch the canonical AGPL-3.0 license text.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> Initializing submodules (JUCE, AMY)…"
git submodule update --init --recursive

# If the submodules were not registered yet (fresh scaffold), add them.
if [ ! -e third_party/JUCE/CMakeLists.txt ]; then
  echo "==> Adding JUCE submodule…"
  git submodule add -b master https://github.com/juce-framework/JUCE.git third_party/JUCE || true
fi
if [ ! -e third_party/amy/src ]; then
  echo "==> Adding AMY submodule…"
  git submodule add -b main https://github.com/shorepine/amy.git third_party/amy || true
fi
git submodule update --init --recursive

# Pull the full AGPL text into LICENSE if only the header is present.
if ! grep -q "TERMS AND CONDITIONS" LICENSE 2>/dev/null; then
  echo "==> Appending canonical GNU AGPL-3.0 text to LICENSE…"
  {
    echo
    echo "==================== FULL GNU AGPL-3.0 TEXT ===================="
    echo
    curl -fsSL https://www.gnu.org/licenses/agpl-3.0.txt || \
      echo "(could not fetch AGPL text — add it manually from https://www.gnu.org/licenses/agpl-3.0.txt)"
  } >> LICENSE
fi

cat <<'EOF'

==> Bootstrap complete.

Next:
  cmake --preset mac-debug
  cmake --build --preset mac-debug

First implementation task (see CLAUDE.md §7):
  grep -nE 'AMY_BLOCK_SIZE|AMY_SAMPLE_RATE|AMY_NCHANS|output_sample_type' third_party/amy/src/*.h
  and copy the real values into src/engine/AmyConfig.h
EOF
