#!/usr/bin/env bash
# packaging/linux/build-deb.sh -- the .deb (6.4, the stretch goal).
#
#   build-deb.sh <stage> <content> <version-label> <out.deb>
#
# Installs the app, the VST3 and the presets system-wide, for Debian/Ubuntu
# (built on 22.04, so 22.04 and newer):
#
#   /usr/lib/milkdawp/{MilkDAWp,libprojectM-4.so}   the app finds projectM beside itself
#   /usr/bin/milkdawp -> ../lib/milkdawp/MilkDAWp
#   /usr/lib/vst3/MilkDAWp.vst3                       where Linux hosts look for VST3s
#   /usr/share/milkdawp/{Presets,Textures}            engine::BundledContent
#   /usr/share/{applications,icons,mime,doc}          desktop integration, licences
#
# Dependencies are the libraries the binaries link (readelf NEEDED) plus the
# X11 libraries JUCE loads at runtime.

set -euo pipefail

stage="${1:?stage folder}"
content="${2:?content folder}"
version="${3:?version label}"
out="${4:?output .deb}"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
root="$work/root"

# Debian versions sort "~" before anything: 1.0.0~beta.1 < 1.0.0.
debversion="$(printf '%s' "$version" | sed 's/-/~/')"

mkdir -p "$root/DEBIAN" "$root/usr/lib/milkdawp" "$root/usr/bin" "$root/usr/lib/vst3" "$root/usr/share/milkdawp" \
  "$root/usr/share/applications" "$root/usr/share/icons/hicolor/256x256/apps" "$root/usr/share/mime/packages" \
  "$root/usr/share/doc/milkdawp"
install -m 755 "$stage/MilkDAWp/MilkDAWp" "$root/usr/lib/milkdawp/"
install -m 644 "$stage/MilkDAWp"/libprojectM-4*.so "$root/usr/lib/milkdawp/"
ln -s ../lib/milkdawp/MilkDAWp "$root/usr/bin/milkdawp"
cp -R "$stage/MilkDAWp.vst3" "$root/usr/lib/vst3/"
cp -R "$content/Presets" "$content/Textures" "$root/usr/share/milkdawp/"
find "$root/usr/share/milkdawp" -name '.milkdawp-*' -delete
install -m 644 "$here/milkdawp.desktop" "$root/usr/share/applications/milkdawp.desktop"
install -m 644 "$here/milkdawp-mime.xml" "$root/usr/share/mime/packages/milkdawp.xml"
install -m 644 "$repo/resources/icon-256.png" "$root/usr/share/icons/hicolor/256x256/apps/milkdawp.png"
cp "$stage/THIRD_PARTY_NOTICES.md" "$root/usr/share/doc/milkdawp/"
cp -R "$stage/LICENSES" "$root/usr/share/doc/milkdawp/"
cat > "$root/usr/share/doc/milkdawp/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: MilkDAWp
Source: https://github.com/Blue-Kachina/MilkDAWp2

Files: *
License: AGPL-3.0-or-later
 See /usr/share/doc/milkdawp/LICENSES/AGPL-3.0-or-later.txt. projectM
 (usr/lib/milkdawp/libprojectM-4.so) is LGPL-2.1: LICENSES/projectM-COPYRIGHT.txt.
 The bundled presets and textures: THIRD_PARTY_NOTICES.md.
EOF
find "$root" -type d -exec chmod 755 {} +
find "$root/usr/share" "$root/usr/lib/vst3" -type f -exec chmod 644 {} +
chmod 755 "$root/usr/lib/vst3/MilkDAWp.vst3/Contents/x86_64-linux/MilkDAWp.so"

size_kb="$(du -sk "$root/usr" | cut -f1)"
cat > "$root/DEBIAN/control" <<EOF
Package: milkdawp
Version: $debversion
Section: sound
Priority: optional
Architecture: amd64
Maintainer: Otitis Media <https://github.com/Blue-Kachina/MilkDAWp2/issues>
Installed-Size: $size_kb
Depends: libc6 (>= 2.35), libstdc++6 (>= 12), libgcc-s1, libasound2 | libasound2t64, libfreetype6, libfontconfig1, libgl1, libegl1, libopengl0, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1
Recommends: shared-mime-info, desktop-file-utils
Homepage: https://github.com/Blue-Kachina/MilkDAWp2
Description: Music visualizer: VST3 plugin and standalone app
 MilkDAWp shows MilkDrop presets (rendered by projectM) that change in time
 with the music, as a VST3 plugin in your DAW or as a standalone app
 listening to an audio input. Includes the ~9,800 "Cream of the Crop" presets.
EOF

dpkg-deb --root-owner-group --build "$root" "$out" > /dev/null
echo "$out"
