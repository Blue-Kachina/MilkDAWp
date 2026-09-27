// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include "milkdawp/engine/RenderEngine.h"

namespace milkdawp::engine {

/// A window's view of the visualization (§4.5, Phase 2.4): the plugin
/// editor, the app's main window, or an `OutputWindow`. Owns its *own*
/// `juce::OpenGLContext`, created sharing with `RenderEngine`'s offscreen
/// context (or, where that fails, fed CPU copies of each frame), and each
/// frame blits the engine's latest texture into itself,
/// cropped to fill (no letterboxing, no stretching). projectM never runs in
/// this context, so creating, destroying, hiding or reparenting a surface
/// never resets the visual; any number of surfaces show the same frame.
///
/// JUCE-painted children (the control drawer, diagnostics) composite over
/// the blitted frame as long as they are children of this component, not
/// siblings (§4.11, 2.3's finding).
///
/// Reports its pixel size and whether it is showing to the engine four
/// times a second: the engine sizes its FBO to the largest visible surface
/// and pauses when none is visible (2.10).
class OutputSurface final : public juce::Component, private juce::OpenGLRenderer, private juce::Timer {
public:
  explicit OutputSurface(RenderEngine& engine);
  ~OutputSurface() override;

  /// False if this surface's context could not see the engine's textures
  /// (the shared-context handshake failed on this driver, or there is no
  /// sharing path on this platform yet). The surface then shows CPU copies of
  /// the frames instead (the readback fallback, ADR-0009), one frame behind;
  /// the diagnostics say which path is in use. Setting the environment
  /// variable MILKDAWP_FORCE_FRAME_READBACK forces the fallback, for testing
  /// it on a machine where sharing works.
  [[nodiscard]] bool isSharingWorking() const noexcept { return sharingWorking_.load(); }
  /// True once this surface's GL context exists.
  [[nodiscard]] bool hasContext() const noexcept { return contextLive_.load(); }
  /// How many times this surface's own GL context has been (re)created.
  [[nodiscard]] int contextCreationCount() const noexcept { return contextCreations_.load(); }

  void resized() override;

private:
  void timerCallback() override;
  void attachIfReady();
  void reportToEngine();
  void setReadbackClient(bool needed) noexcept;
  /// GL thread: uploads the engine's latest CPU frame into readbackTexture_
  /// if it is newer than the last upload. False if there is none yet.
  bool uploadLatestReadbackFrame(int& width, int& height);

  void newOpenGLContextCreated() override;
  void renderOpenGL() override;
  void openGLContextClosing() override;

  RenderEngine& engine_;
  juce::OpenGLContext context_;
  int slot_ = -1;
  bool attached_ = false;
  int ticksWaitingForEngine_ = 0;

  std::atomic<int> logicalWidth_{0};
  std::atomic<int> logicalHeight_{0};
  std::atomic<bool> sharingWorking_{true};
  std::atomic<bool> contextLive_{false};
  std::atomic<int> contextCreations_{0};

  // Readback fallback. The texture and upload bookkeeping belong to the GL
  // thread; the client flag is also cleared from the destructor.
  const bool forceReadback_;
  std::atomic<bool> readbackClient_{false};
  std::uint32_t readbackTexture_ = 0;
  int readbackWidth_ = 0;
  int readbackHeight_ = 0;
  std::uint64_t readbackNumber_ = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputSurface)
};

} // namespace milkdawp::engine
