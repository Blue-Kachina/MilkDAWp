// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "GlUtil.h"

#include <array>

#include <juce_opengl/juce_opengl.h>

namespace milkdawp::engine::gl {

using namespace ::juce::gl;

const char* const kFullscreenVertexSource = R"(#version 330 core
out vec2 vUv;
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

namespace {

GLuint compile(GLenum type, const char* source, const char* label, std::string& error) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == 0) {
    std::array<GLchar, 1024> log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()) - 1, nullptr, log.data());
    error = std::string(label) + (type == GL_VERTEX_SHADER ? " vertex shader: " : " fragment shader: ") + log.data();
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

} // namespace

std::uint32_t buildProgram(const char* vertexSource, const char* fragmentSource, const char* label,
                           std::string& error) {
  const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, label, error);
  if (vertex == 0) {
    return 0;
  }
  const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, label, error);
  if (fragment == 0) {
    glDeleteShader(vertex);
    return 0;
  }
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked == 0) {
    std::array<GLchar, 1024> log{};
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()) - 1, nullptr, log.data());
    error = std::string(label) + " program: " + log.data();
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

void SavedState::capture() {
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2d);
  blend = glIsEnabled(GL_BLEND);
  depthTest = glIsEnabled(GL_DEPTH_TEST);
  cullFace = glIsEnabled(GL_CULL_FACE);
  scissorTest = glIsEnabled(GL_SCISSOR_TEST);
  glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
  glGetBooleanv(GL_COLOR_WRITEMASK, colourMask);
}

void SavedState::restore() const {
  glUseProgram(static_cast<GLuint>(program));
  glBindVertexArray(static_cast<GLuint>(vertexArray));
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2d));
  glActiveTexture(static_cast<GLenum>(activeTexture));
  const auto set = [](GLenum cap, GLboolean on) {
    if (on != GL_FALSE) {
      glEnable(cap);
    } else {
      glDisable(cap);
    }
  };
  set(GL_BLEND, blend);
  set(GL_DEPTH_TEST, depthTest);
  set(GL_CULL_FACE, cullFace);
  set(GL_SCISSOR_TEST, scissorTest);
  glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb), static_cast<GLenum>(blendDstRgb),
                      static_cast<GLenum>(blendSrcAlpha), static_cast<GLenum>(blendDstAlpha));
  glColorMask(colourMask[0], colourMask[1], colourMask[2], colourMask[3]);
}

} // namespace milkdawp::engine::gl
