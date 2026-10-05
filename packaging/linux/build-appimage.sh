#!/usr/bin/env bash
# packaging/linux/build-appimage.sh -- the app as an AppImage (6.4).
#
#   build-appimage.sh <stage> <content> <version-label> <out.AppImage>
#
# <stage>/MilkDAWp holds the app binary and libprojectM-4.so
# (scripts/release/package.sh). Needs appimagetool and its runtime, pinned
# in toolchain.json; MILKDAWP_APPIMAGETOOL / MILKDAWP_APPIMAGE_RUNTIME name
# them, or they are downloaded and checked here.
#
# No libraries are bundled: the app needs only OpenGL/EGL (from the user's
# driver, which must not be bundled), ALSA, fontconfig, freetype and the
# C/C++ runtime, all base libraries on any desktop, and JUCE loads X11 at
# runtime. The release builds on Ubuntu 22.04 (glibc 2.35, D9's floor), so
# the AppImage runs there and on anything newer.
#
#   AppDir/AppRun                      -> usr/bin/MilkDAWp
#   AppDir/usr/bin/{MilkDAWp,libprojectM-4.so}
#   AppDir/usr/share/milkdawp/{Presets,Textures}   (engine::BundledContent)
#   AppDir/usr/share/{applications,icons,mime}     desktop integration
#   AppDir/milkdawp.desktop, milkdawp.png, .DirIcon

set -euo pipefail

stage="${1:?stage folder}"
content="${2:?content folder}"
version="${3:?version label}"
out="${4:?output .AppImage}"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# fetch_pinned <toolchain key> <dest>: download and check a pinned tool.
fetch_pinned() {
  local key="$1" dest="$2" url sha
  url="$(python3 -c "import json,sys; print(json.load(open('$repo/toolchain.json'))['linux']['$key']['url'])")"
  sha="$(python3 -c "import json,sys; print(json.load(open('$repo/toolchain.json'))['linux']['$key']['sha256'])")"
  curl -fsSL -o "$dest" "$url"
  echo "$sha  $dest" | sha256sum -c - > /dev/null || { echo "build-appimage.sh: $key hash mismatch ($url)" >&2; exit 1; }
  chmod +x "$dest"
}

tool="${MILKDAWP_APPIMAGETOOL:-}"
runtime="${MILKDAWP_APPIMAGE_RUNTIME:-}"
if [[ -z "$tool" ]]; then tool="$work/appimagetool"; fetch_pinned appimagetool "$tool"; fi
if [[ -z "$runtime" ]]; then runtime="$work/runtime"; fetch_pinned appimageRuntime "$runtime"; fi

app="$work/MilkDAWp.AppDir"
mkdir -p "$app/usr/bin" "$app/usr/share/milkdawp" "$app/usr/share/applications" \
  "$app/usr/share/icons/hicolor/256x256/apps" "$app/usr/share/mime/packages" "$app/usr/share/doc/milkdawp"
cp "$stage/MilkDAWp/MilkDAWp" "$stage/MilkDAWp"/libprojectM-4*.so "$app/usr/bin/"
cp -R "$content/Presets" "$content/Textures" "$app/usr/share/milkdawp/"
find "$app/usr/share/milkdawp" -name '.milkdawp-*' -delete
cp "$stage/LICENSE" "$stage/THIRD_PARTY_NOTICES.md" "$app/usr/share/doc/milkdawp/"
cp -R "$stage/LICENSES" "$app/usr/share/doc/milkdawp/"

cp "$here/milkdawp.desktop" "$app/usr/share/applications/"
cp "$here/milkdawp-mime.xml" "$app/usr/share/mime/packages/milkdawp.xml"
cp "$here/milkdawp.desktop" "$app/"
cp "$repo/resources/icon-256.png" "$app/usr/share/icons/hicolor/256x256/apps/milkdawp.png"
cp "$app/usr/share/icons/hicolor/256x256/apps/milkdawp.png" "$app/milkdawp.png"
ln -s milkdawp.png "$app/.DirIcon"
install -m 755 "$here/AppRun" "$app/AppRun"

# No FUSE inside containers: run the tool from its extracted self.
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 VERSION="$version" \
  "$tool" --no-appstream --runtime-file "$runtime" "$app" "$out"
echo "$out"
