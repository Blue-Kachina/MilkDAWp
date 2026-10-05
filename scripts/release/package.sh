#!/usr/bin/env bash
# scripts/release/package.sh -- stage and archive one platform's release build
# (Phase 6.5). Called by .github/workflows/release.yml after
# `cmake --build --preset release-<platform>`; runs locally the same way.
#
# Until the installers exist (6.2-6.4) a release is zipped bundles:
#
#   MilkDAWp-<version>-<platform>/
#     MilkDAWp.vst3              the plugin, projectM inside its binary folder
#     MilkDAWp/ or MilkDAWp.app  the standalone app, projectM next to it
#     LICENSE, LICENSES/, THIRD_PARTY_NOTICES.md
#     LICENSES/projectM-COPYRIGHT.txt   projectM's LGPL notice, from vcpkg
#     README.txt                 where to put each piece, per platform
#
# Usage:
#   scripts/release/package.sh <windows|macos|linux> <build-dir> <version-label> <out-dir>
#
# Environment:
#   MILKDAWP_VCPKG_TRIPLET_DIR    vcpkg_installed/<triplet> of the build (for
#                                 projectM's copyright file); defaults to the
#                                 one under <build-dir>.
#   MILKDAWP_PROJECTM_X64_DYLIB   macOS only: an x86_64 libprojectM-4 dylib to
#                                 merge with the arm64 one vcpkg built, so the
#                                 universal binaries can load projectM on both
#                                 architectures. Without it the macOS package
#                                 is refused: Intel Macs would run inert.
#
# Prints the archive paths it wrote, one per line, on stdout.

set -euo pipefail

platform="${1:?platform (windows|macos|linux)}"
build_dir="${2:?build directory}"
version="${3:?version label, e.g. 2.0.0-beta.1}"
out_dir="${4:?output directory}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
product="MilkDAWp"

case "$platform" in
  windows) suffix="windows-x64" ;;
  macos)   suffix="macos-universal" ;;
  linux)   suffix="linux-x64" ;;
  *) echo "package.sh: unknown platform '$platform'" >&2; exit 2 ;;
esac

die() { echo "package.sh: $*" >&2; exit 1; }
log() { echo "package.sh: $*" >&2; }

[[ -d "$build_dir" ]] || die "no build directory at $build_dir"
mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"

plugin_root="$build_dir/plugin/milkdawp_plugin_artefacts/Release/VST3"
app_root="$build_dir/app/milkdawp_app_artefacts/Release"
vst3="$plugin_root/$product.vst3"

# A release preset never sets MILKDAWP_DEV_ALT_IDENTITY (ADR-0007), so the
# bundles carry the real name. Anything else would replace v1 under the wrong
# identity, or not replace it at all.
[[ -d "$vst3" ]] || die "no $product.vst3 under $plugin_root (built with the dev identity, or not built?)"

