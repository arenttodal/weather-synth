#!/bin/bash
# Installs Atmospheric for your user account (no admin password needed):
#   VST3       -> ~/Library/Audio/Plug-Ins/VST3
#   Audio Unit -> ~/Library/Audio/Plug-Ins/Components
#   App        -> ~/Applications
set -euo pipefail
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
APP_DIR="$HOME/Applications"
mkdir -p "$VST3_DIR" "$AU_DIR" "$APP_DIR"

install_bundle () {
  local src="$1" dest_dir="$2"
  local name; name="$(basename "$src")"
  if [ ! -d "$src" ]; then echo "Missing $src next to this installer"; exit 1; fi
  rm -rf "${dest_dir:?}/$name"
  cp -R "$src" "$dest_dir/"
  # Downloads are quarantined by macOS; these builds are not notarized, so clear it
  xattr -dr com.apple.quarantine "$dest_dir/$name" 2>/dev/null || true
  codesign --force --deep --sign - "$dest_dir/$name" >/dev/null 2>&1 || true
  echo "  installed $dest_dir/$name"
}

echo "Installing Atmospheric…"
install_bundle "Atmospheric.vst3" "$VST3_DIR"
install_bundle "Atmospheric.component" "$AU_DIR"
install_bundle "Atmospheric.app" "$APP_DIR"

# Make Logic / GarageBand notice the new Audio Unit straight away
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

echo
echo "Done. Rescan plugins in your DAW (Ableton: Preferences > Plug-ins > Rescan)."
echo "The standalone app is in ~/Applications/Atmospheric.app"
