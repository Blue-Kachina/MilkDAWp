// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/EffectsChain.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <juce_opengl/juce_opengl.h>

#include "GlUtil.h"
#include "milkdawp/engine/GlFrameTarget.h"

namespace milkdawp::engine {

using namespace ::juce::gl;

namespace {

// Colour and geometry in one pass. The geometry works in aspect-corrected
// coordinates around the centre, so mirror and kaleidoscope folds and the
// rotation are not stretched on a wide frame; it maps each output pixel back
// to where it comes from in the source (folds first, then the inverse of the
// rotation and zoom). Outside the source, the picture mirrors (zooming out
// shows reflections, not black or smeared edges). Colour ops are per pixel,
// so their order against the geometry doesn't matter.
constexpr const char* kMainSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform vec2 uSize;
uniform float uHue;
uniform float uSaturation;
uniform float uBrightness;
uniform float uZoom;
uniform float uAngle;
uniform float uPixelate;
uniform float uRgbSplit;
uniform int uMirror;
uniform int uSegments;
vec3 sampleAt(vec2 uv) {
  return texture(uTexture, 1.0 - abs(1.0 - mod(uv, 2.0))).rgb;
}
void main() {
  vec2 uv = vUv;
  if (uPixelate > 1.0) {
    uv = (floor(uv * uSize / uPixelate) + 0.5) * uPixelate / uSize;
  }
  float aspect = uSize.x / uSize.y;
  vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
  if (uMirror == 1 || uMirror == 3) {
    p.x = -abs(p.x); // the right half shows the left, mirrored
  }
  if (uMirror == 2 || uMirror == 3) {
    p.y = abs(p.y); // the bottom shows the top, mirrored
  }
  if (uSegments > 0) {
    float segment = 6.28318530718 / float(uSegments);
    float a = mod(atan(p.y, p.x), segment);
    if (a > segment * 0.5) {
      a = segment - a;
    }
    p = length(p) * vec2(cos(a), sin(a));
  }
  float s = sin(uAngle);
  float c = cos(uAngle);
  p = mat2(c, s, -s, c) * p / uZoom;
  uv = p / vec2(aspect, 1.0) + 0.5;
  vec3 col;
  if (uRgbSplit > 0.0) {
    vec2 d = vec2(uRgbSplit, 0.0);
    col = vec3(sampleAt(uv + d).r, sampleAt(uv).g, sampleAt(uv - d).b);
  } else {
    col = sampleAt(uv);
  }
  col *= uBrightness;
  float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
  col = mix(vec3(luma), col, uSaturation);
  if (uHue != 0.0) {
    // Rotation about the grey axis (Rodrigues): +120 degrees takes red to green.
    vec3 k = vec3(0.57735026919);
    float ch = cos(uHue);
    float sh = sin(uHue);
    col = col * ch + cross(k, col) * sh + k * dot(k, col) * (1.0 - ch);
  }
  fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)";

// One direction of a separable Gaussian: 9 taps `uStep` apart (sigma 2 taps).
constexpr const char* kBlurSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform vec2 uStep;
void main() {
  const float w[5] = float[5](0.2042, 0.1802, 0.1238, 0.0663, 0.0276);
  vec3 sum = texture(uTexture, vUv).rgb * w[0];
  for (int i = 1; i < 5; ++i) {
    sum += texture(uTexture, vUv + uStep * float(i)).rgb * w[i];
    sum += texture(uTexture, vUv - uStep * float(i)).rgb * w[i];
  }
  fragColor = vec4(sum, 1.0);
}
)";

// Glow's input: the bright parts only, averaged down to quarter size (four
// bilinear taps cover the 4x4 source pixels behind each output pixel).
constexpr const char* kBrightSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform vec2 uTexel;
void main() {
  vec3 c = (texture(uTexture, vUv + vec2(-uTexel.x, -uTexel.y)).rgb +
            texture(uTexture, vUv + vec2(uTexel.x, -uTexel.y)).rgb +
            texture(uTexture, vUv + vec2(-uTexel.x, uTexel.y)).rgb +
            texture(uTexture, vUv + vec2(uTexel.x, uTexel.y)).rgb) * 0.25;
  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
  fragColor = vec4(c * smoothstep(0.4, 0.9, luma), 1.0);
}
)";

// Mode 0, glow: the picture plus the blurred bright parts. Mode 1, Trails:
// whichever is brighter, the picture or the previous output faded by `uAmount`.
constexpr const char* kCombineSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform sampler2D uSecond;
uniform int uMode;
uniform float uAmount;
void main() {
  vec3 a = texture(uTexture, vUv).rgb;
  vec3 b = texture(uSecond, vUv).rgb;
  vec3 c = uMode == 0 ? a + b * uAmount : max(a, b * uAmount);
  fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

GlFrameTarget& ensure(std::unique_ptr<GlFrameTarget>& target, int width, int height) {
  if (!target) {
    target = std::make_unique<GlFrameTarget>(width, height);
  } else {
    target->resize(width, height);
  }
  return *target;
}

GLint uniform(GLuint program, const char* name) { return glGetUniformLocation(program, name); }

} // namespace

