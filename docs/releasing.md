# Releasing

How to cut a MilkDAWp release (development_roadmap.md 6.2-6.5). The workflow
is `.github/workflows/release.yml`; packaging is `scripts/release/package.sh`
and `packaging/`.

## Cut a release

1. Make sure CI is green on the commit you want to release.
2. If the version's numbers change, bump `project(MilkDAWp VERSION x.y.z)` in
   `CMakeLists.txt` (and `version` in `vcpkg.json`) and commit.
3. Run through `docs/daw-checklist.md` (before each beta and release, §8).
4. Tag and push:

   ```sh
   git tag v2.0.0-beta.1
   git push origin v2.0.0-beta.1
   ```

The tag's numbers must equal `project(VERSION)`, or the workflow stops in its
first job. A suffix (`-beta.1`, `-rc.1`) makes it a pre-release, and the app
and plugin show the full label (`MILKDAWP_VERSION_LABEL`).

## What a release contains

| Platform | File | What it is |
|---|---|---|
| Windows | `MilkDAWp-<v>-windows-x64-setup.exe` | Inno Setup installer (`packaging/windows/MilkDAWp.iss`): VST3 to `Common Files\VST3`, app to `Program Files\MilkDAWp`, presets to `C:\ProgramData\MilkDAWp`, the VC++ runtime when missing or too old, Start menu and optional desktop shortcut, optional `.milk` association, uninstaller |
| Windows | `MilkDAWp-<v>-windows-x64-symbols.zip` | `.pdb` files for reading crash minidumps; not needed to run anything |
| macOS | `MilkDAWp-<v>-macos-universal.pkg` | `packaging/macos/build-pkg.sh`: app, VST3, AU and presets, each a choice; universal, ad-hoc signed bundles, unsigned package (D11) |
| Linux | `MilkDAWp-<v>-x86_64.AppImage` | The app with projectM and the presets inside (`packaging/linux/build-appimage.sh`) |
| Linux | `MilkDAWp-<v>-linux-x64-vst3.tar.gz` | The VST3, the presets and `install-vst3.sh` (to `~/.vst3` and `~/.local/share/milkdawp`) |
| Linux | `milkdawp_<v>_amd64.deb` | App, VST3 (`/usr/lib/vst3`) and presets (`/usr/share/milkdawp`) system-wide (`packaging/linux/build-deb.sh`) |
| all | `SHA256SUMS.txt` | Checksums of everything above |

The presets always go to a location `engine::BundledContent` searches, so the
app and every plugin instance find them without setup.

## What the workflow does

| Job | Runs on | Does |
|---|---|---|
| `version` | ubuntu | Reads the tag, checks it against `project(VERSION)` |
| `build-native` | windows, macos | `release-win` / `release-mac` preset with the real identity (D1); pluginval at strictness 5 on the built VST3 (Windows); x86_64 projectM merged in and ad-hoc signing (macOS); installers; **smoke test** |
| `build-linux` | `ubuntu:22.04` container | `scripts/release/linux-setup.sh` (GCC 12, CMake, vcpkg), `release-linux`, packages, **smoke test** |
| `publish` | ubuntu | `SHA256SUMS.txt`, build provenance attestations, GitHub Release with generated notes |

The smoke tests (`scripts/release/smoke-test-{windows.ps1,macos.sh,linux.sh}`)
install what was just packaged on the job's throwaway runner and use it:

- **Windows:** silent install, every file and registry entry checked, pluginval
  (strictness 5, projectM required) on the *installed* VST3, the installed app
  started, silent uninstall, nothing left behind.
- **macOS:** `installer -pkg`, every binary universal and every signature valid,
  `auval` on the installed AU, the app started, everything removed.
- **Linux:** no binary needs glibc newer than 2.35; the AppImage's contents
  and the app started from it (Xvfb, Mesa software GL); pluginval on the
  tarball's VST3; the `.deb` installed with apt (so its Depends resolve), the
  installed app started, removed cleanly.

**Never run the smoke tests on your own machine:** they install the real
identity, which replaces v1 in every host.

## Why Linux builds on 22.04

D9's floor is Ubuntu 22.04 / glibc 2.35. A binary only runs on systems at
least as new as the one that built it, and the devcontainer image is 24.04:
its builds need glibc 2.38 and GCC 13's libstdc++. So the release job builds
in a plain `ubuntu:22.04` with GCC 12 (22.04's own libstdc++, nothing newer
needed at runtime). CI keeps using the devcontainer image.

The app and plugin link only OpenGL/EGL, ALSA, fontconfig, freetype and the
C/C++ runtime (JUCE loads X11 at runtime), so the AppImage bundles no
libraries: those come from the user's system, and GL must come from their
driver anyway.

## Try it without publishing

Actions > Release > Run workflow builds, packages and smoke-tests everything
and leaves the files as a workflow artifact (`release-dry-run-<label>`).
Nothing is tagged, attested or published. Use this after changing the
workflow, `package.sh` or anything in `packaging/`.

Locally:

- **Windows** (needs Inno Setup: `winget install JRSoftware.InnoSetup`), after
  `cmake --build --preset release-win`:

  ```sh
  scripts/release/package.sh windows build-release-win 2.0.0-beta.1 dist
  ```

  Don't run the resulting installer on a machine with v1 you want to keep.

- **Linux**, the same steps the release job runs, in Docker:

  ```sh
  docker run --rm -v "$PWD:/src" -w /src ubuntu:22.04 bash -c '
    eval "$(bash scripts/release/linux-setup.sh | sed "s/^/export /")" &&
    cmake --preset release-linux -B /tmp/rel -DVCPKG_INSTALLED_DIR=/tmp/rel/vcpkg_installed &&
    cmake --build /tmp/rel --target milkdawp_plugin_VST3 milkdawp_app &&
    bash scripts/release/package.sh linux /tmp/rel 2.0.0-beta.1 /src/dist &&
    bash scripts/release/smoke-test-linux.sh /src/dist 2.0.0-beta.1'
  ```

- **macOS:** only on GitHub (no Mac here). The x86_64 projectM comes from
  `vcpkg install --triplet x64-osx-dynamic` (`MILKDAWP_PROJECTM_X64_DYLIB` in
  `package.sh`).

## Checking a download

Cutting a release needs only `git`: the `gh` calls in the workflow run on
GitHub's runners, where the GitHub CLI is preinstalled. Checking an
attestation locally does need it (Windows: `winget install --id GitHub.cli`,
then `gh auth login`). The checksum check needs nothing extra.

```sh
sha256sum -c SHA256SUMS.txt --ignore-missing
gh attestation verify MilkDAWp-2.0.0-beta.1-windows-x64-setup.exe --repo Blue-Kachina/MilkDAWp2
```

## The app icon

`resources/icon.png` (1024 px) and `icon-256.png` are v1's wordmark on a dark
tile, made by `resources/make-icon.ps1` from the v1 repository's
`resources/images/MilkDAWp Logo Transparent.png`. JUCE builds the Windows
`.ico` and macOS `.icns` from `icon.png`; the installers and Linux packages use
those and `icon-256.png`. A square mark would read better at 16-32 px.

## Not yet done

- Windows signing through SignPath (6.10) runs before packaging once accepted.
- The macOS package is unsigned and not notarized (D11).
- The repo URLs change in 6.9 (`MilkDAWp2` to `MilkDAWp`).
