#!/bin/sh
# Rebuilds the engine for the browser and copies the designer into the relay's public folder.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
sh "$root/engine/wasm/build.sh" "$root/designer/atmos.wasm"
mkdir -p "$root/server/public/designer"
cp "$root/designer/index.html" "$root/designer/atmos-worklet.js" "$root/designer/atmos.wasm" "$root/designer/starter-bible.json" "$root/server/public/designer/"
echo "designer synced to server/public/designer"
