# ADR-0002: Plugin formats and dependency management

Status: Accepted (D4) / Recommended (D2). D4 amended by ADR-0008: projectM now comes from a
repo-local vcpkg overlay port pinned to an upstream 4.2 commit, not the registry port.

## Context

v1 built VST3 (and optionally Standalone) via a vcpkg-provided JUCE 8.0.7.
v2 targets JUCE 9.x from the start (§4.11), but the vcpkg `juce` port lags at
8.0.7 with no timeline to catch up, while JUCE's own build system is designed
around `add_subdirectory`/`FetchContent` rather than a package manager.
projectM remains a good fit for vcpkg: it is a C API with few consumers of
its own CMake target shape.

## Decision

- **Formats (D2):** VST3 + AU + a Standalone wrapper for 1.0. CLAP (via
  `clap-juce-extensions`) and LV2 are post-1.0. No AAX (Avid signing program
  is out of scope, §1 non-goals).
- **Dependencies (D4):** JUCE 9.x via CMake `FetchContent`, pinned to a
  release tag **and** its commit hash (`cmake/FetchJuce.cmake`). Everything
  else -- projectM 4.x included -- via vcpkg manifest mode with a pinned
  registry baseline and the custom `*-dynamic` triplets in `triplets/`
  (LGPL-compliant dynamic linking for projectM).

## Consequences

- JUCE upgrades are a branch that bumps both the tag and the commit hash,
  runs the full CI matrix and the DAW checklist, and reviews JUCE's
  `BREAKING_CHANGES.md` (see the risk in §10).
- vcpkg baseline bumps are the normal path for a projectM (or other
  vcpkg-provided dependency) upgrade.
- `vcpkg.json` never lists `juce`; adding it back would silently reintroduce
  8.0.7 and needs an ADR.
