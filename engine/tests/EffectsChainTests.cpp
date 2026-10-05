// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 8.2: the effects chain on a real GL context with no projectM. Sources
// are filled with known pictures, run through one effect at a time, and read
// back against hand-computed values. Like the compositor tests, these report
// why and pass where no offscreen context exists, unless
// MILKDAWP_REQUIRE_HEADLESS_RENDER is set.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/engine/EffectsChain.h"
#include "milkdawp/engine/GlFrameTarget.h"
#include "milkdawp/engine/OffscreenGLContext.h"

using namespace milkdawp;
using namespace milkdawp::engine;

// See LayerCompositorTests.cpp: under ThreadSanitizer the GL tests stand down.
#if defined(__SANITIZE_THREAD__)
#define MILKDAWP_UNDER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MILKDAWP_UNDER_TSAN 1
#endif
#endif

namespace {

#ifdef MILKDAWP_UNDER_TSAN
constexpr bool kUnderTsan = true;
#else
constexpr bool kUnderTsan = false;
#endif

struct Rgb {
  int r = 0;
  int g = 0;
  int b = 0;
};

struct EffectsRig {
  std::unique_ptr<OffscreenGLContext> context;
  std::unique_ptr<EffectsChain> chain;
  std::string skipReason;

  EffectsRig() {
    if (kUnderTsan) {
      skipReason = "ThreadSanitizer cannot analyse Mesa's software renderer";
      return;
    }
    auto created = OffscreenGLContext::create();
    if (!created.context) {
      skipReason = created.error;
      return;
    }
    context = std::move(created.context);
    juce::gl::loadFunctions();
    chain = std::make_unique<EffectsChain>();
    if (!chain->ok()) {
      skipReason = chain->error();
      chain.reset();
    }
  }

  ~EffectsRig() {
    chain.reset();
    context.reset();
  }

  [[nodiscard]] bool ready() const { return chain != nullptr; }

  // A target filled pixel by pixel: `colour(x, y)` with y = 0 the bottom row.
  static std::unique_ptr<GlFrameTarget> picture(int width, int height, const std::function<Rgb(int, int)>& colour) {
    using namespace ::juce::gl;
    auto target = std::make_unique<GlFrameTarget>(width, height);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height) * 4);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto c = colour(x, y);
        const auto i = static_cast<std::size_t>(y * width + x) * 4;
        pixels[i] = static_cast<std::uint8_t>(c.r);
        pixels[i + 1] = static_cast<std::uint8_t>(c.g);
        pixels[i + 2] = static_cast<std::uint8_t>(c.b);
        pixels[i + 3] = 255;
      }
    }
    glBindTexture(GL_TEXTURE_2D, target->texture());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return target;
  }

  static std::unique_ptr<GlFrameTarget> solid(int size, Rgb colour) {
    return picture(size, size, [colour](int, int) { return colour; });
  }

  static Rgb at(const GlFrameTarget& target, int x, int y) {
    std::vector<std::uint8_t> rgba;
    target.readPixels(rgba);
    const auto i = static_cast<std::size_t>(y * target.width() + x) * 4;
    return {rgba[i], rgba[i + 1], rgba[i + 2]};
  }

  // Runs `controls` (no smoothing) over `source` into a fresh output.
  std::unique_ptr<GlFrameTarget> run(const GlFrameTarget& source, const core::VisualControls& controls,
                                     float angle = 0.0f) {
    EffectsState state;
    state.setImmediate(controls, angle);
    auto output = std::make_unique<GlFrameTarget>(source.width(), source.height());
    chain->apply(state, source.texture(), *output);
    return output;
  }
};

void reportUnavailable(const std::string& reason) {
  if (kUnderTsan) {
    SUCCEED("skipped under ThreadSanitizer: " + reason);
    return;
  }
  if (juce::SystemStats::getEnvironmentVariable("MILKDAWP_REQUIRE_HEADLESS_RENDER", {}).isNotEmpty()) {
    FAIL("headless render required (MILKDAWP_REQUIRE_HEADLESS_RENDER) but unavailable: " + reason);
  }
  SUCCEED("effects chain unavailable here: " + reason);
}

constexpr int kTolerance = 3;

void checkNear(const Rgb& actual, const Rgb& expected) {
  INFO("actual " << actual.r << "," << actual.g << "," << actual.b << "  expected " << expected.r << ","
                 << expected.g << "," << expected.b);
  CHECK(std::abs(actual.r - expected.r) <= kTolerance);
  CHECK(std::abs(actual.g - expected.g) <= kTolerance);
  CHECK(std::abs(actual.b - expected.b) <= kTolerance);
}

