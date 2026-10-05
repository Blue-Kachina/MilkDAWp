// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 2.7: render real presets into an FBO with no window at all, read the
// pixels back, and check that something is drawn and that it animates.
// Runs wherever OffscreenGLContext works (Windows WGL; Linux EGL surfaceless,
// e.g. Mesa llvmpipe in the devcontainer) and projectM 4.2 is next to the
// test binary; otherwise it reports why and passes, like the other engine
// tests -- unless MILKDAWP_REQUIRE_HEADLESS_RENDER is set, as in Linux CI.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/engine/GlFrameTarget.h"
#include "milkdawp/engine/OffscreenGLContext.h"
#include "milkdawp/engine/ProjectMInstance.h"
#include "milkdawp/engine/ProjectMLibrary.h"

using namespace milkdawp::engine;

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 144;
constexpr double kFps = 60.0;

std::string readPreset(const char* name) {
  const auto file = juce::File(MILKDAWP_FIXTURES_DIR).getChildFile("presets").getChildFile(name);
  REQUIRE(file.existsAsFile());
  return file.loadFileAsString().toStdString();
}

// Mean of R+G+B over the frame, 0..765.
double meanBrightness(const std::vector<std::uint8_t>& rgba) {
  double sum = 0.0;
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    sum += rgba[i] + rgba[i + 1] + rgba[i + 2];
  }
  return sum / static_cast<double>(rgba.size() / 4);
}

// Mean absolute per-channel difference between two frames, 0..255.
double meanDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
    if (i % 4 == 3) {
      continue; // alpha
    }
    sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
  }
  return sum / static_cast<double>(a.size() / 4 * 3);
}

// Everything a headless render needs, set up on the calling thread.
struct HeadlessRig {
  std::unique_ptr<OffscreenGLContext> context;
  std::unique_ptr<ProjectMLibrary> library;
  std::unique_ptr<GlFrameTarget> target;
  std::unique_ptr<ProjectMInstance> instance;
  std::string skipReason;

  explicit HeadlessRig(std::vector<std::string> textureSearchPaths = {}) {
    auto loaded = ProjectMLibrary::load();
    if (!loaded.library) {
      skipReason = loaded.unavailableReason;
      return;
    }
    library = std::move(loaded.library);
    auto created = OffscreenGLContext::create();
    if (!created.context) {
      skipReason = created.error;
      return;
    }
    context = std::move(created.context);
    juce::gl::loadFunctions();
    target = std::make_unique<GlFrameTarget>(kWidth, kHeight);

    ProjectMInstance::Settings settings;
    settings.width = kWidth;
    settings.height = kHeight;
    settings.textureSearchPaths = std::move(textureSearchPaths);
    std::string error;
    instance = ProjectMInstance::create(*library, settings, error);
    if (!instance) {
      skipReason = error;
    }
  }

  ~HeadlessRig() {
    // GL objects go before the context, on this (the owning) thread.
    instance.reset();
    target.reset();
    context.reset();
  }

  [[nodiscard]] bool ready() const { return instance != nullptr; }

  // Feeds one frame's worth of a 220 Hz sine (stereo), renders frame `n` at
  // a fixed 60 fps timeline, and reads the result back.
  std::vector<std::uint8_t> renderFrame(int n) {
    constexpr int samplesPerFrame = 800; // 48 kHz / 60 fps
    std::vector<float> pcm(static_cast<std::size_t>(samplesPerFrame) * 2);
    for (int i = 0; i < samplesPerFrame; ++i) {
      const double t = (n * samplesPerFrame + i) / 48000.0;
      const auto sample = static_cast<float>(0.8 * std::sin(2.0 * 3.14159265358979 * 220.0 * t));
      pcm[static_cast<std::size_t>(i) * 2] = sample;
      pcm[static_cast<std::size_t>(i) * 2 + 1] = sample;
    }
    instance->addPcm(pcm.data(), samplesPerFrame, 2);
    instance->setFrameTime(n / kFps);
    instance->renderTo(target->framebuffer());
    juce::gl::glFinish();
    std::vector<std::uint8_t> rgba;
    target->readPixels(rgba);
    return rgba;
  }
};

// Where the render path must work (the Linux CI job sets this), "unavailable"
// is a failure, not a pass -- otherwise a broken context silently turns these
// tests into no-ops, as the EGL default-display bug once did.
void reportUnavailable(const std::string& reason) {
  if (juce::SystemStats::getEnvironmentVariable("MILKDAWP_REQUIRE_HEADLESS_RENDER", {}).isNotEmpty()) {
    FAIL("headless render required (MILKDAWP_REQUIRE_HEADLESS_RENDER) but unavailable: " + reason);
  }
  SUCCEED("headless render unavailable here: " + reason);
}

} // namespace

