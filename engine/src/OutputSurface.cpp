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

bool readbackForcedByEnvironment() {
  return juce::SystemStats::getEnvironmentVariable("MILKDAWP_FORCE_FRAME_READBACK", {}).isNotEmpty();
}
} // namespace

OutputSurface::OutputSurface(RenderEngine& engine) : engine_(engine), forceReadback_(readbackForcedByEnvironment()) {
  slot_ = engine_.registerSurface();
  context_.setRenderer(this);
  context_.setContinuousRepainting(true);
  attachIfReady();
  startTimerHz(4);
}

OutputSurface::~OutputSurface() {
  stopTimer();
  context_.detach();
  setReadbackClient(false);
  engine_.unregisterSurface(slot_);
}

void OutputSurface::setReadbackClient(bool needed) noexcept {
  if (readbackClient_.exchange(needed) == needed) {
    return;
  }
  if (needed) {
    engine_.addReadbackClient();
  } else {
    engine_.removeReadbackClient();
  }
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
  // link worked, the engine's texture names are visible here. If not, fall
  // back to CPU copies of the frames.
  bool shared = false;
  if (!forceReadback_) {
    engine_.shareIntoCurrentContext();
    const auto probe = engine_.probeTextureName();
    shared = probe != 0 && glIsTexture(probe) == GL_TRUE;
  }
  sharingWorking_.store(shared);
  setReadbackClient(!shared);
  contextLive_.store(true);
}

bool OutputSurface::uploadLatestReadbackFrame(int& width, int& height) {
  using namespace ::juce::gl;
  const auto frame = engine_.latestReadbackFrame();
  if (!frame) {
    if (readbackTexture_ == 0) {
      return false;
    }
    width = readbackWidth_; // no newer frame: keep showing the last upload
    height = readbackHeight_;
    return true;
  }

  if (readbackTexture_ == 0) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    readbackTexture_ = texture;
    glBindTexture(GL_TEXTURE_2D, readbackTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  } else {
    glBindTexture(GL_TEXTURE_2D, readbackTexture_);
  }

  if (frame->number != readbackNumber_) {
    // Rows arrive bottom first, which is also how a texture is laid out, so
    // the upload has the same orientation as the engine's own texture.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (frame->width != readbackWidth_ || frame->height != readbackHeight_) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame->width, frame->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   frame->rgba.data());
      readbackWidth_ = frame->width;
      readbackHeight_ = frame->height;
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height, GL_RGBA, GL_UNSIGNED_BYTE,
                      frame->rgba.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    readbackNumber_ = frame->number;
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  width = readbackWidth_;
  height = readbackHeight_;
  return true;
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

  GLuint texture = 0;
  int frameWidth = 0;
  int frameHeight = 0;
  if (sharingWorking_.load()) {
    const auto frame = engine_.latestFrame();
    if (!frame) {
      return;
    }
    texture = frame->texture;
    frameWidth = frame->width;
    frameHeight = frame->height;
  } else {
    if (!uploadLatestReadbackFrame(frameWidth, frameHeight)) {
      return;
    }
    texture = readbackTexture_;
  }
  if (frameWidth <= 0 || frameHeight <= 0) {
    return;
  }

  // Scale the frame to cover the surface, centred: the overflow on one axis
  // is cropped by the target clip area.
  const double coverScale = std::max(static_cast<double>(outWidth) / frameWidth,
                                     static_cast<double>(outHeight) / frameHeight);
  const int drawWidth = static_cast<int>(std::ceil(frameWidth * coverScale));
  const int drawHeight = static_cast<int>(std::ceil(frameHeight * coverScale));
  const juce::Rectangle<int> anchor((outWidth - drawWidth) / 2, (outHeight - drawHeight) / 2, drawWidth, drawHeight);

  // A textured quad through JUCE's own overlay shader, the same path JUCE
  // uses to composite its component layer. (glBlitFramebuffer into the
  // window's default framebuffer failed with GL_INVALID_OPERATION on an
  // NVIDIA driver, most likely a driver-forced multisampled back buffer.)
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  context_.copyTexture({0, 0, outWidth, outHeight}, anchor, outWidth, outHeight, /*flippedVertically=*/false,
                       /*blend=*/false);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void OutputSurface::openGLContextClosing() {
  using namespace ::juce::gl;
  // Still current here. The next context (a fullscreen toggle recreates it)
  // starts its readback texture from scratch.
  if (readbackTexture_ != 0) {
    GLuint texture = readbackTexture_;
    glDeleteTextures(1, &texture);
    readbackTexture_ = 0;
  }
  readbackWidth_ = 0;
  readbackHeight_ = 0;
  readbackNumber_ = 0;
  contextLive_.store(false);
}

} // namespace milkdawp::engine
