#!/usr/bin/env bash
# Assemble the end-user release archive: AMYplug-macOS.zip containing the AU,
# VST3, Standalone app, the installer, and the docs a user needs. Run AFTER a
# release build:
#   cmake --preset mac-release && cmake --build --preset mac-release
#   ./scripts/package.sh
# Output: dist/AMYplug-macOS.zip

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ART="$ROOT/build/mac-release/AMYplug_artefacts/Release"
DIST="$ROOT/dist"
STAGE="$DIST/AMYplug-macOS"

[[ "$(uname)" == "Darwin" ]] || { echo "macOS only." >&2; exit 1; }
[[ -d "$ART" ]] || { echo "error: no release build at $ART
build first:  cmake --preset mac-release && cmake --build --preset mac-release" >&2; exit 1; }

echo "==> Staging release in $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE"

copied=0
for b in "$ART/AU/AMYplug.component" "$ART/VST3/AMYplug.vst3" "$ART/Standalone/AMYplug.app"; do
  if [[ -d "$b" ]]; then cp -R "$b" "$STAGE/"; echo "  + $(basename "$b")"; copied=$((copied+1)); fi
done
[[ "$copied" -gt 0 ]] || { echo "error: no plug-in bundles found in $ART" >&2; exit 1; }

cp "$SCRIPT_DIR/install.sh" "$STAGE/"; chmod +x "$STAGE/install.sh"
cp "$ROOT/README.md" "$STAGE/"
cp "$ROOT/LICENSE"   "$STAGE/" 2>/dev/null || true
cp "$ROOT/NOTICES.md" "$STAGE/" 2>/dev/null || true
echo "  + install.sh, README.md, LICENSE, NOTICES.md"

echo "==> Zipping (ditto, preserves bundle structure)"
rm -f "$DIST/AMYplug-macOS.zip"
( cd "$DIST" && ditto -c -k --sequesterRsrc --keepParent "AMYplug-macOS" "AMYplug-macOS.zip" )

echo "==> Done: $DIST/AMYplug-macOS.zip"
ls -lh "$DIST/AMYplug-macOS.zip" | awk '{print "    size:", $5}'
echo "Upload it to a GitHub Release (or run: gh release create vX.Y.Z \"$DIST/AMYplug-macOS.zip\")."
