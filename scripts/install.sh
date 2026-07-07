#!/usr/bin/env bash
# AMYplug installer for macOS — installs the AMYplug instrument AND the AMYplugFX
# effect (AU + VST3, and optionally the instrument's Standalone app) into your user
# plug-in folders, clearing macOS Gatekeeper quarantine so your DAW will load them.
#
# Usage:
#   ./install.sh                # install AU + VST3
#   ./install.sh --standalone   # also install the Standalone app to /Applications
#   ./install.sh --uninstall    # remove all AMYplug plug-ins + app
#   ./install.sh --help
#
# No sudo needed for AU/VST3 (they go in your home folder). The Standalone app
# install may ask for your password since /Applications is system-wide.
#
# The plug-ins are ad-hoc signed ("Sign to Run Locally"), not notarized by Apple,
# so a fresh download is quarantined by Gatekeeper. This script removes that
# quarantine and re-applies a local ad-hoc signature so macOS trusts them.

set -euo pipefail

# --- pretty output --------------------------------------------------------
if [[ -t 1 ]]; then B=$'\033[1m'; G=$'\033[32m'; Y=$'\033[33m'; R=$'\033[31m'; N=$'\033[0m'; else B=; G=; Y=; R=; N=; fi
say()  { printf '%s\n' "${B}==>${N} $*"; }
ok()   { printf '%s\n' "${G}  ✓${N} $*"; }
warn() { printf '%s\n' "${Y}  !${N} $*"; }
die()  { printf '%s\n' "${R}error:${N} $*" >&2; exit 1; }

# --- locations ------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DIR="/Applications"

# Both plug-ins: the AMYplug instrument and the AMYplugFX effect.
AU_NAMES=("AMYplug.component" "AMYplugFX.component")
VST3_NAMES=("AMYplug.vst3" "AMYplugFX.vst3")
APP_NAME="AMYplug.app"   # only the instrument ships a Standalone build

WANT_STANDALONE=0
DO_UNINSTALL=0
for arg in "$@"; do
  case "$arg" in
    --standalone) WANT_STANDALONE=1 ;;
    --uninstall)  DO_UNINSTALL=1 ;;
    -h|--help)
      # print the contiguous leading comment block (skip the shebang)
      awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "${BASH_SOURCE[0]}"
      exit 0 ;;
    *) die "unknown option: $arg (try --help)" ;;
  esac
done

[[ "$(uname)" == "Darwin" ]] || die "this installer is for macOS only."

# --- uninstall ------------------------------------------------------------
if [[ "$DO_UNINSTALL" == 1 ]]; then
  say "Removing AMYplug…"
  for p in "${AU_NAMES[@]/#/$AU_DIR/}" "${VST3_NAMES[@]/#/$VST3_DIR/}" "$APP_DIR/$APP_NAME"; do
    if [[ -e "$p" ]]; then rm -rf "$p" && ok "removed $p"; fi
  done
  ok "Uninstall complete. Rescan plug-ins in your DAW."
  exit 0
fi

# --- find the built bundles ----------------------------------------------
# Look next to this script first (release-zip layout), then in the repo build dir.
find_bundle() {
  local name="$1"; shift
  local c
  for c in \
    "$SCRIPT_DIR/$name" \
    "$SCRIPT_DIR/../$name" \
    "$SCRIPT_DIR/../build/mac-release/AMYplug_artefacts/Release/AU/$name" \
    "$SCRIPT_DIR/../build/mac-release/AMYplug_artefacts/Release/VST3/$name" \
    "$SCRIPT_DIR/../build/mac-release/AMYplug_artefacts/Release/Standalone/$name" \
    "$SCRIPT_DIR/../build/mac-release/AMYplugFX_artefacts/Release/AU/$name" \
    "$SCRIPT_DIR/../build/mac-release/AMYplugFX_artefacts/Release/VST3/$name"
  do
    if [[ -d "$c" ]]; then printf '%s' "$c"; return 0; fi
  done
  return 1
}

install_bundle() {  # <src> <dest_dir>
  local src="$1" dest_dir="$2"
  local base; base="$(basename "$src")"
  mkdir -p "$dest_dir"
  rm -rf "$dest_dir/$base"
  cp -R "$src" "$dest_dir/$base"
  # Clear Gatekeeper quarantine and re-apply a local ad-hoc signature.
  xattr -dr com.apple.quarantine "$dest_dir/$base" 2>/dev/null || true
  codesign --force --deep --sign - "$dest_dir/$base" >/dev/null 2>&1 || \
    warn "could not re-sign $base (usually still fine after quarantine removal)"
  ok "installed $dest_dir/$base"
}

say "Installing AMYplug plug-ins…"
installed=0
for n in "${AU_NAMES[@]}"; do
  src="$(find_bundle "$n" || true)"
  if [[ -n "$src" ]]; then install_bundle "$src" "$AU_DIR"; installed=1; fi
done
for n in "${VST3_NAMES[@]}"; do
  src="$(find_bundle "$n" || true)"
  if [[ -n "$src" ]]; then install_bundle "$src" "$VST3_DIR"; installed=1; fi
done

[[ "$installed" == 1 ]] || die "couldn't find any AMYplug plug-ins.
Put this script next to the .component/.vst3 bundles (release zip), or build first:
  cmake --preset mac-release && cmake --build --preset mac-release"

APP_SRC="$(find_bundle "$APP_NAME" || true)"

if [[ "$WANT_STANDALONE" == 1 ]]; then
  if [[ -n "$APP_SRC" ]]; then
    say "Installing Standalone app to $APP_DIR…"
    if [[ -w "$APP_DIR" ]]; then install_bundle "$APP_SRC" "$APP_DIR"
    else
      warn "$APP_DIR needs admin rights; installing with sudo (you may be prompted)…"
      sudo rm -rf "$APP_DIR/$APP_NAME"
      sudo cp -R "$APP_SRC" "$APP_DIR/$APP_NAME"
      sudo xattr -dr com.apple.quarantine "$APP_DIR/$APP_NAME" 2>/dev/null || true
      sudo codesign --force --deep --sign - "$APP_DIR/$APP_NAME" >/dev/null 2>&1 || true
      ok "installed $APP_DIR/$APP_NAME"
    fi
  else
    warn "no Standalone app found — skipping"
  fi
fi

# --- refresh the AudioUnit cache so hosts see the new AU right away --------
killall -q AudioComponentRegistrar 2>/dev/null || true

echo
ok "Done."
echo "Next steps:"
echo "  • Restart your DAW (or trigger a plug-in rescan) to pick up AMYplug (instrument)"
echo "    and AMYplugFX (audio effect — bitcrusher + diode saturator)."
echo "  • Logic/GarageBand users: the AU may take a moment to validate on first scan."
echo "  • Verify from Terminal (optional):  auval -v aumu Amyp Mand   (instrument)"
echo "                                      auval -v aufx Amfx Mand   (effect)"
echo "  • Hardware-mode users: read docs/HARDWARE_MODE.md — and if audio ever sounds"
echo "    glitchy/drifty, CHECK YOUR AUDIO MASTER CLOCK / Aggregate Device first."
