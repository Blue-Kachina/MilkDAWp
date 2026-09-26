# ADR-0009: Engine-owned offscreen GL context, surfaces share its textures

Status: Accepted on Windows; Recommended (untested) for macOS and Linux

## Context

§4.5 requires projectM to live in an engine that outlives every window, and
the Output window (§4.9) to show the same frame as the primary window. Phase
2.3 was the spike to settle how, per platform. What was learned on Windows
(JUCE 9.0.2, NVIDIA RTX 4070 Ti SUPER, driver 616.92; hybrid system with an
Intel iGPU):

1. **A `juce::OpenGLContext` cannot keep a native context alive on its own.**
   JUCE creates and destroys the native context as its component gains and
   loses a visible peer (`isShowingOrMinimised`). A context "owned by the
   engine" but attached to the editor dies with the editor: the
   `glContextCreationCount` diagnostic climbed on every editor reopen in
   REAPER (2026-09-19).
2. **projectM needs a live context for its whole lifetime**, since it
   allocates GL objects in `projectm_create` and frees them in
   `projectm_destroy`. Recreating it on every editor reopen resets the visual,
   which is v1's bug (§2.4).
3. **`OpenGLContext::setNativeSharedContext` fails on this driver.** JUCE calls
   `wglShareLists` from its own render thread at a moment we don't control.
   While the engine's context is current on the engine's render thread,
   NVIDIA refuses, and the surface silently renders nothing it could share.
   A surface-side `glIsTexture` probe on a known engine texture name detects
   this reliably, and did.
4. **`glBlitFramebuffer` into a JUCE window's default framebuffer failed**
   with `GL_INVALID_OPERATION` (probably a driver-forced multisampled back
   buffer). JUCE's own textured-quad path (`OpenGLContext::copyTexture`, the
   one it uses to composite components) works.

## Decision

- `RenderEngine` owns an **`OffscreenGLContext`**, created on the engine's
  own render thread and belonging to no window. On Windows this is a
  never-shown `WS_POPUP` window with a plain `wglCreateContext` context,
  matching JUCE's default pixel format (8-bit RGBA, 16-bit depth) and
  creation path. The thread pumps that window's messages so broadcast
  `SendMessage`s never wait on it.
- projectM (4.2, ADR-0008) renders with `projectm_opengl_render_frame_fbo`
  into a **triple-buffered set of textures**. After each frame the thread
  calls `glFinish`, then publishes the index, size and frame number in one
  atomic. Texture storage is re-specified in place on resize, so names never
  change.
- Each **`OutputSurface`** (plugin editor, app main window, `OutputWindow`,
  `mdw-view`) has its own `juce::OpenGLContext` and **shares by handshake,
  not through JUCE**. In `newOpenGLContextCreated()` (the surface's context
  is current and has no objects yet), it calls
  `RenderEngine::shareIntoCurrentContext()`. That parks the render thread
  with its context released, calls `wglShareLists(engine, surface)` with
  neither side current, and resumes. The surface then checks `glIsTexture`
  on a known engine texture name and reports the result in diagnostics.
- Surfaces draw the latest texture with `OpenGLContext::copyTexture`,
  scaled to cover the surface, centred and cropped. JUCE-painted children
  (drawer, diagnostics) composite on top as usual.
- The engine sizes its frames to the **largest visible surface** (times the
  quality scale) and **pauses GPU work when no surface is visible** (2.10).
  The context, projectM instance, preset and playlist position stay as they
  are while paused.

Per platform:

| Platform | Offscreen context | Sharing | Status |
|---|---|---|---|
| Windows | hidden window + WGL | `wglShareLists` handshake from the surface | Verified: plugin editor, JUCE Standalone, `mdw-view`, two surfaces at once |
| Linux | EGL surfaceless (`EGL_KHR_surfaceless_context`, GL 3.3 core) | Not wired: EGL only shares at creation, and JUCE 9's X11/EGL display differs from `EGL_DEFAULT_DISPLAY` | Headless render (2.7) only; windows show black until a sharing path exists. Untested (no hardware) |
| macOS | Not implemented; `create()` reports why | Would need an `NSOpenGLContext` created for sharing (JUCE's `setNativeSharedContext` takes one) | Untested (no hardware) |

Fallback if sharing fails on some Windows driver (e.g. AMD, or a surface on
a monitor driven by a different GPU in a hybrid system): not implemented.
The planned fallback is a PBO readback of each frame on the render thread
plus a texture upload in the surface (§4.5). The surface already detects the
failure and says so.

## Consequences

- Opening, closing, hiding, moving and recreating windows never resets
  projectM. Switching the Output window between windowed and fullscreen
  recreates its native window and re-shares, which is exercised every time.
- Any number of surfaces show the same frame at the cost of one blit each.
- One extra hidden window and one render thread per plugin instance. The
  thread idles (20 ms sleep, no GL calls) while nothing is visible.
- Frames are published after `glFinish`, costing some GPU/CPU overlap on the
  render thread; it is our own thread, so nothing else waits on it.
- Linux and macOS lose the (never-tested) in-editor rendering they would
  have had with the old design until their sharing paths exist. That is
  recorded under 2.3, since there is no hardware to build them against here.
- Ruled out: an engine context attached to a hidden JUCE component (JUCE
  only keeps contexts for visible, on-screen components); JUCE's
  `setNativeSharedContext` (the failure in finding 3); `glBlitFramebuffer` to
  the default framebuffer (finding 4).
