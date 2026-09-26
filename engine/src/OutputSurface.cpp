// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/OutputSurface.h"

#include <cmath>

namespace milkdawp::engine {

namespace {
// The engine's context appears within a frame or two of its creation; if it
// has not after this long, the engine is unavailable and the surface attaches
// unshared (so the drawer and diagnostics still paint).
constexpr int kMaxTicksWaitingForEngine = 20; // at 4 Hz: 5 s
} // namespace

OutputSurface::OutputSurface(RenderEngine& engine) : engine_(engine) {
  slot_ = engine_.registerSurface();
  context_.setRenderer(this);
  context_.setContinuousRepainting(true);
  attachIfReady();
  startTimerHz(4);
}

OutputSurface::~OutputSurface() {
  stopTimer();
  context_.detach();
  engine_.unregisterSurface(slot_);
}

void OutputSurface::attachIfReady() {
  if (attached_) {
    return;
  }
  void* shared = engine_.sharedContextHandle();
  const bool engineGaveUp = !engine_.unavailableReason().empty();
  if (shared == nullptr && !engineGaveUp && ticksWaitingForEngine_ < kMaxTicksWaitingForEngine) {
    return; // the render thread is still creating its context
  }
  // No setNativeSharedContext(): JUCE would call wglShareLists at a moment
  // we cannot coordinate with the engine's render thread, and NVIDIA
  // refuses while the engine's context is current. newOpenGLContextCreated()
  // shares instead, through the engine's handshake.
  context_.attachTo(*this);
  attached_ = true;
}

void OutputSurface::resized() { reportToEngine(); }

void OutputSurface::timerCallback() {
  if (!attached_) {
    ++ticksWaitingForEngine_;
    attachIfReady();
  }
  reportToEngine();
}

void OutputSurface::reportToEngine() {
  const double scale = juce::Component::getApproximateScaleFactorForComponent(this);
  const int width = static_cast<int>(std::lround(getWidth() * scale));
  const int height = static_cast<int>(std::lround(getHeight() * scale));
  logicalWidth_.store(getWidth());
  logicalHeight_.store(getHeight());
  engine_.reportSurfaceSize(slot_, width, height, isShowing() && attached_);
}

void OutputSurface::newOpenGLContextCreated() {
  using namespace ::juce::gl;
  contextCreations_.fetch_add(1);
  // First thing, before JUCE creates any objects of its own in this context
  // (a context can only be linked while it has none). Then confirm: if the
  // link worked, the engine's texture names are visible here.
  engine_.shareIntoCurrentContext();
  const auto probe = engine_.probeTextureName();
  sharingWorking_.store(probe != 0 && glIsTexture(probe) == GL_TRUE);
  contextLive_.store(true);
}

void OutputSurface::renderOpenGL() {
  using namespace ::juce::gl;

  // Logical size from the message thread, times JUCE's own scale for this
  // context: never touches the Component from this (GL) thread.
  const double renderingScale = context_.getRenderingScale();
  const int outWidth = std::max(juce::roundToInt(renderingScale * logicalWidth_.load()), 1);
  const int outHeight = std::max(juce::roundToInt(renderingScale * logicalHeight_.load()), 1);

  glViewport(0, 0, outWidth, outHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const auto frame = engine_.latestFrame();
  if (!frame || !sharingWorking_.load() || frame->width <= 0 || frame->height <= 0) {
    return;
  }

  // Scale the frame to cover the surface, centred: the overflow on one axis
  // is cropped by the target clip area.
  const double coverScale = std::max(static_cast<double>(outWidth) / frame->width,
                                     static_cast<double>(outHeight) / frame->height);
  const int drawWidth = static_cast<int>(std::ceil(frame->width * coverScale));
  const int drawHeight = static_cast<int>(std::ceil(frame->height * coverScale));
  const juce::Rectangle<int> anchor((outWidth - drawWidth) / 2, (outHeight - drawHeight) / 2, drawWidth, drawHeight);

  // A textured quad through JUCE's own overlay shader, the same path JUCE
  // uses to composite its component layer. (glBlitFramebuffer into the
  // window's default framebuffer failed with GL_INVALID_OPERATION on an
  // NVIDIA driver, most likely a driver-forced multisampled back buffer.)
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, frame->texture);
  context_.copyTexture({0, 0, outWidth, outHeight}, anchor, outWidth, outHeight, /*flippedVertically=*/false,
                       /*blend=*/false);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void OutputSurface::openGLContextClosing() {
  contextLive_.store(false);
}

} // namespace milkdawp::engine
