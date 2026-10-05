# Releasing

How to cut a MilkDAWp release (development_roadmap.md 6.5). The workflow is
`.github/workflows/release.yml`; packaging is `scripts/release/package.sh`.

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

## What the workflow does

| Job | Runs on | Does |
|---|---|---|
| `version` | ubuntu | Reads the tag, checks it against `project(VERSION)` |
| `build-native` | windows, macos | `release-win` / `release-mac` preset with the real identity (D1), pluginval at strictness 5 (Windows), x86_64 projectM merged in and ad-hoc signing (macOS), package |
| `build-linux` | devcontainer image | `release-linux`, pluginval at strictness 5, package |
| `publish` | ubuntu | `SHA256SUMS.txt`, build provenance attestations, GitHub Release with generated notes |

Each archive holds the VST3, the app, the licences (including projectM's
LGPL notice) and a `README.txt` with install steps. Windows also gets a
`-symbols.zip` of the `.pdb` files for reading crash minidumps; it isn't
needed to run anything.

## Try it without publishing

Actions > Release > Run workflow builds and packages everything and leaves the
archives as a workflow artifact (`release-dry-run-<label>`). Nothing is tagged,
attested or published. Use this after changing the workflow or `package.sh`.

Locally, after `cmake --build --preset release-<platform>` (macOS needs an
x86_64 projectM; see `MILKDAWP_PROJECTM_X64_DYLIB` in `package.sh`):

```sh
scripts/release/package.sh windows build-release-win 2.0.0-beta.1 dist
```

## Checking a download

Cutting a release needs only `git`: the `gh` calls in the workflow run on
GitHub's runners, where the GitHub CLI is preinstalled. Checking an
attestation locally does need it (Windows: `winget install --id GitHub.cli`,
then `gh auth login`). The checksum check needs nothing extra.

```sh
sha256sum -c SHA256SUMS.txt --ignore-missing
gh attestation verify MilkDAWp-2.0.0-beta.1-windows-x64.zip --repo Blue-Kachina/MilkDAWp2
```

## Not yet done

- Installers (6.2-6.4) replace the zipped bundles.
- Windows signing through SignPath (6.10) runs before packaging once accepted.
- The Linux build runs in the Ubuntu 24.04 devcontainer image (glibc 2.39),
  above D9's Ubuntu 22.04 / glibc 2.35 floor. The AppImage (6.4) needs an
  older build base.
- The repo URLs change in 6.9 (`MilkDAWp2` to `MilkDAWp`).
