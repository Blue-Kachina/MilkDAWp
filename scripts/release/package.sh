#!/usr/bin/env bash
# scripts/release/package.sh -- stage one platform's release build and make
# its installers (6.2-6.5). Called by .github/workflows/release.yml after
# `cmake --build --preset release-<platform>`; runs locally the same way.
#
#   scripts/release/package.sh <windows|macos|linux> <build-dir> <version-label> <out-dir>
#
# Writes to <out-dir>, and prints the paths, one per line:
#
#   windows  MilkDAWp-<v>-windows-x64-setup.exe   Inno Setup installer (6.2):
#                                                  VST3, app, presets, VC++ runtime
#            MilkDAWp-<v>-windows-x64-symbols.zip  .pdb files for crash minidumps
#   macos    MilkDAWp-<v>-macos-universal.pkg      app, VST3, AU, presets (6.3)
#   linux    MilkDAWp-<v>-x86_64.AppImage           the app with its presets (6.4)
#            MilkDAWp-<v>-linux-x64-vst3.tar.gz     the VST3 + presets + install-vst3.sh
#            milkdawp_<v>_amd64.deb               app, VST3 and presets system-wide
#
# The presets and textures (6.1) go to the shared location
# engine::BundledContent searches, so the app and every plugin instance find
# them: C:\ProgramData\MilkDAWp, /Library/Application Support/MilkDAWp,
# usr/share/milkdawp (AppImage, .deb) or ~/.local/share/milkdawp (tarball).
#
# Environment:
#   MILKDAWP_VCPKG_TRIPLET_DIR    vcpkg_installed/<triplet> of the build (for
#                                 projectM's copyright file); found under
#                                 <build-dir> when unset.
#   MILKDAWP_PROJECTM_X64_DYLIB   macOS: an x86_64 libprojectM-4 dylib to merge
#                                 with vcpkg's arm64 one (required: without it
#                                 Intel Macs would run inert).
#   MILKDAWP_ISCC                 Windows: Inno Setup's ISCC.exe (else searched).
#   MILKDAWP_VC_REDIST            Windows: vc_redist.x64.exe (else downloaded).
#   MILKDAWP_APPIMAGETOOL,
#   MILKDAWP_APPIMAGE_RUNTIME     Linux: see packaging/linux/build-appimage.sh.

set -euo pipefail

platform="${1:?platform (windows|macos|linux)}"
build_dir="${2:?build directory}"
version="${3:?version label, e.g. 1.0.0-beta.1}"
out_dir="${4:?output directory}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
product="MilkDAWp"
numeric="${version%%-*}" # 1.0.0-beta.1 -> 1.0.0

case "$platform" in
  windows) suffix="windows-x64" ;;
  macos)   suffix="macos-universal" ;;
  linux)   suffix="linux-x64" ;;
  *) echo "package.sh: unknown platform '$platform'" >&2; exit 2 ;;
esac

die() { echo "package.sh: $*" >&2; exit 1; }
log() { echo "package.sh: $*" >&2; }

[[ -d "$build_dir" ]] || die "no build directory at $build_dir"
build_dir="$(cd "$build_dir" && pwd)"
mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"

artefacts="$build_dir/plugin/milkdawp_plugin_artefacts/Release"
app_root="$build_dir/app/milkdawp_app_artefacts/Release"
vst3="$artefacts/VST3/$product.vst3"
au="$artefacts/AU/$product.component"

# A release preset never sets MILKDAWP_DEV_ALT_IDENTITY (ADR-0007), so the
# bundles carry the real name. Anything else would replace v1 under the wrong
# identity, or not replace it at all.
[[ -d "$vst3" ]] || die "no $product.vst3 under $artefacts/VST3 (built with the dev identity, or not built?)"

