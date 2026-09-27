# Contributing to MilkDAWp 2

This document covers the mechanics of building and contributing. For the
project's vision, architecture, and phased plan, see `development_roadmap.md`
-- read it first, especially §11 "Working agreements for AI-assisted
development", which applies to every contributor, human or agent.

## Three ways to develop (§4.10)

Cross-platform plugin development cannot be fully containerized: macOS
binaries need Apple's toolchain on macOS, DAW-grade Windows binaries need
MSVC, and containers have no GPU or audio devices. Pick the way that fits
the task:

### 1. Container (default for Linux work and for agents)

Everything that doesn't need a native GUI/audio toolchain -- the JUCE-free
core, `mdw-analyze`, headless render tests, lint, docs -- builds and tests
here. This is where most iteration happens.

```sh
# VS Code / CLion: reopen the folder in the devcontainer (.devcontainer/).
# Or manually, with the published image:
docker run --rm -it -v "$PWD:/workspace" ghcr.io/<owner>/milkdawp2-devcontainer:latest bash
cmake --preset dev-linux
cmake --build --preset dev-linux-Debug
ctest --test-dir build-linux -C Debug --output-on-failure
```

On a Linux host, pass `/dev/dri` and the X11 socket through (see the
commented-out `runArgs`/`mounts` in `.devcontainer/devcontainer.json`) to run
`mdw-view` with a real GPU instead of Mesa's software rasterizer.

**CLion note:** CLion's remote-dev CMake integration sometimes configures
with its own default profile (`cmake-build-debug`) instead of picking up
`CMakePresets.json`, which drops the vcpkg toolchain/triplet and makes
`MILKDAWP_WITH_PROJECTM=ON` fail to find projectM. The top of
`CMakeLists.txt` now falls back to the same `VCPKG_ROOT`/triplet/overlay
settings the presets use whenever they weren't already supplied, so a bare
CLion configure works too. If you'd rather build into the same directory as
the CLI (`build-linux`) and match the preset's build type exactly, open
Settings | Build, Execution, Deployment | CMake and add a profile using the
`dev-linux` preset.

### 2. CI as the Windows and macOS build farm

Every push builds VST3/AU/app artifacts and `pluginval` reports for all
three platforms (`.github/workflows/ci.yml`). "Push, wait, download the
bundle" is a legitimate day-to-day loop for a project this size --
especially for anything AU-, MSVC-, or installer-specific that can't run in
the container. Say so in the PR when a change needs a CI artifact rather
than a local build, so the reviewer knows what to pull.

### 3. Native, when you want it

```sh
# macOS
scripts/bootstrap.sh --doctor   # see what's missing
scripts/bootstrap.sh            # install it (Homebrew: cmake, ninja; Xcode CLT)
export VCPKG_ROOT=/path/to/vcpkg   # vcpkg stays repo-local or wherever you keep it
cmake --preset dev-mac
cmake --build --preset dev-mac-Debug
```

```powershell
# Windows
powershell -File scripts\bootstrap.ps1 -Doctor   # see what's missing
powershell -File scripts\bootstrap.ps1           # install it (winget: VS Build Tools, CMake, Ninja)
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset dev-win
cmake --build --preset dev-win-Debug
```

Both scripts are idempotent and read minimum versions from `toolchain.json`;
`--doctor`/`-Doctor` only reports, never installs, and exits non-zero on any
gap.

After changing the plugin, run pluginval against the VST3 you just built
(Windows; it downloads the pinned version on first use):

```powershell
pwsh scripts\pluginval.ps1             # strictness 10, with the editor tests
pwsh scripts\pluginval.ps1 -Repeat 5   # five runs in random order, for threading changes
```

CI runs the same script without the editor tests, since its runners have no GPU.

## Threading rules (§4.2)

This is the rule that matters most and the one most likely to be violated by
an innocuous-looking change. There are five threads; each one has a job and
a short list of things it must never do:

| Thread | Owner | Does | Never does |
|---|---|---|---|
| Audio (host callback / device callback) | shell | copies PCM into `AudioRing`, writes transport info into an atomic snapshot | allocate, lock, log, call the message thread |
| Analysis | core, driven by engine | pulls from `AudioRing` in hops, runs FFT/onsets/tempo, updates `BeatClock`, ticks `TransitionScheduler` | touch GL or UI |
| Render (GL) | engine | feeds PCM to projectM, executes due `TransitionRequest`s, renders to FBO, presents to surfaces | parse presets, block on I/O |
| Preset I/O | engine | scans folders, reads/pre-validates preset files, warms the next preset | touch GL |
| Message (UI) | shell | widgets, parameter attachments, status polling via a lock-free snapshot | block waiting on any other thread |

Communication crosses threads only as plain-old-data messages over
SPSC/MPSC lock-free queues (`core/include/milkdawp/core/Messages.h` once it
exists, Phase 1.2). Strings cross threads only as interned preset IDs, never
as `juce::String`.

Anything that runs on the audio callback path is marked
`[[clang::nonblocking]]` and covered by the Clang RealtimeSanitizer CI job
(`ci-linux-rtsan` preset). If you think a function on that path needs to
allocate, lock, or log: it doesn't -- restructure so the audio thread only
touches lock-free rings, and do the real work on the analysis thread.

## Definition of done (§11)

1. Code compiles warning-free on the platform you're on; CI green on all
   three.
2. Tests exist for the behaviour (core: Catch2 unit tests; engine: headless
   smoke test; shells: `pluginval` or the DAW checklist updated).
3. The threading rules above are respected.
4. The relevant checkbox in `development_roadmap.md` is ticked in the same
   commit, with a one-line note if the approach changed.
5. If a decision in `development_roadmap.md` §5 was touched, an ADR was
   added or amended under `docs/adr/`.

## Boundaries (§11)

- No JUCE or GL includes in `core/`. If you think you need one, write down
  why in the PR and stop.
- Don't add a third way to do something that already has two. Delete one
  first.
- Don't disable, skip, or loosen a test or a threshold in
  `fixtures/thresholds.json` to get green. Lowering a threshold is a
  decision for Matthew with the metric report attached.
- Don't change plugin identity codes, bundle IDs, or the state schema
  version without an ADR (see `docs/adr/0001-plugin-identity.md`).
- Don't add UI that exists in only one shell -- drawer, output window, and
  settings are `milkdawp_ui` components composed by both shells.

## When to stop and ask

- Any §5 decision marked **Open** (currently D11 signing accounts, D12
  bundled preset licensing) that the task depends on.
- A platform behaviour that contradicts an ADR -- write up what you saw
  first.
- Anything involving accounts, certificates, or licences.
- End of each phase: post the hand-test list from `development_roadmap.md`
  and what you'd like checked.

## Formatting and linting

`.clang-format` and `.clang-tidy` are committed at the repo root.
`scripts/hooks/pre-commit` rejects a commit if a staged C/C++ file isn't
clang-format-clean; install it with:

```sh
git config core.hooksPath scripts/hooks
```

New source files we own get an SPDX header -- see
`docs/spdx-header-template.txt`.

## Commit hygiene

Small commits, imperative subject, body says *why*. Reference the roadmap
item in the subject, e.g. `[1.6] Add BeatClock phase tracker`.