// ---- EffectsState ----------------------------------------------------------------

struct EffectsState::Targets {
  std::unique_ptr<GlFrameTarget> a;
  std::unique_ptr<GlFrameTarget> b;
  std::unique_ptr<GlFrameTarget> glowA;
  std::unique_ptr<GlFrameTarget> glowB;
  std::unique_ptr<GlFrameTarget> history[2];
  int historyIndex = 0;
  bool historyValid = false;
};

EffectsState::EffectsState() = default;
EffectsState::~EffectsState() = default;

bool EffectsState::advance(const core::VisualControls& target, float dtSeconds) {
  if (!primed_) {
    smoothed_ = target;
    primed_ = true;
  } else {
    smoothed_ = core::smoothVisualControls(smoothed_, target, dtSeconds);
  }
  const float dt = std::clamp(dtSeconds, 0.0f, 0.25f);
  if (smoothed_.rotation != 0.0f) {
    // Half a turn per second at full Rotation.
    angle_ = std::remainder(angle_ + smoothed_.rotation * std::numbers::pi_v<float> * dt, kTwoPi);
  } else if (angle_ != 0.0f) {
    angle_ -= angle_ * std::min(1.0f, dt / 0.3f); // remainder() keeps it in (-pi, pi]: upright is 0
    if (std::abs(angle_) < 1.0e-3f) {
      angle_ = 0.0f;
    }
  }
  active_ = !core::postEffectsNeutral(smoothed_) || angle_ != 0.0f;
  if (!active_) {
    targets_.reset();
  }
  return active_;
}

void EffectsState::setImmediate(const core::VisualControls& controls, float angleRadians) {
  smoothed_ = controls;
  angle_ = angleRadians;
  primed_ = true;
  active_ = !core::postEffectsNeutral(smoothed_) || angle_ != 0.0f;
}

// ---- EffectsChain ----------------------------------------------------------------

EffectsChain::EffectsChain() {
  struct Build {
    std::uint32_t* program;
    const char* source;
    const char* label;
  };
  for (const auto& [program, source, label] :
       {Build{&blur_, kBlurSource, "effects blur"}, Build{&bright_, kBrightSource, "effects glow"},
        Build{&combine_, kCombineSource, "effects combine"}, Build{&main_, kMainSource, "effects"}}) {
    *program = gl::buildProgram(gl::kFullscreenVertexSource, source, label, error_);
    if (*program == 0) {
      // main_ is built last, so ok() is false whichever one failed.
      return;
    }
  }
  GLuint vertexArray = 0;
  glGenVertexArrays(1, &vertexArray);
  vertexArray_ = vertexArray;
}

EffectsChain::~EffectsChain() {
  for (const auto program : {main_, blur_, bright_, combine_}) {
    if (program != 0) {
      glDeleteProgram(program);
    }
  }
  if (vertexArray_ != 0) {
    GLuint vertexArray = vertexArray_;
    glDeleteVertexArrays(1, &vertexArray);
  }
}

