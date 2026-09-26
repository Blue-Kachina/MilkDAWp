// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include "milkdawp/engine/RenderEngine.h"

namespace milkdawp::engine {

/// The embedded primary-window half of Phase 2.4's `OutputSurface`, and the
/// live instrument for Phase 2.3's context-persistence spike (see
/// `RenderEngine`'s class comment). A thin `Component` that attaches
/// `RenderEngine`'s persistent `OpenGLContext` to itself on construction and
/// detaches -- never destroys -- it on destruction, so creating/destroying
/// this `Component` (exactly what happens every time a plugin editor opens
/// and closes) does not by itself tear down GL state the way v1's
/// editor-owned canvas did (§2.4).
///
/// Currently renders a simple time-based colour cycle rather than a real
/// projectM frame: wiring `RenderEngine::renderFrame()` in here (PCM feed +
/// preset loading) is still a follow-up, now that a real projectM install is
/// actually available to build and test against on this Windows box. The
/// colour cycle exists purely so there is something visibly alive on screen
/// -- the concrete, human-checkable signal for "does the context survive
/// editor close/reopen" that this spike needs.
///
/// Also the only place `RenderEngine::ensureInstanceCreated()`/
/// `releaseInstance()` get called (from `newOpenGLContextCreated()`/
/// `openGLContextClosing()`): projectM's instance allocates real GL
/// resources at creation, confirmed by a crash the first time this ran
/// against a real install with the instance created eagerly in
/// RenderEngine's constructor, before any GL context existed.
class OutputSurface final : public juce::Component, private juce::OpenGLRenderer {
public:
  explicit OutputSurface(RenderEngine& engine);
  ~OutputSurface() override;

private:
  void newOpenGLContextCreated() override;
  void renderOpenGL() override;
  void openGLContextClosing() override;

  RenderEngine& engine_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputSurface)
};

} // namespace milkdawp::engine
