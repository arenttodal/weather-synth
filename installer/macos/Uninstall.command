#!/bin/bash
# Removes Atmospheric. Your kept Days stay in ~/Library/Application Support/Atmospheric
set -uo pipefail
rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Atmospheric.vst3" \
       "$HOME/Library/Audio/Plug-Ins/Components/Atmospheric.component" \
       "$HOME/Applications/Atmospheric.app"
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
echo "Atmospheric removed. Kept Days are still in ~/Library/Application Support/Atmospheric"