void EffectsChain::draw(std::uint32_t program, const GlFrameTarget& target, std::uint32_t texture,
                        std::uint32_t second) const {
  glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
  glViewport(0, 0, target.width(), target.height());
  glUseProgram(program);
  glUniform1i(uniform(program, "uTexture"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  if (second != 0) {
    glUniform1i(uniform(program, "uSecond"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, second);
    glActiveTexture(GL_TEXTURE0);
  }
  glDrawArrays(GL_TRIANGLES, 0, 3);
  if (second != 0) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
  }
}

void EffectsChain::apply(EffectsState& state, std::uint32_t sourceTexture, const GlFrameTarget& output) {
  if (!ok()) {
    return;
  }
  gl::SavedState saved;
  saved.capture();
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glBindVertexArray(vertexArray_);

  if (!state.targets_) {
    state.targets_ = std::make_unique<EffectsState::Targets>();
  }
  auto& t = *state.targets_;
  const auto& c = state.smoothed_;
  const int width = output.width();
  const int height = output.height();
  const auto fw = static_cast<float>(width);
  const auto fh = static_cast<float>(height);

  const bool trailsOn = c.trails > 0.0f;
  const bool glowOn = c.glow > 0.0f;
  const bool blurOn = c.blur > 0.0f;
  const bool geometryOrColour = c.hueDegrees != 0.0f || c.saturation != 1.0f || c.brightness != 1.0f ||
                                c.zoom != 0.0f || state.angle_ != 0.0f || c.pixelate > 0.0f ||
                                c.mirror != core::MirrorMode::Off || c.kaleidoscopeSegments > 0 || c.rgbSplit > 0.0f;
  // With nothing else to do (the chain was called anyway), the main pass at
  // neutral is a plain copy.
  const bool mainOn = geometryOrColour || !(trailsOn || glowOn || blurOn);
  if (!trailsOn) {
    t.historyValid = false;
  }

  int stagesLeft = (mainOn ? 1 : 0) + (blurOn ? 1 : 0) + (glowOn ? 1 : 0) + (trailsOn ? 1 : 0);
  std::uint32_t current = sourceTexture;
  const GlFrameTarget* currentTarget = nullptr;
  // A full-size scratch target that isn't the one being read.
  const auto scratch = [&]() -> const GlFrameTarget& {
    auto& a = ensure(t.a, width, height);
    return &a != currentTarget ? a : ensure(t.b, width, height);
  };
  // Where a stage writes: the output if it is the last (Trails, when on, is
  // always last and writes its history target instead).
  const auto stageTarget = [&]() -> const GlFrameTarget& {
    --stagesLeft;
    return stagesLeft == 0 && !trailsOn ? output : scratch();
  };
  const auto advanceTo = [&](const GlFrameTarget& target) {
    current = target.texture();
    currentTarget = &target;
  };

  if (mainOn) {
    const auto& target = stageTarget();
    glUseProgram(main_);
    glUniform2f(uniform(main_, "uSize"), fw, fh);
    glUniform1f(uniform(main_, "uHue"), c.hueDegrees * std::numbers::pi_v<float> / 180.0f);
    glUniform1f(uniform(main_, "uSaturation"), c.saturation);
    glUniform1f(uniform(main_, "uBrightness"), c.brightness);
    glUniform1f(uniform(main_, "uZoom"), std::exp2(c.zoom));
    glUniform1f(uniform(main_, "uAngle"), state.angle_);
    // Up to 1/16 of the frame height per block, most of the travel in the small sizes.
    glUniform1f(uniform(main_, "uPixelate"), 1.0f + c.pixelate * c.pixelate * fh / 16.0f);
    glUniform1f(uniform(main_, "uRgbSplit"), c.rgbSplit * 0.02f);
    glUniform1i(uniform(main_, "uMirror"), static_cast<GLint>(c.mirror));
    glUniform1i(uniform(main_, "uSegments"), c.kaleidoscopeSegments);
    draw(main_, target, current);
    advanceTo(target);
  }

  if (blurOn) {
    // Up to 1/40 of the frame height each side (27 px at 1080p).
    const float spacing = c.blur * fh / 40.0f / 4.0f;
    const auto& across = scratch();
    glUseProgram(blur_);
    glUniform2f(uniform(blur_, "uStep"), spacing / fw, 0.0f);
    draw(blur_, across, current);
    advanceTo(across);
    const auto& target = stageTarget();
    glUseProgram(blur_);
    glUniform2f(uniform(blur_, "uStep"), 0.0f, spacing / fh);
    draw(blur_, target, current);
    advanceTo(target);
  }

  if (glowOn) {
    const int qw = std::max(width / 4, 1);
    const int qh = std::max(height / 4, 1);
    auto& glowA = ensure(t.glowA, qw, qh);
    auto& glowB = ensure(t.glowB, qw, qh);
    glUseProgram(bright_);
    glUniform2f(uniform(bright_, "uTexel"), 1.0f / fw, 1.0f / fh);
    draw(bright_, glowA, current);
    const float spacing = static_cast<float>(qh) / 180.0f;
    glUseProgram(blur_);
    glUniform2f(uniform(blur_, "uStep"), spacing / static_cast<float>(qw), 0.0f);
    draw(blur_, glowB, glowA.texture());
    glUseProgram(blur_);
    glUniform2f(uniform(blur_, "uStep"), 0.0f, spacing / static_cast<float>(qh));
    draw(blur_, glowA, glowB.texture());
    const auto& target = stageTarget();
    glUseProgram(combine_);
    glUniform1i(uniform(combine_, "uMode"), 0);
    glUniform1f(uniform(combine_, "uAmount"), c.glow * 1.5f);
    draw(combine_, target, current, glowA.texture());
    advanceTo(target);
  }

  if (trailsOn) {
    --stagesLeft;
    auto& previous = ensure(t.history[t.historyIndex], width, height);
    auto& next = ensure(t.history[1 - t.historyIndex], width, height);
    glUseProgram(combine_);
    glUniform1i(uniform(combine_, "uMode"), 1);
    glUniform1f(uniform(combine_, "uAmount"), t.historyValid ? c.trails * kMaxTrailsDecay : 0.0f);
    draw(combine_, next, current, previous.texture());
    t.historyIndex = 1 - t.historyIndex;
    t.historyValid = true;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, next.framebuffer());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, output.framebuffer());
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  saved.restore();
}

} // namespace milkdawp::engine
