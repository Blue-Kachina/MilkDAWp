// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/GlFrameTarget.h"

#include <algorithm>

#include <juce_opengl/juce_opengl.h>

namespace milkdawp::engine {

using namespace ::juce::gl;

GlFrameTarget::GlFrameTarget(int width, int height) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  texture_ = texture;
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  framebuffer_ = framebuffer;

  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  resize(width, height);
}

GlFrameTarget::~GlFrameTarget() {
  GLuint framebuffer = framebuffer_;
  glDeleteFramebuffers(1, &framebuffer);
  GLuint texture = texture_;
  glDeleteTextures(1, &texture);
}

void GlFrameTarget::resize(int width, int height) {
  width = std::max(width, 1);
  height = std::max(height, 1);
  if (width == width_ && height == height_) {
    return;
  }
  width_ = width;
  height_ = height;

  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);

  GLint previous = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
}

void GlFrameTarget::readPixels(std::vector<std::uint8_t>& rgba) const {
  rgba.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4);
  GLint previous = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous));
}

} // namespace milkdawp::engine
