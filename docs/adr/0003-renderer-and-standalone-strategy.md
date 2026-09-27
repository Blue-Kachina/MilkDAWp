# ADR-0003: Renderer location and standalone shell delivery

Status: Recommended

## Context

v1 ran projectM on a thread inside the plugin process, described (misleadingly)
as "a separate process". A true out-of-process renderer would isolate hosts
from GL driver crashes and let the standalone app double as that renderer
("Link mode", §4.6), but it also makes embedding a live preview inside the
plugin editor hard on macOS (cross-process view embedding isn't supported
there). Delivering a standalone app from scratch (D8) is also more work than
reusing what JUCE already provides for early testing.

## Decision

- **Renderer location (D3):** in-process engine thread for 1.0. The engine
  boundary -- POD messages plus an audio ring (§4.2) -- is designed so an
  out-of-process transport can replace the in-process queues later without
  touching the shells. This is explicitly a post-1.0 item, not MVP work.
- **Standalone shell (D8):** Phase 3 enables JUCE's `Standalone` plugin
  format to get an app early, sharing the plugin's engine and UI code. Phase 4
  replaces it with a real `juce_add_gui_app` shell (`milkdawp_app`) built on
  `milkdawp_ui`, and Phase 4.11 retires the Standalone wrapper (or keeps it
  behind a CMake option as a dev convenience).

## Consequences

- Hosts that dislike in-process OpenGL (some macOS hosts, sandboxed AUv3,
  which is out of scope) remain a risk until an out-of-process renderer is
  built; `pluginval` and the DAW matrix (Phase 3) are the near-term
  mitigation, not this ADR.
- `milkdawp_app`'s CMake target exists from Phase 0.2 but is off by default
  (`MILKDAWP_BUILD_APP=OFF`) until Phase 4 makes it real.
  Update (2026-09-27): Phase 4.1 made it real, and it is now on by default
  (`MILKDAWP_BUILD_APP=ON`), so CI builds and tests the app on every platform.
