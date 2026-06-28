#!/usr/bin/env bash
# Convenience wrapper: configure + build + (optionally) test.
#   ./scripts/build.sh [preset]      default preset: mac-release
set -euo pipefail
cd "$(dirname "$0")/.."

PRESET="${1:-mac-release}"

if [ ! -e third_party/JUCE/CMakeLists.txt ] || [ ! -e third_party/amy/src ]; then
  echo "Submodules missing — running bootstrap first."
  ./scripts/bootstrap.sh
fi

cmake --preset "$PRESET"
cmake --build --preset "$PRESET"

# Run tests if a matching test preset exists.
if cmake --list-presets test 2>/dev/null | grep -q "\"$PRESET\""; then
  ctest --preset "$PRESET"
fi

echo "==> Done. Artefacts under build/$PRESET/AMYplug_artefacts/"