const Rgb kRed{200, 0, 0};
const Rgb kBlue{0, 0, 200};

// Left half red, right half blue.
std::unique_ptr<GlFrameTarget> halves(int size) {
  return EffectsRig::picture(size, size, [size](int x, int) { return x < size / 2 ? kRed : kBlue; });
}

} // namespace

TEST_CASE("EffectsState: neutral controls need no drawing at all", "[engine][effects]") {
  // No GL needed: the decision is made before any pass would run.
  EffectsState state;
  CHECK_FALSE(state.advance(core::VisualControls{}, 1.0f / 60.0f));

  core::VisualControls glow;
  glow.glow = 0.5f;
  CHECK(state.advance(glow, 1.0f / 60.0f));
  // Back to neutral: within half a second the chain is off again.
  bool active = true;
  for (int i = 0; i < 30; ++i) {
    active = state.advance(core::VisualControls{}, 1.0f / 60.0f);
  }
  CHECK_FALSE(active);
}

TEST_CASE("EffectsState: Rotation turns the frame, and it settles upright after", "[engine][effects]") {
  EffectsState state;
  core::VisualControls spin;
  spin.rotation = 1.0f;
  state.setImmediate(spin);
  state.advance(spin, 0.25f); // half a turn per second
  CHECK(std::abs(state.angleRadians() - std::numbers::pi_v<float> / 4.0f) < 1.0e-3f);

  bool active = true;
  for (int i = 0; i < 180; ++i) {
    active = state.advance(core::VisualControls{}, 1.0f / 60.0f);
  }
  CHECK(state.angleRadians() == 0.0f);
  CHECK_FALSE(active);
}

TEST_CASE("EffectsChain: brightness, saturation and hue", "[engine][effects]") {
  EffectsRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto source = EffectsRig::solid(8, {200, 100, 50});

  SECTION("neutral copies the picture") {
    checkNear(EffectsRig::at(*rig.run(*source, {}), 4, 4), {200, 100, 50});
  }
  SECTION("brightness scales") {
    core::VisualControls c;
    c.brightness = 0.5f;
    checkNear(EffectsRig::at(*rig.run(*source, c), 4, 4), {100, 50, 25});
  }
  SECTION("saturation 0 is the luma grey") {
    core::VisualControls c;
    c.saturation = 0.0f;
    // 0.2126 * 200 + 0.7152 * 100 + 0.0722 * 50 = 117.6
    checkNear(EffectsRig::at(*rig.run(*source, c), 4, 4), {118, 118, 118});
  }
  SECTION("hue +120 degrees takes red to green") {
    const auto red = EffectsRig::solid(8, kRed);
    core::VisualControls c;
    c.hueDegrees = 120.0f;
    checkNear(EffectsRig::at(*rig.run(*red, c), 4, 4), {0, 200, 0});
  }
}

TEST_CASE("EffectsChain: mirror, rotation and zoom move the picture", "[engine][effects]") {
  EffectsRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  constexpr int kSize = 16;
  const auto source = halves(kSize);

  SECTION("left-right mirror shows the left half on both sides") {
    core::VisualControls c;
    c.mirror = core::MirrorMode::LeftRight;
    const auto out = rig.run(*source, c);
    checkNear(EffectsRig::at(*out, 2, 8), kRed);
    checkNear(EffectsRig::at(*out, 13, 8), kRed);
  }
  SECTION("half a turn swaps the sides") {
    const auto out = rig.run(*source, {}, std::numbers::pi_v<float>);
    checkNear(EffectsRig::at(*out, 2, 8), kBlue);
    checkNear(EffectsRig::at(*out, 13, 8), kRed);
  }
  SECTION("zoom 1 doubles the size about the centre") {
    // Four stripes, red green blue white; doubled, the left edge shows green.
    const auto stripes = EffectsRig::picture(kSize, kSize, [](int x, int) {
      switch (x / 4) {
      case 0: return Rgb{200, 0, 0};
      case 1: return Rgb{0, 200, 0};
      case 2: return Rgb{0, 0, 200};
      default: return Rgb{200, 200, 200};
      }
    });
    core::VisualControls c;
    c.zoom = 1.0f;
    const auto out = rig.run(*stripes, c);
    // Pixel 1 samples source x 4.75 and pixel 14 x 11.25: both well inside a stripe.
    checkNear(EffectsRig::at(*out, 1, 8), {0, 200, 0});
    checkNear(EffectsRig::at(*out, 14, 8), {0, 0, 200});
  }
}