TEST_CASE("Headless render: each fixture preset draws a non-black frame that changes over time (2.7)",
          "[engine][headless]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }

  for (const char* name : {"mdw-border.milk", "mdw-wave.milk", "mdw-feedback.milk"}) {
    INFO("preset: " << name);
    const auto text = readPreset(name);
    rig.instance->loadPresetData(text.c_str(), /*smoothTransition=*/false);

    std::vector<std::uint8_t> previous;
    double brightest = 0.0;
    double largestChange = 0.0;
    for (int frame = 0; frame < 30; ++frame) {
      const auto pixels = rig.renderFrame(frame);
      brightest = std::max(brightest, meanBrightness(pixels));
      if (!previous.empty()) {
        largestChange = std::max(largestChange, meanDifference(previous, pixels));
      }
      previous = pixels;
    }
    CHECK(brightest > 3.0);     // not black
    CHECK(largestChange > 0.1); // animates
  }
}

TEST_CASE("Headless render: explicit frame time drives time-based preset content (2.7, set_frame_time)",
          "[engine][headless]") {
  // mdw-border's border colour is a pure function of `time`, drawn opaque
  // over the whole border every frame, so a pixel in the border is exactly
  // reproducible from the frame time alone. (Whole frames are not: projectM
  // seeds some per-instance randomness, e.g. rand_start/rand_preset and its
  // noise textures, from the clock.)
  auto cornerPixelsAt = [](const std::vector<int>& frames, std::vector<std::uint32_t>& out) {
    HeadlessRig rig;
    if (!rig.ready()) {
      return rig.skipReason;
    }
    const auto text = readPreset("mdw-border.milk");
    rig.instance->loadPresetData(text.c_str(), false);
    int next = 0;
    for (const int wanted : frames) {
      std::vector<std::uint8_t> pixels;
      while (next <= wanted) {
        pixels = rig.renderFrame(next++);
      }
      // (4, 4) from the bottom-left: well inside a 20% border.
      const std::size_t i = (static_cast<std::size_t>(4) * kWidth + 4) * 4;
      out.push_back(static_cast<std::uint32_t>(pixels[i]) << 16U | static_cast<std::uint32_t>(pixels[i + 1]) << 8U |
                    pixels[i + 2]);
    }
    return std::string{};
  };

  const std::vector<int> frames{10, 40, 70};
  std::vector<std::uint32_t> first;
  std::vector<std::uint32_t> second;
  const auto skip = cornerPixelsAt(frames, first);
  if (!skip.empty()) {
    reportUnavailable(skip);
    return;
  }
  cornerPixelsAt(frames, second);
  REQUIRE(first.size() == frames.size());
  REQUIRE(second.size() == frames.size());

  auto channelDistance = [](std::uint32_t a, std::uint32_t b) {
    int worst = 0;
    for (const unsigned shift : {16U, 8U, 0U}) {
      worst = std::max(worst, std::abs(static_cast<int>((a >> shift) & 0xFFU) - static_cast<int>((b >> shift) & 0xFFU)));
    }
    return worst;
  };
  for (std::size_t k = 0; k < frames.size(); ++k) {
    INFO("frame " << frames[k]);
    CHECK(channelDistance(first[k], second[k]) <= 2); // same time -> same colour
  }
  // And the colour really follows time (otherwise the check above is vacuous).
  CHECK(channelDistance(first[0], first[1]) > 10);
  CHECK(channelDistance(first[1], first[2]) > 10);
}

