// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Layers L2: the compositor's blend maths, on a real GL context with no
// projectM. Layers are filled with known colours, composed, and the canvas is
// read back. Like the headless render tests, these report why and pass where
// no offscreen context exists, unless MILKDAWP_REQUIRE_HEADLESS_RENDER is set.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/engine/GlFrameTarget.h"
#include "milkdawp/engine/LayerCompositor.h"
#include "milkdawp/engine/OffscreenGLContext.h"

using namespace milkdawp::engine;

namespace {

constexpr int kSize = 8;

struct Rgb {
  int r = 0;
  int g = 0;
  int b = 0;
};

struct CompositorRig {
  std::unique_ptr<OffscreenGLContext> context;
  std::unique_ptr<LayerCompositor> compositor;
  std::unique_ptr<GlFrameTarget> canvas;
  std::string skipReason;

  CompositorRig() {
    auto created = OffscreenGLContext::create();
    if (!created.context) {
      skipReason = created.error;
      return;
    }
    context = std::move(created.context);
    juce::gl::loadFunctions();
    compositor = std::make_unique<LayerCompositor>();
    if (!compositor->ok()) {
      skipReason = compositor->error();
      return;
    }
    canvas = std::make_unique<GlFrameTarget>(kSize, kSize);
  }

  ~CompositorRig() {
    canvas.reset();
    compositor.reset();
    context.reset();
  }

  [[nodiscard]] bool ready() const { return canvas != nullptr; }

  // A layer texture filled with one colour (0..255 per channel).
  static std::unique_ptr<GlFrameTarget> solid(int r, int g, int b) {
    using namespace ::juce::gl;
    auto target = std::make_unique<GlFrameTarget>(kSize, kSize);
    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer());
    glClearColor(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return target;
  }

  // The canvas's centre pixel, and its alpha.
  Rgb centre(int* alpha = nullptr) const {
    std::vector<std::uint8_t> rgba;
    canvas->readPixels(rgba);
    const std::size_t i = (static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2) * 4;
    if (alpha != nullptr) {
      *alpha = rgba[i + 3];
    }
    return {rgba[i], rgba[i + 1], rgba[i + 2]};
  }
};

void reportUnavailable(const std::string& reason) {
  if (juce::SystemStats::getEnvironmentVariable("MILKDAWP_REQUIRE_HEADLESS_RENDER", {}).isNotEmpty()) {
    FAIL("headless render required (MILKDAWP_REQUIRE_HEADLESS_RENDER) but unavailable: " + reason);
  }
  SUCCEED("compositor unavailable here: " + reason);
}

constexpr int kTolerance = 3; // 8-bit rounding through blend and readback

void checkNear(const Rgb& actual, const Rgb& expected) {
  CHECK(std::abs(actual.r - expected.r) <= kTolerance);
  CHECK(std::abs(actual.g - expected.g) <= kTolerance);
  CHECK(std::abs(actual.b - expected.b) <= kTolerance);
}

} // namespace

TEST_CASE("The layerBlend parameter's choices line up with LayerBlend", "[engine][layers]") {
  // The plugin hands the parameter's choice index to the engine as a LayerBlend,
  // so the two lists must stay in the same order.
  const auto* spec = milkdawp::core::findParameter(milkdawp::core::allParameters(), "layerBlend");
  REQUIRE(spec != nullptr);
  REQUIRE(spec->choices.size() == static_cast<std::size_t>(kLayerBlendCount));
  CHECK(spec->choices[static_cast<std::size_t>(LayerBlend::Normal)] == "Normal");
  CHECK(spec->choices[static_cast<std::size_t>(LayerBlend::Add)] == "Add");
  CHECK(spec->choices[static_cast<std::size_t>(LayerBlend::Screen)] == "Screen");
  CHECK(spec->choices[static_cast<std::size_t>(LayerBlend::Multiply)] == "Multiply");
  CHECK(spec->choices[static_cast<std::size_t>(LayerBlend::LumaKey)] == "Luma key");
}

TEST_CASE("LayerCompositor: luma key makes a layer's dark areas transparent", "[engine][layers][compositor]") {
  CompositorRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto base = CompositorRig::solid(200, 0, 0);

  SECTION("black shows what is below") {
    const auto black = CompositorRig::solid(0, 0, 0);
    const std::array layers{LayerDraw{base->texture(), 1.0f, LayerBlend::Normal},
                            LayerDraw{black->texture(), 1.0f, LayerBlend::LumaKey}};
    rig.compositor->compose(*rig.canvas, layers);
    checkNear(rig.centre(), {200, 0, 0});
  }
  SECTION("a bright layer covers it") {
    const auto white = CompositorRig::solid(255, 255, 255);
    const std::array layers{LayerDraw{base->texture(), 1.0f, LayerBlend::Normal},
                            LayerDraw{white->texture(), 1.0f, LayerBlend::LumaKey}};
    rig.compositor->compose(*rig.canvas, layers);
    checkNear(rig.centre(), {255, 255, 255});
  }
  SECTION("a dim layer is partly there, in proportion to its brightness") {
    // Grey 40/255 has luma 0.157; alpha = 4 * 0.157 = 0.627, so 0.627*40 + 0.373*(200,0,0).
    const auto dim = CompositorRig::solid(40, 40, 40);
    const std::array layers{LayerDraw{base->texture(), 1.0f, LayerBlend::Normal},
                            LayerDraw{dim->texture(), 1.0f, LayerBlend::LumaKey}};
    rig.compositor->compose(*rig.canvas, layers);
    checkNear(rig.centre(), {100, 25, 25});
  }
  SECTION("opacity still scales the keyed layer") {
    const auto white = CompositorRig::solid(255, 255, 255);
    const std::array layers{LayerDraw{base->texture(), 1.0f, LayerBlend::Normal},
                            LayerDraw{white->texture(), 0.5f, LayerBlend::LumaKey}};
    rig.compositor->compose(*rig.canvas, layers);
    checkNear(rig.centre(), {228, 128, 128}); // halfway between (200,0,0) and white
  }
}