TEST_CASE("EffectsChain: pixelate makes blocks", "[engine][effects]") {
  EffectsRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  constexpr int kSize = 64; // pixelate 1 = blocks of 1 + 64 / 16 = 5 pixels
  const auto gradient = EffectsRig::picture(kSize, kSize, [](int x, int) { return Rgb{x * 4, 0, 0}; });
  core::VisualControls c;
  c.pixelate = 1.0f;
  const auto out = rig.run(*gradient, c);
  const auto first = EffectsRig::at(*out, 0, 10);
  checkNear(EffectsRig::at(*out, 4, 10), first); // one block
  checkNear(first, {2 * 4, 0, 0});               // its centre pixel
  checkNear(EffectsRig::at(*out, 5, 10), {7 * 4, 0, 0}); // the next block
}

TEST_CASE("EffectsChain: blur and glow spread light", "[engine][effects]") {
  EffectsRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  constexpr int kSize = 160; // blur 1 = taps 1 px apart, +-4 px
  const auto line = EffectsRig::picture(kSize, kSize, [](int x, int) {
    return x == 80 ? Rgb{255, 255, 255} : Rgb{0, 0, 0};
  });

  SECTION("blur") {
    core::VisualControls c;
    c.blur = 1.0f;
    const auto out = rig.run(*line, c);
    const auto centre = EffectsRig::at(*out, 80, 80);
    CHECK(centre.r < 255);
    CHECK(centre.r > 30); // the centre weight is 0.204
    CHECK(EffectsRig::at(*out, 82, 80).r > 10);
    CHECK(EffectsRig::at(*out, 100, 80).r == 0); // nothing reaches 20 px away
  }
  SECTION("glow brightens around bright parts and leaves dim ones alone") {
    const auto square = EffectsRig::picture(kSize, kSize, [](int x, int y) {
      return (x >= 72 && x < 88 && y >= 72 && y < 88) ? Rgb{255, 255, 255} : Rgb{60, 60, 60};
    });
    core::VisualControls c;
    c.glow = 1.0f;
    const auto out = rig.run(*square, c);
    CHECK(EffectsRig::at(*out, 92, 80).r > 70);       // next to the square: lit up
    checkNear(EffectsRig::at(*out, 10, 10), {60, 60, 60}); // far away and dim: as it was
    checkNear(EffectsRig::at(*out, 80, 80), {255, 255, 255});
  }
}

TEST_CASE("EffectsChain: Trails keep the previous frame, fading", "[engine][effects]") {
  EffectsRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto white = EffectsRig::solid(8, {255, 255, 255});
  const auto black = EffectsRig::solid(8, {0, 0, 0});
  core::VisualControls c;
  c.trails = 1.0f;
  EffectsState state;
  state.setImmediate(c);
  GlFrameTarget output(8, 8);

  rig.chain->apply(state, white->texture(), output);
  checkNear(EffectsRig::at(output, 4, 4), {255, 255, 255});
  rig.chain->apply(state, black->texture(), output);
  const int faded = static_cast<int>(std::lround(255.0f * EffectsChain::kMaxTrailsDecay));
  checkNear(EffectsRig::at(output, 4, 4), {faded, faded, faded});
  rig.chain->apply(state, black->texture(), output);
  const int twice = static_cast<int>(std::lround(faded * EffectsChain::kMaxTrailsDecay));
  checkNear(EffectsRig::at(output, 4, 4), {twice, twice, twice});
}

TEST_CASE("EffectsChain: several effects at once, and the GL state is put back", "[engine][effects]") {
  using namespace ::juce::gl;
  EffectsRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto source = EffectsRig::solid(32, {200, 100, 50});
  GlFrameTarget other(4, 4);
  glBindFramebuffer(GL_FRAMEBUFFER, other.framebuffer());
  glViewport(1, 2, 3, 4);
  glEnable(GL_BLEND);
  glUseProgram(0);

  core::VisualControls c;
  c.brightness = 0.5f;
  c.blur = 0.5f;
  c.glow = 0.2f;
  c.trails = 0.5f;
  const auto out = rig.run(*source, c);
  checkNear(EffectsRig::at(*out, 16, 16), {100, 50, 25}); // a flat picture: blur and dim glow change nothing

  GLint framebuffer = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
  CHECK(static_cast<std::uint32_t>(framebuffer) == other.framebuffer());
  GLint viewport[4] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  CHECK(viewport[0] == 1);
  CHECK(viewport[3] == 4);
  CHECK(glIsEnabled(GL_BLEND) == GL_TRUE);
  GLint program = -1;
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  CHECK(program == 0);
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
