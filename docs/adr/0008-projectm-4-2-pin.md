# ADR-0008: Pin projectM to an upstream 4.2 commit via a vcpkg overlay port

Status: Recommended (D15; amends D4)

## Context

D4 / ADR-0002 takes projectM from the vcpkg registry, currently port 4.1.7
(the last upstream tag, July 2024). An API audit on 2026-09-26, against the
real installed headers and the 4.1.7 source in vcpkg's buildtrees, found
that 4.1.7 cannot do things the roadmap already depends on:

- **No render-to-FBO.** `ProjectM::RenderFrame()` calls
  `glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0)` before its final composite
  (source comment: "ToDo: Allow external apps to provide a custom target
  framebuffer"). Binding our own FBO around `projectm_opengl_render_frame()`,
  which is what `RenderEngine.cpp`'s `ScopedFramebufferBinding` does, has no
  effect on where the image lands. It only appears to work today because
  JUCE's render target in the embedded surface happens to be framebuffer 0.
  §4.5 (render once to an FBO, present to every `OutputSurface`), Phase
  2.3 (multi-surface spike), 2.4 (Output window), 2.7 (headless render test)
  and 5.3 (adaptive quality scales the FBO) all assume render-to-FBO.
- **GLEW with nobody initializing it.** 4.1.7 links GLEW but never calls
  `glewInit()`. Phase 2.3's crash fix is a Windows-only `ensureGlewInitialized()`
  helper that opens `glew32(d).dll` by name. It is unverified on macOS and
  Linux.
- **Wall-clock only timing.** Preset animation runs off projectM's own
  system clock, so headless renders (2.7) are not deterministic and preset
  motion cannot be driven from the audio sample clock.

Upstream `master` (checked at commit `1e7ef78`, 2026-09-10, `vcpkg.json`
version 4.2.0, SO version still 4, library name still `projectM-4`) adds,
all marked `@since 4.2.0`:

| API | Solves |
|---|---|
| `projectm_opengl_render_frame_fbo(instance, fbo)` | render-to-FBO; multi-surface; headless tests; adaptive quality |
| `projectm_create_with_opengl_load_proc(proc, user)` | GLEW removed (glad + internal `GLResolver`); one GL loader shared by all instances |
| `projectm_set_frame_time(instance, seconds)` | deterministic timing from our sample clock |
| `projectm_opengl_burn_texture(...)` | drawing an external texture into a preset's feedback buffer (post-1.0 layers, below) |
| `projectm_set_log_callback` / `_level` | route projectM diagnostics into 5.9's panel |
| `projectm_set_preset_start_clean` | optional black start instead of the previous frame carried into the next preset |
| user sprites (`projectm_sprite_*`) | MilkDrop sprite overlays; not needed for 1.0 |

4.2.0 has not been tagged. Options considered:

1. **Wait for a 4.2 release.** Blocks 2.3/2.4/2.7 on an unknown date.
2. **Backport only the FBO change onto 4.1.7** as a vcpkg patch. Smallest
   diff, but we would own a fork patch and still carry the GLEW workaround
   and wall-clock timing.
3. **Overlay port building a pinned `master` commit.** Gets every item
   above at once, and is the same pattern D4 already uses for JUCE (pinned
   tag + hash because the registry lags).

## Decision

Option 3.

- Add `vcpkg-overlays/projectm/` (portfile + `vcpkg.json`) that fetches
  `projectM-visualizer/projectm` at a pinned commit hash with `SHA512`, and
  register it via `overlay-ports` in `vcpkg-configuration.json`. The
  `projectm` override in `vcpkg.json` moves to the overlay's version. Use
  upstream's own default features (`external-glm`, `external-evallib`),
  which the registry already provides.
- The **minimum supported projectM is 4.2.0**. `ProjectMLibrary` requires
  `projectm_opengl_render_frame_fbo`, `projectm_create_with_opengl_load_proc`
  and `projectm_set_frame_time`, so a stray 4.1.x library on the search path
  reports `Unavailable{reason}` (which names the missing symbol) instead of
  silently rendering to the wrong framebuffer. No 4.1 fallback path is kept:
  one way to render, not two (§11 "do not add a third way").
- `RenderEngine` deletes `ScopedFramebufferBinding` and
  `ensureGlewInitialized()`, and creates instances with
  `projectm_create_with_opengl_load_proc`, passing a resolver backed by
  JUCE's `OpenGLHelpers::getExtensionFunction` (or `nullptr` to let
  projectM's own resolver work, whichever the 2.12 spike shows is reliable
  per platform).
- Bumping the pin works like a JUCE bump: a branch, the full CI matrix, the
  DAW checklist. When upstream tags 4.2.0 (or later), move to the tag and
  drop the overlay if the registry port has caught up.

## Consequences

- 2.3, 2.4, 2.7 and 5.3 are unblocked in principle; 2.12 (overlay port +
  engine adoption) becomes their prerequisite.
- The Windows-only GLEW workaround goes away, removing an unverified
  per-platform difference before macOS/Linux bring-up.
- Headless tests and `mdw-view` can drive projectM time from the
  `AudioRing` sample clock via `projectm_set_frame_time`, so frame N of a
  fixture render is reproducible.
- **Risk: building on an unreleased branch.** Master may have regressions
  that 4.1.7 does not. Mitigated by pinning a hash (never floating), the
  2.7 headless render test over real presets, and the DAW checklist before
  any beta. Recorded in §10.
- **Licensing (D10) is unchanged in kind:** still LGPL-2.1, still
  dynamically linked. The source offer and `THIRD_PARTY_NOTICES.md` must
  name the exact pinned commit, not a version number.
- **Multiple instances, one GL context.** 4.2's shared GL resolver is
  designed for several `projectm_handle`s in one process, and the audit
  found no per-process state that would prevent it (4.1.7's
  `MilkdropStaticShaders` singleton only holds shader source text). This
  keeps the post-1.0 "layers" idea open: N instances, each fed its own
  input, each rendering to its own FBO, mixed by our compositor pass. It is
  not in 1.0 scope. The guardrail for now: engine code should not *assume*
  exactly one projectM instance when an extra level of indirection costs
  nothing (for example, per-instance state lives in a struct, not directly
  as `RenderEngine` members).
- Unchanged by this ADR: D7 (own `Playlist`; `libprojectM-4-playlist` still
  has no ratings, weighting or sample-accurate scheduling), and our own beat
  analysis (projectM exposes no beat/tempo output; its hard-cut detector is a
  volume-delta threshold).
- Ruled out: keeping 4.1.7 and blitting framebuffer 0 into our FBO after
  every frame (extra copy, still broken for any surface whose default
  framebuffer is not framebuffer 0), and maintaining a long-lived fork.