TEST_CASE("LayerCompositor: no layers gives an opaque black canvas", "[engine][layers][compositor]") {
  CompositorRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  rig.compositor->compose(*rig.canvas, {});
  int alpha = 0;
  checkNear(rig.centre(&alpha), {0, 0, 0});
  CHECK(alpha == 255);
}

TEST_CASE("LayerCompositor: a fully opaque normal layer replaces what is below", "[engine][layers][compositor]") {
  CompositorRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto red = CompositorRig::solid(200, 0, 0);
  const auto green = CompositorRig::solid(0, 180, 0);
  const std::array layers{LayerDraw{red->texture(), 1.0f, LayerBlend::Normal},
                          LayerDraw{green->texture(), 1.0f, LayerBlend::Normal}};
  rig.compositor->compose(*rig.canvas, layers);
  int alpha = 0;
  checkNear(rig.centre(&alpha), {0, 180, 0});
  CHECK(alpha == 255); // layers never make the canvas translucent
}

TEST_CASE("LayerCompositor: normal opacity mixes in proportion", "[engine][layers][compositor]") {
  CompositorRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto red = CompositorRig::solid(200, 0, 0);
  const auto green = CompositorRig::solid(0, 200, 0);
  const std::array half{LayerDraw{red->texture(), 1.0f, LayerBlend::Normal},
                        LayerDraw{green->texture(), 0.5f, LayerBlend::Normal}};
  rig.compositor->compose(*rig.canvas, half);
  checkNear(rig.centre(), {100, 100, 0});

  const std::array none{LayerDraw{red->texture(), 1.0f, LayerBlend::Normal},
                        LayerDraw{green->texture(), 0.0f, LayerBlend::Normal}};
  rig.compositor->compose(*rig.canvas, none);
  checkNear(rig.centre(), {200, 0, 0}); // opacity 0 changes nothing
}

TEST_CASE("LayerCompositor: a lone translucent layer fades in from black", "[engine][layers][compositor]") {
  CompositorRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto blue = CompositorRig::solid(0, 0, 200);
  const std::array layers{LayerDraw{blue->texture(), 0.25f, LayerBlend::Normal}};
  rig.compositor->compose(*rig.canvas, layers);
  checkNear(rig.centre(), {0, 0, 50});
}

TEST_CASE("LayerCompositor: add, screen and multiply", "[engine][layers][compositor]") {
  CompositorRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto base = CompositorRig::solid(100, 0, 128);
  const auto top = CompositorRig::solid(0, 100, 128);

  SECTION("add sums the light") {
    const std::array layers{LayerDraw{base->texture(), 1.0f, LayerBlend::Normal},
                            LayerDraw{top->texture(), 1.0f, LayerBlend::Add}};
    rig.compositor->compose(*rig.canvas, layers);
    checkNear(rig.centre(), {100, 100, 255}); // 128 + 128 clips
  }
  SECTION("add scales with opacity") {
    const std::array layers{LayerDraw{base->texture(), 1.0f, LayerBlend::Normal},
                            LayerDraw{top->texture(), 0.5f, LayerBlend::Add}};
    rig.compositor->compose(*rig.canvas, layers);
    checkNear(rig.centre(), {100, 50, 192});
  }
  SECTION("screen is 1 - (1 - a)(1 - b)") {
    const auto grey = CompositorRig::solid(128, 128, 128);
    const std::array layers{LayerDraw{grey->texture(), 1.0f, LayerBlend::Normal},
                            LayerDraw{grey->texture(), 1.0f, LayerBlend::Screen}};
    rig.compositor->compose(*rig.canvas, layers);
    checkNear(rig.centre(), {191, 191, 191});
  }
  SECTION("multiply darkens, and fades out with opacity") {
    const auto grey = CompositorRig::solid(128, 128, 128);
    const std::array full{LayerDraw{grey->texture(), 1.0f, LayerBlend::Normal},
                          LayerDraw{grey->texture(), 1.0f, LayerBlend::Multiply}};
    rig.compositor->compose(*rig.canvas, full);
    checkNear(rig.centre(), {64, 64, 64});

    const std::array half{LayerDraw{grey->texture(), 1.0f, LayerBlend::Normal},
                          LayerDraw{grey->texture(), 0.5f, LayerBlend::Multiply}};
    rig.compositor->compose(*rig.canvas, half);
    checkNear(rig.centre(), {96, 96, 96}); // 0.5 * (1 - 0.5 + 0.5 * 0.5)
  }
}

TEST_CASE("LayerCompositor puts back the GL state projectM relies on", "[engine][layers][compositor]") {
  using namespace ::juce::gl;
  CompositorRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto layer = CompositorRig::solid(10, 20, 30);

  glBindFramebuffer(GL_FRAMEBUFFER, layer->framebuffer());
  glViewport(1, 2, 3, 4);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glUseProgram(0);

  const std::array layers{LayerDraw{layer->texture(), 1.0f, LayerBlend::Add}};
  rig.compositor->compose(*rig.canvas, layers);

  GLint framebuffer = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
  CHECK(static_cast<std::uint32_t>(framebuffer) == layer->framebuffer());
  GLint viewport[4] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  CHECK(viewport[0] == 1);
  CHECK(viewport[1] == 2);
  CHECK(viewport[2] == 3);
  CHECK(viewport[3] == 4);
  CHECK(glIsEnabled(GL_BLEND) == GL_FALSE);
  CHECK(glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
  GLint program = -1;
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  CHECK(program == 0);

  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