triplet_dir="${MILKDAWP_VCPKG_TRIPLET_DIR:-}"
if [[ -z "$triplet_dir" ]]; then
  # vcpkg_installed also holds the host triplet's tools; take the one with projectM.
  for d in "$build_dir"/vcpkg_installed/*/; do
    if [[ -f "$d/share/projectm/copyright" ]]; then triplet_dir="${d%/}"; break; fi
  done
fi
projectm_copyright="$triplet_dir/share/projectm/copyright"
[[ -f "$projectm_copyright" ]] || die "projectM's copyright file is missing ($projectm_copyright); set MILKDAWP_VCPKG_TRIPLET_DIR"

# The bundled presets and textures (cmake/BundledContent.cmake). A release
# without them starts with an empty library, so they're required.
content="$build_dir/content"
[[ -d "$content/Textures" ]] \
  || die "no bundled textures in $content (configured with MILKDAWP_BUNDLE_CONTENT=OFF?)"
# The presets are made by the build from the fetched pack, with their Macros
# (8.11, ADR-0014); the original .milk files don't ship.
[[ -f "$content/Presets/Cream of the CrAWp/.milkdawp-pack.stamp" ]] \
  || die "the bundled presets weren't made (build the milkdawp_preset_pack target)"
[[ -f "$content/Presets/Cream of the CrAWp/.milkdawp-originals.stamp" ]] \
  || die "our own presets aren't in the pack (build the milkdawp_original_presets target)"
[[ ! -e "$content/Presets/Cream of the Crop" ]] \
  || die "$content/Presets/Cream of the Crop is a build tree from before 8.11; reconfigure to remove it"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
stage="$work/stage"
mkdir -p "$stage"

# --- Stage the bundles ------------------------------------------------------
# Build byproducts (.pdb/.ilk/.exp/.lib) are allowed next to the binaries
# (check_runtime_layout.cmake) but never ship; Windows symbols get their own
# archive below.
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
    # JUCE builds the app's .ico from resources/icon.png (ICON_BIG).
    icon="$(find "$build_dir/app" -name icon.ico -path '*JuceLibraryCode*' | head -n 1)"
    [[ -n "$icon" ]] || die "no icon.ico under $build_dir/app (JUCE makes it from resources/icon.png)"
    cp "$icon" "$stage/$product.ico"
    ;;
  macos)
    [[ -d "$app_root/$product.app" ]] || die "no $product.app in $app_root"
    [[ -d "$au" ]] || die "no $product.component under $artefacts/AU"
    copy_clean "$app_root/$product.app" "$stage/$product.app"
    copy_clean "$au" "$stage/$product.component"
    ;;
  linux)
    [[ -x "$app_root/$product" ]] || die "no $product executable in $app_root"
    mkdir -p "$stage/$product"
    cp "$app_root/$product" "$app_root"/libprojectM-4*.so "$stage/$product/"
    ;;
esac

# --- macOS: universal projectM and ad-hoc signing (D11) --------------------
if [[ "$platform" == macos ]]; then
  x64_dylib="${MILKDAWP_PROJECTM_X64_DYLIB:-}"
  [[ -n "$x64_dylib" && -f "$x64_dylib" ]] \
    || die "MILKDAWP_PROJECTM_X64_DYLIB must name an x86_64 libprojectM-4 dylib (vcpkg's arm64 one alone leaves Intel Macs inert)"
  for bundle in "$stage/$product.vst3" "$stage/$product.component" "$stage/$product.app"; do
    dylib="$bundle/Contents/MacOS/libprojectM-4.dylib"
    [[ -f "$dylib" ]] || die "no libprojectM-4.dylib in $bundle/Contents/MacOS"
    lipo -create "$dylib" "$x64_dylib" -output "$dylib.universal"
    mv "$dylib.universal" "$dylib"
    for bin in "$dylib" "$bundle/Contents/MacOS/$product"; do
      archs="$(lipo -archs "$bin")"
      [[ "$archs" == *x86_64* && "$archs" == *arm64* ]] || die "$bin is not universal (has: $archs)"
    done
    # Ad-hoc: no Developer ID (D11). Re-signing seals the merged dylib into
    # the bundle.
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
  echo "$product $version"
  echo
  echo "MilkDAWp is a music visualizer: a plugin for your DAW and a standalone app, with"
  echo "about 9,800 bundled presets."
  echo
  case "$platform" in
    windows)
      echo "Installed:"
      echo "  Plugin   C:\\Program Files\\Common Files\\VST3\\MilkDAWp.vst3 (rescan plugins in your DAW)"
      echo "  App      C:\\Program Files\\MilkDAWp\\MilkDAWp.exe (Start menu: MilkDAWp)"
      echo "  Presets  C:\\ProgramData\\MilkDAWp\\Presets"
      echo
      echo "Uninstall from Settings > Apps. Your settings and ratings in %APPDATA%\\MilkDAWp stay."
      echo "The installer is not code-signed yet, so SmartScreen may warn (More info > Run"
      echo "anyway). Check the download against SHA256SUMS.txt on the release."
      ;;
    macos)
      echo "Installs:"
      echo "  App      /Applications/MilkDAWp.app"
      echo "  VST3     /Library/Audio/Plug-Ins/VST3/MilkDAWp.vst3"
      echo "  AU       /Library/Audio/Plug-Ins/Components/MilkDAWp.component"
      echo "  Presets  /Library/Application Support/MilkDAWp"
      echo
      echo "The installer isn't signed by an identified developer (no paid Apple Developer"
      echo "account), so macOS stops it the first time. Control-click (or right-click) the .pkg,"
      echo "choose Open, then Open again; or allow it in System Settings > Privacy & Security >"
      echo "Open Anyway. What it installs is ad-hoc signed and opens normally afterwards."
      echo "Check the download against SHA256SUMS.txt on the release first."
      echo
      echo "To uninstall, delete those four items."
      ;;
    linux)
      echo "The app: MilkDAWp-$version-x86_64.AppImage. Make it executable and run it:"
      echo "  chmod +x MilkDAWp-$version-x86_64.AppImage && ./MilkDAWp-$version-x86_64.AppImage"
      echo "The plugin: from this folder run ./install-vst3.sh, which copies MilkDAWp.vst3 to"
      echo "~/.vst3 and the presets to ~/.local/share/milkdawp. On Debian/Ubuntu the .deb"
      echo "installs both system-wide instead: sudo apt install ./milkdawp_*_amd64.deb"
      echo
      echo "Needs Ubuntu 22.04 or newer (glibc 2.35), OpenGL 3.3, ALSA or JACK, and X11 or"
      echo "XWayland."
      ;;
  esac
  echo
  echo "Bundled presets: \"Cream of the CrAWp\", the \"Cream of the Crop\" pack curated by"
  echo "Jason Fletcher (ISOSCELES) as packaged by the projectM project, each preset given"
  echo "MilkDAWp controls (.milkdawp), with the MilkDrop texture pack. Preset authors keep"
  echo "their copyright; see Presets/Cream of the CrAWp/LICENSE.md. To have a preset"
  echo "removed, open an issue. The \"MilkDAWp Originals\" folder in it holds MilkDAWp's own"
  echo "presets (AGPL-3.0-or-later), which use the media source and the beat."
  echo
  echo "Licences: MilkDAWp is AGPL-3.0-or-later (LICENSE). projectM is LGPL-2.1, shipped as"
  echo "a separate shared library you may replace (LICENSES/projectM-COPYRIGHT.txt; source"
  echo "in THIRD_PARTY_NOTICES.md). Source code: https://github.com/Blue-Kachina/MilkDAWp"
} > "$stage/README.txt"

# --- Installers --------------------------------------------------------------
outputs=()
case "$platform" in
  windows)
    iscc="${MILKDAWP_ISCC:-}"
    if [[ -z "$iscc" ]]; then
      for candidate in "$(command -v ISCC.exe 2>/dev/null || true)" "$(command -v iscc 2>/dev/null || true)" \
                       "${LOCALAPPDATA:-}/Programs/Inno Setup 6/ISCC.exe" "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
                       "/c/Program Files/Inno Setup 6/ISCC.exe"; do
        if [[ -n "$candidate" && -f "$candidate" ]]; then iscc="$candidate"; break; fi
      done
    fi
    [[ -n "$iscc" && -f "$iscc" ]] || die "Inno Setup's ISCC.exe not found; install Inno Setup 6 or set MILKDAWP_ISCC"

    redist="${MILKDAWP_VC_REDIST:-}"
    if [[ -z "$redist" ]]; then
      redist="$work/vc_redist.x64.exe"
      curl -fsSL -o "$redist" https://aka.ms/vs/17/release/vc_redist.x64.exe
    fi
    # The runtime must be at least the toolset that built us: 14.<minor>.
    vc_minor="$(printf '%s' "${VCToolsVersion:-14.40}" | cut -d. -f2)"

    base="$product-$version-$suffix-setup"
    winpath() { cygpath -w "$1" 2>/dev/null || printf '%s' "$1"; }
    # MSYS2_ARG_CONV_EXCL: Git Bash would otherwise rewrite "/D..." as paths.
    MSYS2_ARG_CONV_EXCL="*" "$iscc" -Q \
      "-DVersion=$version" "-DNumericVersion=$numeric" "-DStage=$(winpath "$stage")" \
      "-DContent=$(winpath "$content")" "-DRedist=$(winpath "$redist")" "-DVCMinor=$vc_minor" \
      "-DOutputDir=$(winpath "$out_dir")" "-DOutputBase=$base" \
      "$(winpath "$repo_root/packaging/windows/MilkDAWp.iss")" >&2
    outputs+=("$out_dir/$base.exe")

    # Symbols for the crash reporter's minidumps (4.10): the app's and the
    # VST3's .pdb (release presets link with /DEBUG). Both are MilkDAWp.pdb,
    # hence one folder each.
    sym="$work/$product-$version-$suffix-symbols"
    mkdir -p "$sym/app" "$sym/vst3"
    cp "$app_root"/*.pdb "$sym/app/" 2>/dev/null || true
    cp "$artefacts/VST3"/*.pdb "$sym/vst3/" 2>/dev/null || true
    if compgen -G "$sym/*/*.pdb" > /dev/null; then
      sym_archive="$out_dir/$(basename "$sym").zip"
      rm -f "$sym_archive"
      (cd "$work" && cmake -E tar cf "$sym_archive" --format=zip "$(basename "$sym")")
      outputs+=("$sym_archive")
    else
      log "warning: no .pdb files found, so no symbols archive"
    fi
    ;;

  macos)
    pkg="$out_dir/$product-$version-$suffix.pkg"
    rm -f "$pkg"
    bash "$repo_root/packaging/macos/build-pkg.sh" "$stage" "$content" "$version" "$numeric" "$pkg" >&2
    outputs+=("$pkg")
    ;;

  linux)
    appimage="$out_dir/$product-$version-x86_64.AppImage"
    rm -f "$appimage"
    bash "$repo_root/packaging/linux/build-appimage.sh" "$stage" "$content" "$version" "$appimage" >&2
    outputs+=("$appimage")

    # The VST3 tarball: bundle, presets and a per-user install script.
    tarname="$product-$version-$suffix-vst3"
    tardir="$work/$tarname"
    mkdir -p "$tardir/Content"
    cp -R "$stage/$product.vst3" "$stage/LICENSE" "$stage/LICENSES" "$stage/THIRD_PARTY_NOTICES.md" "$stage/README.txt" "$tardir/"
    cp -R "$content/Presets" "$content/Textures" "$tardir/Content/"
    find "$tardir/Content" -name '.milkdawp-*' -delete
    install -m 755 "$repo_root/packaging/linux/install-vst3.sh" "$tardir/install-vst3.sh"
    tarball="$out_dir/$tarname.tar.gz"
    tar -C "$work" -czf "$tarball" "$tarname"
    outputs+=("$tarball")

    # The file name keeps the label (GitHub may rename "~" in asset names);
    # the Debian version inside is 1.0.0~beta.1, which sorts before 1.0.0.
    deb="$out_dir/milkdawp_${version}_amd64.deb"
    rm -f "$deb"
    bash "$repo_root/packaging/linux/build-deb.sh" "$stage" "$content" "$version" "$deb" >&2
    outputs+=("$deb")
    ;;
esac

for f in "${outputs[@]}"; do
  [[ -f "$f" ]] || die "expected $f was not written"
  log "wrote $f ($(du -h "$f" | cut -f1))"
  echo "$f"
done
