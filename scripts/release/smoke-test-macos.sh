#!/usr/bin/env bash
# scripts/release/smoke-test-macos.sh -- installs, checks and removes the
# macOS release (6.3). For the release workflow's throwaway runner only: the
# package uses the real identity (D1).
#
#   smoke-test-macos.sh <dist> <version-label>
#
#   1. `installer` puts the app, VST3, AU and presets where the package says;
#   2. every binary is universal (x86_64 + arm64) and every bundle's ad-hoc
#      signature verifies;
#   3. auval validates the installed AU;
#   4. the installed app starts and keeps running;
#   5. removing the files and the package receipts leaves nothing behind.

set -euo pipefail

dist="$(cd "${1:?dist folder}" && pwd)"
version="${2:?version label}"
pkg="$dist/MilkDAWp-$version-macos-universal.pkg"

fail() { echo "smoke-test-macos: FAIL: $*" >&2; exit 1; }
pass() { echo "smoke-test-macos: ok: $*"; }

[[ -f "$pkg" ]] || fail "missing $pkg"
app="/Applications/MilkDAWp.app"
vst3="/Library/Audio/Plug-Ins/VST3/MilkDAWp.vst3"
au="/Library/Audio/Plug-Ins/Components/MilkDAWp.component"
content="/Library/Application Support/MilkDAWp"

# 1. Install.
sudo installer -pkg "$pkg" -target / || fail "installer"
for path in "$app" "$vst3" "$au" "$content/Presets/Cream of the CrAWp" "$content/Textures/worms.jpg"; do
  [[ -e "$path" ]] || fail "not installed: $path"
done
presets="$(find "$content/Presets" -name '*.milk' | wc -l | tr -d ' ')"
[[ "$presets" -gt 9000 ]] || fail "only $presets presets installed"
pass "installed ($presets presets)"

# 2. Universal and signed.
for bundle in "$app" "$vst3" "$au"; do
  for bin in "$bundle/Contents/MacOS/MilkDAWp" "$bundle/Contents/MacOS/libprojectM-4.dylib"; do
    archs="$(lipo -archs "$bin")"
    [[ "$archs" == *x86_64* && "$archs" == *arm64* ]] || fail "$bin is not universal ($archs)"
  done
  codesign --verify --deep --strict "$bundle" || fail "signature of $bundle"
done
pass "universal binaries, signatures verify"

# 3. auval: aufx (effect), subtype = plugin code, manufacturer OMda.
killall -9 AudioComponentRegistrar 2> /dev/null || true # pick up the new component
auval -v aufx Mlkw OMda > /tmp/auval.log 2>&1 || { tail -40 /tmp/auval.log; fail "auval"; }
grep -q "AU VALIDATION SUCCEEDED" /tmp/auval.log || { tail -40 /tmp/auval.log; fail "auval didn't report success"; }
pass "auval"

# 4. The installed app starts and keeps running.
"$app/Contents/MacOS/MilkDAWp" > /tmp/milkdawp-app.log 2>&1 &
pid=$!
sleep 8
kill -0 "$pid" 2> /dev/null || { cat /tmp/milkdawp-app.log; fail "the installed app exited"; }
kill "$pid"
pass "the installed app starts"

# 5. Remove (a .pkg has no uninstaller; README.txt says to delete these).
sudo rm -rf "$app" "$vst3" "$au" "$content"
for id in app vst3 au presets; do sudo pkgutil --forget "com.otitismedia.MilkDAWp.pkg.$id" > /dev/null || true; done
pass "removed"
echo "smoke-test-macos: all passed"
