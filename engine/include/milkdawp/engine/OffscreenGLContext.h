// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <memory>
#include <string>

namespace milkdawp::engine {

/// A GL context that belongs to no window (Phase 2.3, ADR-0009). This is
/// what lets projectM outlive every window: `RenderEngine` creates one of
/// these on its render thread for the lifetime of the processor or app,
/// projectM renders into textures in it, and each `OutputSurface` (editor,
/// Output window) is a separate JUCE GL context created *sharing* with this
/// one, so it can draw those textures. Closing the plugin editor no longer
/// takes projectM's GL state with it (§2.4).
///
/// Platforms:
///   - Windows: a never-shown WS_POPUP window + plain wglCreateContext,
///     the same creation path JUCE uses for its own contexts, so
///     wglShareLists between them sees matching contexts. Verified.
///   - Linux: EGL on the default display with EGL_KHR_surfaceless_context.
///     Enough for headless rendering (2.7); sharing with JUCE's X11/EGL
///     contexts is not wired or tested.
///   - macOS: not implemented yet; create() reports why.
///
/// Thread affinity: create(), the destructor, makeCurrent(), doneCurrent()
/// and pumpPlatformEvents() must all run on the same thread (on Windows the
/// hidden window belongs to its creating thread). create() leaves the
/// context current on that thread.
class OffscreenGLContext {
public:
  struct CreateResult {
    std::unique_ptr<OffscreenGLContext> context; // null on failure
    std::string error;                           // empty on success
  };

  [[nodiscard]] static CreateResult create();

  ~OffscreenGLContext();
  OffscreenGLContext(const OffscreenGLContext&) = delete;
  OffscreenGLContext& operator=(const OffscreenGLContext&) = delete;

  bool makeCurrent() noexcept;
  void doneCurrent() noexcept;

  /// The native context handle to pass to
  /// `juce::OpenGLContext::setNativeSharedContext()` (HGLRC on Windows).
  [[nodiscard]] void* nativeShareHandle() const noexcept;

  /// Makes the context current on the calling thread (a different window
  /// context) share objects with `offscreenHandle` (a nativeShareHandle()).
  /// The calling thread's context must not have created any GL objects
  /// yet, and the offscreen context must not be current on any thread
  /// during the call (NVIDIA's wglShareLists fails otherwise, which is why
  /// JUCE's own setNativeSharedContext() path does not work here: it shares
  /// at an unpredictable moment on JUCE's thread). Windows only; returns
  /// false elsewhere.
  static bool shareWithCurrentContext(void* offscreenHandle) noexcept;

  /// Resolves a GL function by name for the current context. Signature
  /// matches projectM 4.2's `projectm_load_proc`.
  static void* getProcAddress(const char* name, void* userData) noexcept;

  /// Windows: drains the hidden window's message queue so broadcast
  /// messages (e.g. WM_SETTINGCHANGE sent with SendMessage) never wait on
  /// this thread. Call regularly from the owning thread. No-op elsewhere.
  void pumpPlatformEvents() noexcept;

  /// GL_VENDOR / GL_RENDERER / GL_VERSION of the context, for diagnostics.
  [[nodiscard]] const std::string& description() const noexcept { return description_; }

  struct Impl;

private:
  explicit OffscreenGLContext(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
  std::string description_;
};

} // namespace milkdawp::engine