TEST_CASE("Headless render: two projectM instances share one context and render to their own FBOs (Layers L1)",
          "[engine][headless][layers]") {
  // Layers puts N projectM instances in the engine's one GL context, each
  // rendering to its own FBO (4.2's render_frame_fbo). This is the risk that
  // design rests on: that rendering B neither draws into A's target nor
  // disturbs A's time-driven output. mdw-border's border colour is a pure
  // function of frame time, so A's corner pixel is reproducible, with or
  // without B rendering in between.
  constexpr int frames = 71;
  constexpr std::size_t corner = (static_cast<std::size_t>(4) * kWidth + 4) * 4;
  auto colourAt = [&](const std::vector<std::uint8_t>& pixels) {
    return static_cast<std::uint32_t>(pixels[corner]) << 16U | static_cast<std::uint32_t>(pixels[corner + 1]) << 8U |
           pixels[corner + 2];
  };

  // A alone, as the reference.
  std::vector<std::uint32_t> alone;
  {
    HeadlessRig rig;
    if (!rig.ready()) {
      reportUnavailable(rig.skipReason);
      return;
    }
    const auto text = readPreset("mdw-border.milk");
    rig.instance->loadPresetData(text.c_str(), false);
    for (int n = 0; n < frames; ++n) {
      alone.push_back(colourAt(rig.renderFrame(n)));
    }
  }

  // A and B interleaved in one context.
  HeadlessRig rig;
  REQUIRE(rig.ready());
  ProjectMInstance::Settings settings;
  settings.width = kWidth;
  settings.height = kHeight;
  std::string error;
  auto second = ProjectMInstance::create(*rig.library, settings, error);
  REQUIRE(second != nullptr);
  GlFrameTarget secondTarget(kWidth, kHeight);

  const auto border = readPreset("mdw-border.milk");
  const auto wave = readPreset("mdw-wave.milk");
  rig.instance->loadPresetData(border.c_str(), false);
  second->loadPresetData(wave.c_str(), false);

  std::vector<std::uint32_t> interleaved;
  std::vector<std::uint8_t> firstPixels;
  std::vector<std::uint8_t> secondPixels;
  double secondBrightest = 0.0;
  for (int n = 0; n < frames; ++n) {
    firstPixels = rig.renderFrame(n);
    interleaved.push_back(colourAt(firstPixels));

    constexpr int samplesPerFrame = 800;
    std::vector<float> pcm(static_cast<std::size_t>(samplesPerFrame) * 2, 0.5f);
    second->addPcm(pcm.data(), samplesPerFrame, 2);
    second->setFrameTime(n / kFps);
    second->renderTo(secondTarget.framebuffer());
    juce::gl::glFinish();
    secondTarget.readPixels(secondPixels);
    secondBrightest = std::max(secondBrightest, meanBrightness(secondPixels));
  }

  CHECK(secondBrightest > 3.0); // B draws something into its own target
  CHECK(meanDifference(firstPixels, secondPixels) > 1.0); // and it is not a copy of A's frame

  auto channelDistance = [](std::uint32_t a, std::uint32_t b) {
    int worst = 0;
    for (const unsigned shift : {16U, 8U, 0U}) {
      worst = std::max(worst, std::abs(static_cast<int>((a >> shift) & 0xFFU) - static_cast<int>((b >> shift) & 0xFFU)));
    }
    return worst;
  };
  for (const int n : {10, 40, 70}) {
    INFO("frame " << n);
    CHECK(channelDistance(alone[static_cast<std::size_t>(n)], interleaved[static_cast<std::size_t>(n)]) <= 2);
  }

  second.reset(); // GL objects before the context, which `rig` destroys
}

TEST_CASE("Headless render: a preset projectM rejects fires the failure callback", "[engine][headless]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  bool failed = false;
  rig.instance->setPresetSwitchFailedCallback(
      [](const char*, const char*, void* userData) { *static_cast<bool*>(userData) = true; }, &failed);
  rig.instance->loadPresetFile("does/not/exist/anywhere.milk", false);
  CHECK(failed);
}

// 6.1: the bundled texture pack reaches projectM. The composite shader just
// shows the pack's worms.jpg, so the frame is the texture when projectM
// finds it and projectM's stand-in when it doesn't.
TEST_CASE("Headless render: presets find the bundled textures (6.1)", "[engine][headless][content]") {
#ifndef MILKDAWP_TEST_CONTENT_DIR
  SKIP("built without the bundled content (MILKDAWP_BUNDLE_CONTENT=OFF)");
#else
  const auto textures = juce::File(MILKDAWP_TEST_CONTENT_DIR).getChildFile("Textures");
  REQUIRE(textures.getChildFile("worms.jpg").existsAsFile());

  static constexpr const char* kTexturePreset = "[preset00]\n"
                                                "MILKDROP_PRESET_VERSION=201\n"
                                                "PSVERSION=2\n"
                                                "PSVERSION_WARP=2\n"
                                                "PSVERSION_COMP=2\n"
                                                "fDecay=1.0\n"
                                                "comp_1=`sampler sampler_worms;\n"
                                                "comp_2=`shader_body\n"
                                                "comp_3=`{\n"
                                                "comp_4=`    ret = tex2D(sampler_worms, uv).xyz;\n"
                                                "comp_5=`}\n";

  auto render = [](std::vector<std::string> paths) {
    HeadlessRig rig(std::move(paths));
    if (!rig.ready()) {
      return std::pair{std::vector<std::uint8_t>{}, rig.skipReason};
    }
    rig.instance->loadPresetData(kTexturePreset, false);
    std::vector<std::uint8_t> frame;
    for (int n = 0; n < 5; ++n) {
      frame = rig.renderFrame(n);
    }
    return std::pair{frame, std::string{}};
  };

  const auto [with, reason] = render({textures.getFullPathName().toStdString()});
  if (with.empty()) {
    reportUnavailable(reason);
    return;
  }
  [[maybe_unused]] const auto [without, unusedReason] = render({});
  REQUIRE_FALSE(without.empty());

  // worms.jpg is a mid-grey pattern; a missing texture is a flat stand-in.
  INFO("with textures " << meanBrightness(with) << ", without " << meanBrightness(without));
  CHECK(meanBrightness(with) > 30.0);
  CHECK(meanDifference(with, without) > 10.0);
#endif
}
