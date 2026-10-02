// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/LayerCompositor.h"

#include <array>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/engine/GlFrameTarget.h"

namespace milkdawp::engine {

using namespace ::juce::gl;

namespace {

// A fullscreen triangle from gl_VertexID, so there is no vertex buffer to own.
constexpr const char* kVertexSource = R"(#version 330 core
out vec2 vUv;
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The blend state decides how the output meets the canvas; this only shapes
// the colour and alpha it is given (see LayerCompositor::compose).
constexpr const char* kFragmentSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform float uOpacity;
uniform int uMode; // 0 normal, 1 add, 2 screen, 3 multiply, 4 luma key
void main() {
  vec3 c = texture(uTexture, vUv).rgb;
  if (uMode == 2) {
    fragColor = vec4(c * uOpacity, 1.0);
  } else if (uMode == 3) {
    fragColor = vec4(c * uOpacity, uOpacity);
  } else if (uMode == 4) {
    // Brightness is the layer's opacity: black is clear, and anything about a
    // quarter bright or more is fully there.
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(c, uOpacity * min(1.0, luma * 4.0));
  } else {
    fragColor = vec4(c, uOpacity);
  }
}
)";

GLuint compile(GLenum type, const char* source, std::string& error) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == 0) {
    std::array<GLchar, 1024> log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()) - 1, nullptr, log.data());
    error = std::string(type == GL_VERTEX_SHADER ? "compositor vertex shader: " : "compositor fragment shader: ") +
            log.data();
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

// The GL state compose() changes, so it can put it back for projectM.
struct SavedState {
  GLint program = 0;
  GLint vertexArray = 0;
  GLint framebuffer = 0;
  GLint viewport[4] = {0, 0, 0, 0};
  GLint activeTexture = 0;
  GLint texture2d = 0;
  GLboolean blend = GL_FALSE;
  GLboolean depthTest = GL_FALSE;
  GLboolean cullFace = GL_FALSE;
  GLboolean scissorTest = GL_FALSE;
  GLint blendSrcRgb = 0;
  GLint blendDstRgb = 0;
  GLint blendSrcAlpha = 0;
  GLint blendDstAlpha = 0;
  GLboolean colourMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};

  void capture() {
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

  void restore() const {
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
};

} // namespace

LayerCompositor::LayerCompositor() {
  const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexSource, error_);
  if (vertex == 0) {
    return;
  }
  const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentSource, error_);
  if (fragment == 0) {
    glDeleteShader(vertex);
    return;
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
    error_ = std::string("compositor program: ") + log.data();
    glDeleteProgram(program);
    return;
  }
  program_ = program;
  textureUniform_ = glGetUniformLocation(program, "uTexture");
  opacityUniform_ = glGetUniformLocation(program, "uOpacity");
  modeUniform_ = glGetUniformLocation(program, "uMode");

  GLuint vertexArray = 0;
  glGenVertexArrays(1, &vertexArray);
  vertexArray_ = vertexArray;
}

LayerCompositor::~LayerCompositor() {
  if (program_ != 0) {
    glDeleteProgram(program_);
  }
  if (vertexArray_ != 0) {
    GLuint vertexArray = vertexArray_;
    glDeleteVertexArrays(1, &vertexArray);
  }
}

void LayerCompositor::compose(const GlFrameTarget& canvas, std::span<const LayerDraw> layers) {
  if (!ok()) {
    return;
  }
  SavedState saved;
  saved.capture();

  glBindFramebuffer(GL_FRAMEBUFFER, canvas.framebuffer());
  glViewport(0, 0, canvas.width(), canvas.height());
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(program_);
  glBindVertexArray(vertexArray_);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(textureUniform_, 0);
  glEnable(GL_BLEND);

  for (const auto& layer : layers) {
    // Alpha factors (ZERO, ONE) keep the canvas's own alpha at 1.
    switch (layer.blend) {
    case LayerBlend::Add:
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
      break;
    case LayerBlend::Screen:
      glBlendFuncSeparate(GL_ONE_MINUS_DST_COLOR, GL_ONE, GL_ZERO, GL_ONE);
      break;
    case LayerBlend::Multiply:
      glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
      break;
    case LayerBlend::Normal:
    case LayerBlend::LumaKey: // the shader folds brightness into alpha; the mix is an ordinary "over"
    default:
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
      break;
    }
    glBindTexture(GL_TEXTURE_2D, layer.texture);
    glUniform1f(opacityUniform_, layer.opacity);
    glUniform1i(modeUniform_, static_cast<GLint>(layer.blend));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  saved.restore();
}

} // namespace milkdawp::engine
