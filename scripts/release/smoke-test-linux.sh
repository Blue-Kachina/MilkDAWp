#!/usr/bin/env bash
# scripts/release/smoke-test-linux.sh -- checks the Linux release (6.4) the
# way a user meets it. Run as root in a throwaway ubuntu:22.04 (the release
# job's container, or docker locally), after package.sh:
#
#   smoke-test-linux.sh <dist> <version-label>
#
#   1. no binary needs a glibc newer than 2.35 (D9's floor);
#   2. the AppImage holds the app, projectM and the presets, and the app
#      starts from it (under Xvfb, with Mesa's software GL) without crashing;
#   3. pluginval (strictness 5) passes on the tarball's VST3, with projectM;
#   4. the .deb installs with apt (resolving its Depends), puts everything in
#      place, the installed app starts, and it removes cleanly.

set -euo pipefail

dist="$(cd "${1:?dist folder}" && pwd)"
version="${2:?version label}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export DEBIAN_FRONTEND=noninteractive

fail() { echo "smoke-test-linux: FAIL: $*" >&2; exit 1; }
pass() { echo "smoke-test-linux: ok: $*"; }

appimage="$dist/MilkDAWp-$version-x86_64.AppImage"
tarball="$dist/MilkDAWp-$version-linux-x64-vst3.tar.gz"
deb="$dist/milkdawp_${version}_amd64.deb"
for f in "$appimage" "$tarball" "$deb"; do [[ -f "$f" ]] || fail "missing $f"; done

apt-get update -qq
apt-get install -y -qq --no-install-recommends xvfb xauth libgl1-mesa-dri libegl1 libgl1 binutils curl unzip > /dev/null

# starts_ok <command...>: runs the app for 8 s under Xvfb. Killed by the
# timeout (124) means it was still running: good. Anything else is a crash
# or an early exit.
starts_ok() {
  local status=0
  LIBGL_ALWAYS_SOFTWARE=1 timeout 8 xvfb-run -a "$@" > "$work/run.log" 2>&1 || status=$?
  if [[ $status -ne 124 ]]; then
    cat "$work/run.log" >&2
    return 1
  fi
}

# 1. glibc floor.
cd "$work"
APPIMAGE_EXTRACT_AND_RUN=1 "$appimage" --appimage-extract > /dev/null
app="$work/squashfs-root"
newest="$(for f in "$app/usr/bin/MilkDAWp" "$app"/usr/bin/libprojectM-4*.so; do
  objdump -T "$f" | grep -o 'GLIBC_[0-9.]*' ; done | sort -Vu | tail -n 1)"
[[ "$(printf '%s\nGLIBC_2.35\n' "$newest" | sort -V | tail -n 1)" == GLIBC_2.35 ]] \
  || fail "binaries need $newest, newer than D9's GLIBC_2.35"
pass "binaries need at most $newest"

# 2. The AppImage's contents, and the app starting from it.
[[ -x "$app/usr/bin/MilkDAWp" ]] || fail "no usr/bin/MilkDAWp in the AppImage"
compgen -G "$app/usr/bin/libprojectM-4*.so" > /dev/null || fail "no libprojectM-4.so in the AppImage"
[[ -d "$app/usr/share/milkdawp/Presets/Cream of the CrAWp" ]] || fail "no presets in the AppImage"
count="$(find "$app/usr/share/milkdawp/Presets" -name '*.milkdawp' | wc -l)"
[[ "$count" -gt 9000 ]] || fail "only $count presets in the AppImage"
pass "AppImage holds the app, projectM and $count presets"
chmod +x "$appimage"
HOME="$work/home" APPIMAGE_EXTRACT_AND_RUN=1 starts_ok "$appimage" || fail "the AppImage's app exited or crashed"
pass "the AppImage's app starts"

# 3. pluginval on the tarball's VST3, with projectM required.
tar -C "$work" -xzf "$tarball"
vst3="$work/MilkDAWp-$version-linux-x64-vst3/MilkDAWp.vst3"
[[ -f "$vst3/Contents/x86_64-linux/MilkDAWp.so" && -f "$vst3/Contents/x86_64-linux/libprojectM-4.so" ]] \
  || fail "the tarball's VST3 is incomplete"
url="https://github.com/Tracktion/pluginval/releases/download/v$(python3 -c "import json; print(json.load(open('$repo/toolchain.json'))['pluginval']['version'])")/pluginval_Linux.zip"
sha="$(python3 -c "import json; print(json.load(open('$repo/toolchain.json'))['pluginval']['sha256Linux'])")"
curl -fsSL -o "$work/pluginval.zip" "$url"
echo "$sha  $work/pluginval.zip" | sha256sum -c - > /dev/null || fail "pluginval download hash mismatch"
unzip -q "$work/pluginval.zip" -d "$work/pluginval"
MILKDAWP_REQUIRE_PROJECTM=1 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a "$work/pluginval/pluginval" \
  --strictness-level 5 --timeout-ms 900000 --skip-gui-tests --validate "$vst3" > "$work/pluginval.log" 2>&1 \
  || { tail -40 "$work/pluginval.log" >&2; fail "pluginval"; }
pass "pluginval (strictness 5) on the tarball's VST3"

# 4. The .deb: install (apt resolves Depends), check, run, remove.
apt-get install -y -qq "$deb" > /dev/null || fail "apt couldn't install the .deb"
for path in /usr/bin/milkdawp /usr/lib/milkdawp/MilkDAWp /usr/lib/vst3/MilkDAWp.vst3/Contents/x86_64-linux/MilkDAWp.so \
            "/usr/share/milkdawp/Presets/Cream of the CrAWp" /usr/share/applications/milkdawp.desktop \
            /usr/share/icons/hicolor/256x256/apps/milkdawp.png; do
  [[ -e "$path" ]] || fail ".deb didn't install $path"
done
compgen -G "/usr/lib/milkdawp/libprojectM-4*.so" > /dev/null || fail ".deb didn't install libprojectM-4.so"
desktop-file-validate /usr/share/applications/milkdawp.desktop 2>/dev/null || true
pass ".deb installs everything ($(dpkg-query -W -f='${Version}' milkdawp))"
HOME="$work/home" starts_ok milkdawp || fail "the installed app exited or crashed"
pass "the installed app starts"
apt-get remove -y -qq milkdawp > /dev/null
[[ ! -e /usr/lib/milkdawp && ! -e /usr/lib/vst3/MilkDAWp.vst3 && ! -e /usr/share/milkdawp ]] || fail ".deb left files behind"
pass ".deb removes cleanly"

echo "smoke-test-linux: all passed"
