#!/usr/bin/env bash
# packaging/macos/build-pkg.sh -- the macOS installer (6.3).
#
#   build-pkg.sh <stage> <content> <version-label> <numeric-version> <out.pkg>
#
# <stage> holds the already universal, ad-hoc signed MilkDAWp.app,
# MilkDAWp.vst3 and MilkDAWp.component plus LICENSE and README.txt
# (scripts/release/package.sh). Each goes in its own component package so
# the installer can offer them as choices:
#
#   app.pkg      MilkDAWp.app        -> /Applications
#   vst3.pkg     MilkDAWp.vst3       -> /Library/Audio/Plug-Ins/VST3
#   au.pkg       MilkDAWp.component  -> /Library/Audio/Plug-Ins/Components
#   presets.pkg  Presets, Textures   -> /Library/Application Support/MilkDAWp
#
# The product archive itself is unsigned (D11: no Developer ID Installer
# certificate), so Gatekeeper asks once before it opens; the bundles inside
# are ad-hoc signed, and files an installer package writes carry no
# quarantine flag, so they then load without further prompts.

set -euo pipefail

stage="${1:?stage folder}"
content="${2:?content folder}"
version="${3:?version label}"
numeric="${4:?numeric version}"
out="${5:?output .pkg}"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/pkgs" "$work/resources"

# component_pkg <id> <bundle-in-stage> <install-location> <out>
# A root holding just the bundle; relocation off, or Installer "updates" a
# copy it finds elsewhere on disk (an old build in ~/Downloads, say) instead
# of installing where we said.
component_pkg() {
  local id="$1" bundle="$2" location="$3" pkg="$4"
  local root="$work/root-$id"
  mkdir -p "$root"
  ditto "$stage/$bundle" "$root/$bundle"
  pkgbuild --analyze --root "$root" "$work/$id.plist" > /dev/null
  plutil -replace 0.BundleIsRelocatable -bool NO "$work/$id.plist"
  plutil -replace 0.BundleOverwriteAction -string upgrade "$work/$id.plist"
  pkgbuild --root "$root" --component-plist "$work/$id.plist" \
    --identifier "com.otitismedia.MilkDAWp.pkg.$id" --version "$numeric" \
    --install-location "$location" "$pkg"
}

component_pkg app MilkDAWp.app /Applications "$work/pkgs/app.pkg"
component_pkg vst3 MilkDAWp.vst3 /Library/Audio/Plug-Ins/VST3 "$work/pkgs/vst3.pkg"
component_pkg au MilkDAWp.component /Library/Audio/Plug-Ins/Components "$work/pkgs/au.pkg"

# Presets: plain files, no bundle (the build's stamp files left out).
presets_root="$work/root-presets"
mkdir -p "$presets_root"
ditto "$content/Presets" "$presets_root/Presets"
ditto "$content/Textures" "$presets_root/Textures"
find "$presets_root" -name '.milkdawp-*' -delete
pkgbuild --root "$presets_root" --identifier com.otitismedia.MilkDAWp.pkg.presets --version "$numeric" \
  --install-location "/Library/Application Support/MilkDAWp" "$work/pkgs/presets.pkg"

cp "$stage/LICENSE" "$work/resources/LICENSE.txt"
cp "$stage/README.txt" "$work/resources/README.txt"
sed -e "s/@VERSION@/$version/g" -e "s/@NUMERIC_VERSION@/$numeric/g" "$here/distribution.xml" > "$work/distribution.xml"

productbuild --distribution "$work/distribution.xml" --resources "$work/resources" \
  --package-path "$work/pkgs" "$out"
pkgutil --check-signature "$out" || true # "no signature" is expected (D11)
echo "$out"