triplet_dir="${MILKDAWP_VCPKG_TRIPLET_DIR:-}"
if [[ -z "$triplet_dir" ]]; then
  # vcpkg_installed also holds the host triplet's tools; take the one with projectM.
  for d in "$build_dir"/vcpkg_installed/*/; do
    if [[ -f "$d/share/projectm/copyright" ]]; then triplet_dir="${d%/}"; break; fi
  done
fi
projectm_copyright="$triplet_dir/share/projectm/copyright"
[[ -f "$projectm_copyright" ]] || die "projectM's copyright file is missing ($projectm_copyright); set MILKDAWP_VCPKG_TRIPLET_DIR"

name="$product-$version-$suffix"
stage_parent="$(mktemp -d)"
trap 'rm -rf "$stage_parent"' EXIT
stage="$stage_parent/$name"
mkdir -p "$stage"

# --- Bundles --------------------------------------------------------------
# Build byproducts (.pdb/.ilk/.exp/.lib) are allowed next to the binaries
# (check_runtime_layout.cmake) but never ship; Windows symbols go into their
# own archive below.
copy_clean() {
  local src="$1" dest="$2"
  if [[ "$platform" == macos ]]; then
    ditto "$src" "$dest"   # keeps symlinks, permissions and extended attributes
  else
    cp -R "$src" "$dest"
  fi
  find "$dest" -type f \( -name '*.pdb' -o -name '*.ilk' -o -name '*.exp' -o -name '*.lib' \) -delete
}

copy_clean "$vst3" "$stage/$product.vst3"

case "$platform" in
  windows)
    [[ -f "$app_root/$product.exe" ]] || die "no $product.exe in $app_root"
    mkdir -p "$stage/$product"
    for f in "$app_root"/*; do
      case "$f" in *.pdb|*.ilk|*.exp|*.lib) continue ;; esac
      cp -R "$f" "$stage/$product/"
    done
    ;;
  macos)
    [[ -d "$app_root/$product.app" ]] || die "no $product.app in $app_root"
    copy_clean "$app_root/$product.app" "$stage/$product.app"
    ;;
  linux)
    [[ -x "$app_root/$product" ]] || die "no $product executable in $app_root"
    mkdir -p "$stage/$product"
    cp "$app_root/$product" "$stage/$product/"
    cp "$app_root"/libprojectM-4*.so "$stage/$product/"
    ;;
esac

# --- macOS: universal projectM and ad-hoc signing (D11) --------------------
if [[ "$platform" == macos ]]; then
  x64_dylib="${MILKDAWP_PROJECTM_X64_DYLIB:-}"
  [[ -n "$x64_dylib" && -f "$x64_dylib" ]] \
    || die "MILKDAWP_PROJECTM_X64_DYLIB must name an x86_64 libprojectM-4 dylib (vcpkg's arm64 one alone leaves Intel Macs inert)"
  for bundle in "$stage/$product.vst3" "$stage/$product.app"; do
    dylib="$bundle/Contents/MacOS/libprojectM-4.dylib"
    [[ -f "$dylib" ]] || die "no libprojectM-4.dylib in $bundle/Contents/MacOS"
    lipo -create "$dylib" "$x64_dylib" -output "$dylib.universal"
    mv "$dylib.universal" "$dylib"
    for bin in "$dylib" "$bundle/Contents/MacOS/$product"; do
      archs="$(lipo -archs "$bin")"
      [[ "$archs" == *x86_64* && "$archs" == *arm64* ]] || die "$bin is not universal (has: $archs)"
    done
    # Ad-hoc: no Developer ID (D11). Re-signing seals the merged dylib into
    # the bundle; Gatekeeper still asks on first launch (README.txt).
    codesign --force --deep --sign - "$bundle"
    codesign --verify --deep --strict "$bundle"
  done
fi

# --- Licences and notes ----------------------------------------------------
cp "$repo_root/LICENSE" "$stage/"
cp -R "$repo_root/LICENSES" "$stage/"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$stage/"
cp "$projectm_copyright" "$stage/LICENSES/projectM-COPYRIGHT.txt"

{
  echo "$product $version ($suffix)"
  echo
  echo "MilkDAWp is a music visualizer: a VST3 plugin for your DAW and a standalone app."
  echo "This is a zipped build. Installers come later; for now, copy the pieces by hand."
  echo
  case "$platform" in
    windows)
      echo "Plugin: copy the MilkDAWp.vst3 folder to C:\\Program Files\\Common Files\\VST3\\"
      echo "        then rescan plugins in your DAW."
      echo "App:    copy the MilkDAWp folder anywhere (e.g. C:\\Program Files\\MilkDAWp\\) and"
      echo "        run MilkDAWp.exe. Keep projectM-4.dll next to it."
      echo
      echo "Needs the Microsoft Visual C++ 2015-2022 Redistributable (x64):"
      echo "  https://aka.ms/vs/17/release/vc_redist.x64.exe"
      echo "This build is not code-signed yet, so Windows SmartScreen may warn on first launch"
      echo "(More info > Run anyway). Check the download against SHA256SUMS.txt on the release."
      ;;
    macos)
      echo "Plugin: copy MilkDAWp.vst3 to ~/Library/Audio/Plug-Ins/VST3/ (or /Library/...)."
      echo "App:    copy MilkDAWp.app to /Applications."
      echo
      echo "These are ad-hoc signed, not notarized (no paid Apple Developer account), so"
      echo "macOS blocks them on first launch. To allow them, either:"
      echo "  - right-click MilkDAWp.app > Open > Open, or System Settings > Privacy &"
      echo "    Security > Open Anyway; or"
      echo "  - in Terminal:"
      echo "      xattr -dr com.apple.quarantine /Applications/MilkDAWp.app"
      echo "      xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MilkDAWp.vst3"
      echo "Check the download against SHA256SUMS.txt on the release first."
      ;;
    linux)
      echo "Plugin: copy MilkDAWp.vst3 to ~/.vst3/ (or /usr/lib/vst3/)."
      echo "App:    copy the MilkDAWp folder anywhere and run ./MilkDAWp. Keep"
      echo "        libprojectM-4.so next to it."
      echo
      echo "Needs OpenGL 3.3 (Mesa or a vendor driver), ALSA or JACK, and X11."
      ;;
  esac
  echo
  echo "Licences: MilkDAWp is AGPL-3.0-or-later (LICENSE). projectM is LGPL-2.1, shipped as"
  echo "a separate shared library you may replace (LICENSES/projectM-COPYRIGHT.txt; source"
  echo "in THIRD_PARTY_NOTICES.md). Source code: https://github.com/Blue-Kachina/MilkDAWp2"
} > "$stage/README.txt"

# --- Archive ---------------------------------------------------------------
archives=()
case "$platform" in
  windows)
    archive="$out_dir/$name.zip"
    rm -f "$archive"
    (cd "$stage_parent" && cmake -E tar cf "$archive" --format=zip "$name")
    archives+=("$archive")

    # Symbols for the crash reporter's minidumps (4.10): the app's and the
    # VST3's .pdb. Release presets link with /DEBUG for exactly this. Both
    # are named MilkDAWp.pdb, hence one folder each.
    sym_stage="$stage_parent/$name-symbols"
    mkdir -p "$sym_stage/app" "$sym_stage/vst3"
    cp "$app_root"/*.pdb "$sym_stage/app/" 2>/dev/null || true
    cp "$plugin_root"/*.pdb "$sym_stage/vst3/" 2>/dev/null || true
    if compgen -G "$sym_stage/*/*.pdb" > /dev/null; then
      sym_archive="$out_dir/$name-symbols.zip"
      rm -f "$sym_archive"
      (cd "$stage_parent" && cmake -E tar cf "$sym_archive" --format=zip "$name-symbols")
      archives+=("$sym_archive")
    else
      log "warning: no .pdb files found, so no symbols archive"
    fi
    ;;
  macos)
    archive="$out_dir/$name.zip"
    rm -f "$archive"
    ditto -c -k --keepParent "$stage" "$archive"
    archives+=("$archive")
    ;;
  linux)
    archive="$out_dir/$name.tar.gz"
    rm -f "$archive"
    tar -C "$stage_parent" -czf "$archive" "$name"
    archives+=("$archive")
    ;;
esac

for a in "${archives[@]}"; do
  log "wrote $a ($(du -h "$a" | cut -f1))"
  echo "$a"
done
