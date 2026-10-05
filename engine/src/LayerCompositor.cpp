// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/LayerCompositor.h"

#include <juce_opengl/juce_opengl.h>

#include "GlUtil.h"
#include "milkdawp/engine/GlFrameTarget.h"

namespace milkdawp::engine {

using namespace ::juce::gl;

namespace {

// The blend state decides how the output meets the canvas; this only shapes
// the colour and alpha it is given (see LayerCompositor::compose).
constexpr const char* kFragmentSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform float uOpacity;
uniform int uMode; // 0 normal, 1 add, 2 screen, 3 multiply, 4 luma key, 5 displace
uniform int uTextureAlpha; // 1: the texture's alpha scales the opacity (media)
uniform vec2 uUvScale;     // crop about the centre (media of another shape)
uniform sampler2D uBelow;  // displace: a copy of the canvas so far
void main() {
  vec4 t = texture(uTexture, (vUv - 0.5) * uUvScale + 0.5);
  vec3 c = t.rgb;
  float opacity = uOpacity * (uTextureAlpha == 1 ? t.a : 1.0);
  if (uMode == 5) {
    // Red pushes sideways, green up and down; mid-grey stays put. Up to a
    // tenth of the frame at full opacity. Drawn without blending. The map is
    // read from a mip level about 90 px tall: a camera's noise and fine detail
    // would otherwise make every pixel jitter from frame to frame (it read as
    // flicker); the big shapes still bend the picture.
    float lod = max(0.0, log2(float(textureSize(uTexture, 0).y) / 90.0));
    vec4 smoothMap = textureLod(uTexture, (vUv - 0.5) * uUvScale + 0.5, lod);
    float strength = uOpacity * (uTextureAlpha == 1 ? smoothMap.a : 1.0);
    vec2 push = (smoothMap.rg - 0.5) * 0.2 * strength;
    fragColor = vec4(texture(uBelow, vUv + push).rgb, 1.0);
  } else if (uMode == 2) {
    fragColor = vec4(c * opacity, 1.0);
  } else if (uMode == 3) {
    fragColor = vec4(c * opacity, opacity);
  } else if (uMode == 4) {
    // Brightness is the layer's opacity: black is clear, and anything about a
    // quarter bright or more is fully there.
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(c, opacity * min(1.0, luma * 4.0));
  } else {
    fragColor = vec4(c, opacity);
  }
}
)";

} // namespace

LayerCompositor::LayerCompositor() {
  const GLuint program = gl::buildProgram(gl::kFullscreenVertexSource, kFragmentSource, "compositor", error_);
  if (program == 0) {
    return;
  }
  program_ = program;
  textureUniform_ = glGetUniformLocation(program, "uTexture");
  opacityUniform_ = glGetUniformLocation(program, "uOpacity");
  modeUniform_ = glGetUniformLocation(program, "uMode");
  textureAlphaUniform_ = glGetUniformLocation(program, "uTextureAlpha");
  uvScaleUniform_ = glGetUniformLocation(program, "uUvScale");
  belowUniform_ = glGetUniformLocation(program, "uBelow");

  GLuint vertexArray = 0;
  glGenVertexArrays(1, &vertexArray);
  vertexArray_ = vertexArray;
}

LayerCompositor::~LayerCompositor() {
  below_.reset();
  if (program_ != 0) {
    glDeleteProgram(program_);
  }
  if (vertexArray_ != 0) {
    GLuint vertexArray = vertexArray_;
    glDeleteVertexArrays(1, &vertexArray);
  }
}

void LayerCompositor::compose(const GlFrameTarget& canvas, std::span<const LayerDraw> layers) {
  draw(canvas, layers, true);
}

void LayerCompositor::overlay(const GlFrameTarget& canvas, std::span<const LayerDraw> layers) {
  draw(canvas, layers, false);
}

void LayerCompositor::stamp(const GlFrameTarget& target, const LayerDraw& layer) {
  if (!ok()) {
    return;
  }
  gl::SavedState saved;
  saved.capture();
  glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
  glViewport(0, 0, target.width(), target.height());
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND); // the colour and its alpha go in as they are
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(program_);
  glBindVertexArray(vertexArray_);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(textureUniform_, 0);
  glBindTexture(GL_TEXTURE_2D, layer.texture);
  glUniform1f(opacityUniform_, layer.opacity);
  glUniform1i(modeUniform_, static_cast<GLint>(LayerBlend::Normal));
  glUniform1i(textureAlphaUniform_, layer.textureAlpha ? 1 : 0);
  glUniform2f(uvScaleUniform_, layer.uvScaleX, layer.uvScaleY);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  saved.restore();
}

void LayerCompositor::draw(const GlFrameTarget& canvas, std::span<const LayerDraw> layers, bool clear) {
  if (!ok()) {
    return;
  }
  gl::SavedState saved;
  saved.capture();

  glBindFramebuffer(GL_FRAMEBUFFER, canvas.framebuffer());
  glViewport(0, 0, canvas.width(), canvas.height());
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (clear) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  glUseProgram(program_);
  glBindVertexArray(vertexArray_);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(textureUniform_, 0);
  glUniform1i(belowUniform_, 1);
  glEnable(GL_BLEND);

  for (const auto& layer : layers) {
    if (layer.blend == LayerBlend::Displace) {
      // Displace reads what is below, and GL can't read the target it draws
      // into: copy the canvas so far first, then draw over it unblended.
      if (!below_) {
        below_ = std::make_unique<GlFrameTarget>(canvas.width(), canvas.height());
      } else {
        below_->resize(canvas.width(), canvas.height());
      }
      glBindFramebuffer(GL_READ_FRAMEBUFFER, canvas.framebuffer());
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, below_->framebuffer());
      glBlitFramebuffer(0, 0, canvas.width(), canvas.height(), 0, 0, canvas.width(), canvas.height(),
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, canvas.framebuffer());
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, below_->texture());
      glActiveTexture(GL_TEXTURE0);
      glDisable(GL_BLEND);
    } else {
      glEnable(GL_BLEND);
    }
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
    glUniform1i(textureAlphaUniform_, layer.textureAlpha ? 1 : 0);
    glUniform2f(uvScaleUniform_, layer.uvScaleX, layer.uvScaleY);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);

  saved.restore();
}

} // namespace milkdawp::engine
