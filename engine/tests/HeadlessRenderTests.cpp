// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 2.7: render real presets into an FBO with no window at all, read the
// pixels back, and check that something is drawn and that it animates.
// Runs wherever OffscreenGLContext works (Windows WGL; Linux EGL surfaceless,
// e.g. Mesa llvmpipe in the devcontainer) and projectM 4.2 is next to the
// test binary; otherwise it reports why and passes, like the other engine
// tests.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
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

  HeadlessRig() {
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

} // namespace

TEST_CASE("Headless render: each fixture preset draws a non-black frame that changes over time (2.7)",
          "[engine][headless]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    SUCCEED("headless render unavailable here: " + rig.skipReason);
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
    SUCCEED("headless render unavailable here: " + skip);
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

TEST_CASE("Headless render: a preset projectM rejects fires the failure callback", "[engine][headless]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    SUCCEED("headless render unavailable here: " + rig.skipReason);
    return;
  }
  bool failed = false;
  rig.instance->setPresetSwitchFailedCallback(
      [](const char*, const char*, void* userData) { *static_cast<bool*>(userData) = true; }, &failed);
  rig.instance->loadPresetFile("does/not/exist/anywhere.milk", false);
  CHECK(failed);
}
