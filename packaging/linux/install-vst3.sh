#!/bin/sh
# Installs the MilkDAWp VST3 and its presets for the current user (6.4):
#   MilkDAWp.vst3 -> ~/.vst3/
#   Content/      -> ~/.local/share/milkdawp/   (where the plugin looks)
# Run from the folder this script is in: ./install-vst3.sh
# Remove with: rm -rf ~/.vst3/MilkDAWp.vst3 ~/.local/share/milkdawp
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
data="${XDG_DATA_HOME:-$HOME/.local/share}/milkdawp"

mkdir -p "$HOME/.vst3" "$data"
rm -rf "$HOME/.vst3/MilkDAWp.vst3"
# 1.0 put the presets in "Cream of the Crop"; they're "Cream of the CrAWp" now.
rm -rf "$data/Presets/Cream of the Crop"
cp -R "$here/MilkDAWp.vst3" "$HOME/.vst3/"
cp -R "$here/Content/." "$data/"
echo "Installed $HOME/.vst3/MilkDAWp.vst3 and the presets in $data."
echo "Rescan plugins in your DAW."
