// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 2.7: render real presets into an FBO with no window at all, read the
// pixels back, and check that something is drawn and that it animates.
// Runs wherever OffscreenGLContext works (Windows WGL; Linux EGL surfaceless,
// e.g. Mesa llvmpipe in the devcontainer) and projectM 4.2 is next to the
// test binary; otherwise it reports why and passes, like the other engine
// tests -- unless MILKDAWP_REQUIRE_HEADLESS_RENDER is set, as in Linux CI.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/core/MilkdawpPreset.h"
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

// 8.7 (ADR-0012): projectm_set_preset_variable, our projectM patch. Each preset
// below turns a variable into its outer border colour, which is drawn opaque
// every frame and reaches the output unchanged through a plain composite
// shader, so the border pixel reads the variable back (0..1 -> 0..255).
namespace {

std::string borderPreset(const std::string& perFrameInit, const std::string& perFrame, const std::string& perPixel) {
  std::string text = "[preset00]\n"
                     "MILKDROP_PRESET_VERSION=201\n"
                     "PSVERSION=2\n"
                     "PSVERSION_WARP=2\n"
                     "PSVERSION_COMP=2\n"
                     "fDecay=0.9\n"
                     "zoom=1.0\n"
                     "rot=0.0\n"
                     "warp=0\n"
                     "wave_a=0\n"
                     "ob_size=0.2\n"
                     "ob_r=0\n"
                     "ob_g=0\n"
                     "ob_b=0\n"
                     "ob_a=1\n";
  auto addLines = [&text](const char* key, const std::string& code) {
    int n = 1;
    std::size_t start = 0;
    while (start < code.size()) {
      const auto end = code.find('\n', start);
      text += key + std::to_string(n++) + "=" + code.substr(start, end - start) + "\n";
      start = end == std::string::npos ? code.size() : end + 1;
    }
  };
  addLines("per_frame_init_", perFrameInit);
  addLines("per_frame_", perFrame);
  addLines("per_pixel_", perPixel);
  text += "comp_1=`shader_body\n"
          "comp_2=`{\n"
          "comp_3=`ret = tex2D(sampler_main, uv).xyz;\n"
          "comp_4=`}\n";
  return text;
}

struct Rgb {
  int r = 0;
  int g = 0;
  int b = 0;
};

// (4, 4) from the bottom-left: well inside the 20% border.
Rgb borderPixel(const std::vector<std::uint8_t>& rgba) {
  const std::size_t i = (static_cast<std::size_t>(4) * kWidth + 4) * 4;
  return {rgba[i], rgba[i + 1], rgba[i + 2]};
}

// Mean of one channel over the frame, 0..255.
double meanChannel(const std::vector<std::uint8_t>& rgba, std::size_t channel) {
  double sum = 0.0;
  for (std::size_t i = channel; i < rgba.size(); i += 4) {
    sum += rgba[i];
  }
  return sum / static_cast<double>(rgba.size() / 4);
}

constexpr int kColourTolerance = 4;

bool matchesColour(int actual, double expected01) {
  return std::abs(actual - static_cast<int>(std::lround(expected01 * 255.0))) <= kColourTolerance;
}

} // namespace

TEST_CASE("Preset variables: per-frame and init code read an outside value, and it survives frames (8.7)",
          "[engine][headless][presetvars]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  // The preset clobbers mdw_m1 at the end of every frame, so a value that
  // only lived until the preset's own code changed it would read 0 from
  // the second frame on.
  const auto preset = borderPreset("init_seen = mdw_m1;", "ob_r = mdw_m1;\nob_g = init_seen;\nmdw_m1 = 0;", "");

  // Set before the preset loads: the init code sees it too.
  rig.instance->setPresetVariable("mdw_m1", 0.75);
  rig.instance->loadPresetData(preset.c_str(), false);

  std::vector<std::uint8_t> pixels;
  int n = 0;
  for (; n < 3; ++n) {
    pixels = rig.renderFrame(n);
  }
  auto px = borderPixel(pixels);
  INFO("after 3 frames: r " << px.r << " g " << px.g << " b " << px.b);
  CHECK(matchesColour(px.r, 0.75));
  CHECK(matchesColour(px.g, 0.75));
  CHECK(matchesColour(px.b, 0.0));

  for (; n < 60; ++n) {
    pixels = rig.renderFrame(n);
  }
  px = borderPixel(pixels);
  INFO("after 60 frames: r " << px.r << " g " << px.g);
  CHECK(matchesColour(px.r, 0.75)); // still there, set once, 57 frames ago

  // A new value takes effect on the next frame; init code ran once, so
  // init_seen keeps the value from load time. Names are case-insensitive.
  rig.instance->setPresetVariable("MDW_M1", 0.25);
  pixels = rig.renderFrame(n++);
  px = borderPixel(pixels);
  INFO("after the change: r " << px.r << " g " << px.g);
  CHECK(matchesColour(px.r, 0.25));
  CHECK(matchesColour(px.g, 0.75));
}

TEST_CASE("Preset variables: per-vertex code reads an outside value (8.7)", "[engine][headless][presetvars]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  // Per-vertex code copies the variable into reg01 (global registers are
  // shared by all of a preset's code), and per-frame code shows reg01 on
  // the next frame. Per-frame code never names mdw_m2, so the value can only
  // have reached the per-vertex context directly.
  //
  // reg02 is the control: a plain per-frame variable is NOT visible to
  // per-vertex code (MilkDrop copies only q1..q32 across), which is why the
  // patch writes into both contexts instead of relying on "the usual copy".
  const auto preset =
      borderPreset("", "plain_var = 0.5;\nob_r = reg01;\nob_g = reg02;", "reg01 = mdw_m2;\nreg02 = plain_var;");
  rig.instance->setPresetVariable("mdw_m2", 0.6);
  rig.instance->loadPresetData(preset.c_str(), false);

  std::vector<std::uint8_t> pixels;
  for (int n = 0; n < 5; ++n) {
    pixels = rig.renderFrame(n);
  }
  const auto px = borderPixel(pixels);
  INFO("r " << px.r << " g " << px.g);
  CHECK(matchesColour(px.r, 0.6));
  CHECK(matchesColour(px.g, 0.0));
}

TEST_CASE("Preset variables: both presets receive a value set during a soft cut (8.7)",
          "[engine][headless][presetvars]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  // A shows the variable in red only, B in green only, so each channel of
  // the blended output belongs to one preset.
  const auto presetA = borderPreset("", "ob_r = mdw_m3;", "");
  const auto presetB = borderPreset("", "ob_g = mdw_m3;", "");
  constexpr double softCutSeconds = 2.0;
  rig.instance->setSoftCutDuration(softCutSeconds);

  rig.instance->setPresetVariable("mdw_m3", 0.0);
  rig.instance->loadPresetData(presetA.c_str(), false);
  int n = 0;
  for (; n < 10; ++n) {
    rig.renderFrame(n);
  }
  rig.instance->loadPresetData(presetB.c_str(), /*smoothTransition=*/true);

  // A quarter into the blend, both presets still read 0.
  std::vector<std::uint8_t> pixels;
  const int framesPerBlend = static_cast<int>(softCutSeconds * kFps);
  const int quarter = n + framesPerBlend / 4;
  for (; n < quarter; ++n) {
    pixels = rig.renderFrame(n);
  }
  const double redBefore = meanChannel(pixels, 0);
  const double greenBefore = meanChannel(pixels, 1);
  const double blueBefore = meanChannel(pixels, 2);

  // Set mid-transition, once. Both presets were loaded with 0, so each can
  // only show 1 if this call reached it. projectM picks a random transition,
  // and some show one preset far more than the other at any given moment, so
  // A (outgoing) is checked early, where it still dominates, and B
  // (incoming) late, where it does.
  rig.instance->setPresetVariable("mdw_m3", 1.0);
  pixels = rig.renderFrame(n++);
  const double redEarly = meanChannel(pixels, 0);
  const double blueEarly = meanChannel(pixels, 2);

  const int threeQuarters = quarter + framesPerBlend / 2;
  for (; n < threeQuarters; ++n) {
    pixels = rig.renderFrame(n);
  }
  const double greenLate = meanChannel(pixels, 1);
  const double blueLate = meanChannel(pixels, 2);

  INFO("before: r " << redBefore << " g " << greenBefore << " b " << blueBefore << "; 25%: r " << redEarly
                    << " b " << blueEarly << "; 75%: g " << greenLate << " b " << blueLate);
  // Before: dark, and the same in every channel (whatever faint level the
  // blend itself adds), so neither preset's border colour is showing yet.
  CHECK(redBefore < 8.0);
  CHECK(std::abs(redBefore - blueBefore) < 0.5);
  CHECK(std::abs(greenBefore - blueBefore) < 0.5);
  // After: each preset's channel stands out from blue, which neither draws.
  CHECK(redEarly > blueEarly + 10.0); // preset A (outgoing) got it
  CHECK(greenLate > blueLate + 10.0); // preset B (incoming) got it
}

TEST_CASE("Preset variables: a value set earlier reaches a preset loaded later (8.7)",
          "[engine][headless][presetvars]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto first = borderPreset("", "ob_b = 1;", "");
  const auto second = borderPreset("", "ob_r = mdw_m4;", "");
  rig.instance->loadPresetData(first.c_str(), false);
  rig.instance->setPresetVariable("mdw_m4", 0.5); // only the first preset exists now
  int n = 0;
  for (; n < 3; ++n) {
    rig.renderFrame(n);
  }
  rig.instance->loadPresetData(second.c_str(), false);
  std::vector<std::uint8_t> pixels;
  for (; n < 6; ++n) {
    pixels = rig.renderFrame(n);
  }
  const auto px = borderPixel(pixels);
  INFO("r " << px.r << " b " << px.b);
  CHECK(matchesColour(px.r, 0.5));
  CHECK(matchesColour(px.b, 0.0));
}

// ---- external textures through projectM's texture-load callback (8.12) -------------

namespace {

// A preset whose composite shader shows `sampler` and nothing else.
std::string samplerPreset(const std::string& sampler) {
  return "[preset00]\n"
         "MILKDROP_PRESET_VERSION=201\n"
         "PSVERSION=2\n"
         "PSVERSION_WARP=2\n"
         "PSVERSION_COMP=2\n"
         "fWaveAlpha=0\n"
         "comp_1=`shader_body\n"
         "comp_2=`{\n"
         "comp_3=`ret = tex2D(" +
         sampler +
         ", uv).xyz;\n"
         "comp_4=`}\n";
}

// What the callback was asked, and the texture it hands over for "camera".
struct TextureRequests {
  std::uint32_t camera = 0;
  std::vector<std::string> names;
};

void onTextureLoad(const char* name, ProjectMTextureLoadData* data, void* userData) {
  auto& requests = *static_cast<TextureRequests*>(userData);
  requests.names.emplace_back(name);
  if (juce::String(name).equalsIgnoreCase("camera")) {
    data->textureId = requests.camera;
    data->width = 2;
    data->height = 2;
    data->channels = 4;
  } else if (juce::String(name).startsWithIgnoreCase("extra")) {
    static constexpr std::array<unsigned char, 16> grey{128, 128, 128, 255, 128, 128, 128, 255,
                                                        128, 128, 128, 255, 128, 128, 128, 255};
    data->data = grey.data(); // projectM copies it into a texture of its own
    data->width = 2;
    data->height = 2;
    data->channels = 4;
  }
}

// A 2x2 texture: `bottom` in row 0, `top` in row 1 (GL's order).
void fillTexture(std::uint32_t texture, Rgb bottom, Rgb top) {
  using namespace juce::gl;
  const auto c = [](int v) { return static_cast<std::uint8_t>(v); };
  const std::array<std::uint8_t, 16> rgba{c(bottom.r), c(bottom.g), c(bottom.b), 255, c(bottom.r), c(bottom.g),
                                          c(bottom.b), 255,        c(top.r),    c(top.g),    c(top.b),    255,
                                          c(top.r),    c(top.g),    c(top.b),    255};
  glBindTexture(GL_TEXTURE_2D, texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

Rgb pixelAt(const std::vector<std::uint8_t>& rgba, int x, int y) { // y from the bottom
  const std::size_t i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
  return {rgba[i], rgba[i + 1], rgba[i + 2]};
}

} // namespace

TEST_CASE("External textures: a preset samples a texture we own, live, and projectM never deletes it (8.12)",
          "[engine][headless][externaltex]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  using namespace juce::gl;
  TextureRequests requests;
  glGenTextures(1, &requests.camera);
  fillTexture(requests.camera, {0, 0, 255}, {255, 0, 0}); // blue at the bottom, red at the top
  rig.instance->setTextureLoadCallback(&onTextureLoad, &requests);

  const auto camera = samplerPreset("sampler_camera");
  rig.instance->loadPresetData(camera.c_str(), false);
  CHECK(std::find(requests.names.begin(), requests.names.end(), "camera") != requests.names.end());
  std::vector<std::uint8_t> pixels;
  int n = 0;
  for (; n < 3; ++n) {
    pixels = rig.renderFrame(n);
  }
  // projectM reads row 0 as the *top* (stb_image's order, like its own file
  // textures), not GL's bottom: a picture in GL order shows upside down, so
  // the engine stamps media into this texture flipped. (Read at the texel
  // centres, a quarter of the way in, where filtering mixes nothing.)
  const auto top = pixelAt(pixels, kWidth / 2, kHeight * 3 / 4);
  const auto bottom = pixelAt(pixels, kWidth / 2, kHeight / 4);
  INFO("top " << top.r << "," << top.g << "," << top.b << " bottom " << bottom.r << "," << bottom.g << ","
              << bottom.b);
  CHECK(top.b > 200); // GL row 0
  CHECK(top.r < 50);
  CHECK(bottom.r > 200); // GL row 1
  CHECK(bottom.b < 50);

  // Live: new contents show on the next frame, with no reload and no new request.
  const auto asked = requests.names.size();
  fillTexture(requests.camera, {0, 255, 0}, {0, 255, 0});
  pixels = rig.renderFrame(n++);
  const auto green = pixelAt(pixels, kWidth / 2, kHeight / 2);
  INFO("after the update " << green.r << "," << green.g << "," << green.b);
  CHECK(green.g > 200);
  CHECK(green.r < 50);
  CHECK(requests.names.size() == asked);

  // Never deleted: not when presets that don't use it age it out of projectM's
  // cache, and not when the instance goes. projectM purges at most one texture
  // per load, the oldest-and-biggest, once at least two of different ages are
  // two or more loads old (with one, its weighting is 0/0 and nothing goes),
  // so each preset in between names a texture of its own.
  for (int load = 0; load < 6; ++load) {
    const auto other = samplerPreset("sampler_extra" + std::to_string(load));
    rig.instance->loadPresetData(other.c_str(), false);
    rig.renderFrame(n++);
  }
  CHECK(glIsTexture(requests.camera) == GL_TRUE);
  // After a purge, a preset naming it asks again, and gets the same texture.
  rig.instance->loadPresetData(camera.c_str(), false);
  for (int i = 0; i < 2; ++i) {
    pixels = rig.renderFrame(n++);
  }
  CHECK(std::count(requests.names.begin(), requests.names.end(), "camera") == 2);
  CHECK(pixelAt(pixels, kWidth / 2, kHeight / 2).g > 200);
  rig.instance.reset();
  CHECK(glIsTexture(requests.camera) == GL_TRUE);
  glDeleteTextures(1, &requests.camera);
}

namespace {

// Hands one texture over for both of the engine's names, as RenderEngine does.
void onMediaTextureLoad(const char* name, ProjectMTextureLoadData* data, void* userData) {
  const juce::String texture(name);
  if (texture.equalsIgnoreCase(milkdawp::core::kCameraTexture) ||
      texture.equalsIgnoreCase(milkdawp::core::kVideoTexture)) {
    data->textureId = *static_cast<std::uint32_t*>(userData);
    data->width = 64;
    data->height = 36;
    data->channels = 4;
  }
}

void collectProjectMErrors(const char* message, int, void* userData) {
  static_cast<std::vector<std::string>*>(userData)->emplace_back(message != nullptr ? message : "");
}

} // namespace

TEST_CASE("The hand-made 8.12 presets compile, and draw from the media and the beat",
          "[engine][headless][externaltex][originals]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  using namespace juce::gl;
  // A checkerboard "camera" picture that scrolls sideways a texel a frame, so edges,
  // media and movement all show; or black.
  std::uint32_t media = 0;
  glGenTextures(1, &media);
  const auto fillMedia = [&](bool checker, int scroll) {
    std::vector<std::uint8_t> rgba(64U * 36U * 4U, 0);
    for (int y = 0; y < 36 && checker; ++y) {
      for (int x = 0; x < 64; ++x) {
        auto* p = &rgba[static_cast<std::size_t>(y * 64 + x) * 4];
        const bool on = (((x + scroll) / 8) + (y / 8)) % 2 == 0;
        p[0] = on ? 230 : 20;
        p[1] = on ? 180 : 40;
        p[2] = on ? 90 : 120;
      }
    }
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
      rgba[i] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, media);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 36, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  };
  fillMedia(true, 0);
  rig.instance->setTextureLoadCallback(&onMediaTextureLoad, &media);
  std::vector<std::string> errors;
  const auto& fn = rig.library->functions();
  fn.setLogCallback(&collectProjectMErrors, /*currentThreadOnly=*/true, &errors);
  // Warn, not Error: a composite shader that fails to compile is only a
  // warning (projectM falls back to its default one), as is a missing texture.
  fn.setLogLevel(static_cast<int>(ProjectMLogLevel::Warn), true);
  // ...and it is heard: a composite shader that can't compile.
  const auto broken = samplerPreset("sampler_main) + nonsense(");
  rig.instance->loadPresetData(broken.c_str(), false);
  CHECK_FALSE(errors.empty());

  const auto folder = juce::File(MILKDAWP_ORIGINAL_PRESETS_DIR);
  const auto files = folder.findChildFiles(juce::File::findFiles, false, "*.milkdawp");
  REQUIRE(files.size() >= 13);
  int n = 0;
  for (const auto& file : files) {
    INFO(file.getFileName());
    const auto text = file.loadFileAsString().toStdString();
    const auto parsed = milkdawp::core::parseMilkdawp(text);
    for (const auto& problem : parsed.problems) {
      INFO(problem);
    }
    CHECK(parsed.problems.empty());
    CHECK(parsed.preset.controls.size() >= 2);
    const auto compiled = milkdawp::core::compileForProjectM(parsed.preset);
    const auto defaults = milkdawp::core::macroDefaults(parsed.preset);
    // Loads the preset fresh and plays 1.5 s at 120 bpm with an onset on every
    // beat, Macros on their defaults; the frames half way and at the end.
    const auto play = [&](bool withMedia, std::vector<std::uint8_t>& early, std::vector<std::uint8_t>& late) {
      errors.clear();
      rig.instance->loadPresetData(compiled.c_str(), false);
      for (int frame = 0; frame < 90; ++frame, ++n) {
        fillMedia(withMedia, frame);
        const double beats = frame / kFps * 2.0;
        const double phase = beats - std::floor(beats);
        rig.instance->setPresetVariable(milkdawp::core::kBpmVariable, 120.0);
        rig.instance->setPresetVariable(milkdawp::core::kBeatPhaseVariable, phase);
        rig.instance->setPresetVariable(milkdawp::core::kBarPhaseVariable, std::fmod(beats, 4.0) / 4.0);
        rig.instance->setPresetVariable(milkdawp::core::kOnsetVariable, std::exp(-phase * 0.5 / 0.1));
        for (std::size_t k = 0; k < milkdawp::core::kMacroCount; ++k) {
          rig.instance->setPresetVariable(milkdawp::core::kMacroVariables[k], defaults[k].value_or(0.0f));
        }
        rig.instance->setPresetVariable(milkdawp::core::kWarpVariable, 1.0);
        rig.instance->setPresetVariable(milkdawp::core::kDtVariable, 1.0 / kFps);
        auto pixels = rig.renderFrame(n);
        if (frame == 45) {
          early = std::move(pixels);
        } else if (frame == 89) {
          late = std::move(pixels);
        }
      }
      for (const auto& error : errors) {
        INFO("projectM: " << error);
      }
      CHECK(errors.empty());
    };
    std::vector<std::uint8_t> early;
    std::vector<std::uint8_t> late;
    play(true, early, late);
    INFO("brightness " << meanBrightness(late) << ", change " << meanDifference(early, late));
    CHECK(meanBrightness(late) > 15.0);
    CHECK(meanDifference(early, late) > 1.0);
    // A preset that names the media shows it: black media changes the picture.
    if (text.find("_camera") != std::string::npos || text.find("_video") != std::string::npos) { // any prefix
      std::vector<std::uint8_t> blackEarly;
      std::vector<std::uint8_t> blackLate;
      play(false, blackEarly, blackLate);
      INFO("with black media: change " << meanDifference(late, blackLate));
      CHECK(meanDifference(late, blackLate) > 10.0);
    }
  }
  fn.setLogCallback(nullptr, true, nullptr);
  rig.instance.reset();
  glDeleteTextures(1, &media);
}

// ---- .milkdawp on projectM (8.8-8.10) ----------------------------------------------

namespace {

using MacroValues = std::array<float, milkdawp::core::kMacroCount>;

// What the render engine sets in every preset each frame (RenderEngine.cpp),
// with the Macros on `macros` and the Visual globals at the given values.
void setPresetInputs(ProjectMInstance& instance, const MacroValues& macros, float zoom = 0.0f, float rotation = 0.0f,
                     float warp = 1.0f, float trails = 0.0f) {
  for (std::size_t k = 0; k < macros.size(); ++k) {
    instance.setPresetVariable(milkdawp::core::kMacroVariables[k], macros[k]);
  }
  instance.setPresetVariable(milkdawp::core::kZoomVariable, zoom);
  instance.setPresetVariable(milkdawp::core::kRotationVariable, rotation);
  instance.setPresetVariable(milkdawp::core::kWarpVariable, warp);
  instance.setPresetVariable(milkdawp::core::kTrailsVariable, trails);
  instance.setPresetVariable(milkdawp::core::kDtVariable, 1.0 / kFps);
}

MacroValues defaultMacros(const milkdawp::core::MilkdawpPreset& preset) {
  MacroValues macros{};
  const auto defaults = milkdawp::core::macroDefaults(preset);
  for (std::size_t k = 0; k < macros.size(); ++k) {
    macros[k] = defaults[k].value_or(0.0f);
  }
  return macros;
}

// mdw-border.milk as a .milkdawp: lines before [preset00], odd case, a space
// for '=', code full of '=' and ';', and a control in every mode.
std::string borderAsMilkdawp() {
  auto text = readPreset("mdw-border.milk");
  text.insert(0, "MDW_FORMAT=1\nmdw_title Border\n");
  text += "mdw_ctl_1_name=Swirl\nmdw_ctl_1_slot=macro1\nmdw_ctl_1_mode=rate\nmdw_ctl_1_target=rot\n"
          "mdw_ctl_2_slot=macro2\nmdw_ctl_2_mode=scale\nmdw_ctl_2_target=zoom\nmdw_ctl_2_min=0.5\nmdw_ctl_2_max=1.7\n"
          "mdw_ctl_3_slot=macro3\nmdw_ctl_3_mode=replace\nmdw_ctl_3_target=ob_r\nmdw_ctl_3_value=1\n"
          "mdw_ctl_4_slot=macro4\nmdw_ctl_4_mode=offset\nmdw_ctl_4_target=dx\nmdw_ctl_4_stage=pixel\n"
          "mdw_ctl_5_slot=macro5\nmdw_ctl_5_mode=expr\nmdw_ctl_5_code=ob_g = ob_g + mdw_c5; q1 = q1 == 0;\n"
          "mdw_fx_1=glow,amount:0.35\n";
  return text;
}

} // namespace

TEST_CASE(".milkdawp: projectM opens one as its plain preset, and compiled with neutral controls it "
          "looks the same (8.8, 8.9)",
          "[engine][headless][milkdawp]") {
  // The border colour is a pure function of time (see the set_frame_time
  // test), so the same frames must show the same colour all three ways.
  const auto plain = readPreset("mdw-border.milk");
  const auto raw = borderAsMilkdawp();
  const auto parsed = milkdawp::core::parseMilkdawp(raw);
  REQUIRE(parsed.problems.size() == 1); // only mdw_fx_1: not used yet
  REQUIRE(parsed.preset.controls.size() == 5);
  const auto compiled = milkdawp::core::compileForProjectM(parsed.preset);

  const std::vector<int> frames{10, 40, 70};
  const auto borderColours = [&frames](const std::string& text, const milkdawp::core::MilkdawpPreset* inputs,
                                       std::vector<Rgb>& out) {
    HeadlessRig rig;
    if (!rig.ready()) {
      return rig.skipReason;
    }
    if (inputs != nullptr) {
      setPresetInputs(*rig.instance, defaultMacros(*inputs));
    }
    rig.instance->loadPresetData(text.c_str(), false);
    int next = 0;
    for (const int wanted : frames) {
      std::vector<std::uint8_t> pixels;
      while (next <= wanted) {
        pixels = rig.renderFrame(next++);
      }
      out.push_back(borderPixel(pixels));
    }
    return std::string{};
  };
  std::vector<Rgb> expected;
  std::vector<Rgb> asRenamed;
  std::vector<Rgb> asCompiled;
  if (const auto skip = borderColours(plain, nullptr, expected); !skip.empty()) {
    reportUnavailable(skip);
    return;
  }
  // 8.8: projectM reading the file itself (our patch leaves its parser alone)
  // ignores every mdw_ key, so it plays the plain preset.
  borderColours(raw, nullptr, asRenamed);
  // 8.9: what we load: every Macro on its default, the globals neutral.
  borderColours(compiled, &parsed.preset, asCompiled);
  REQUIRE(asRenamed.size() == frames.size());
  REQUIRE(asCompiled.size() == frames.size());
  const auto close = [](const Rgb& a, const Rgb& b) {
    return std::abs(a.r - b.r) <= 2 && std::abs(a.g - b.g) <= 2 && std::abs(a.b - b.b) <= 2;
  };
  for (std::size_t k = 0; k < frames.size(); ++k) {
    INFO("frame " << frames[k] << ": plain " << expected[k].r << "," << expected[k].g << "," << expected[k].b
                  << " renamed " << asRenamed[k].r << "," << asRenamed[k].g << "," << asRenamed[k].b << " compiled "
                  << asCompiled[k].r << "," << asCompiled[k].g << "," << asCompiled[k].b);
    CHECK(close(expected[k], asRenamed[k]));
    CHECK(close(expected[k], asCompiled[k]));
  }
}

TEST_CASE(".milkdawp: Macros drive the preset through its controls (8.10)", "[engine][headless][milkdawp]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  const auto text = borderPreset("", "ob_r = 0.2;\nob_g = 0.5;\nob_b = 0;", "") +
                    "mdw_format=1\n"
                    "mdw_ctl_1_slot=macro1\nmdw_ctl_1_mode=replace\nmdw_ctl_1_target=ob_r\nmdw_ctl_1_value=1\n"
                    "mdw_ctl_2_slot=macro2\nmdw_ctl_2_mode=offset\nmdw_ctl_2_target=ob_g\n"
                    "mdw_ctl_2_min=-0.5\nmdw_ctl_2_max=0.5\n"
                    "mdw_ctl_3_slot=macro3\nmdw_ctl_3_mode=expr\nmdw_ctl_3_code=ob_b = mdw_c3\n"
                    "mdw_ctl_3_min=0\nmdw_ctl_3_max=1\nmdw_ctl_3_default=0.25\n";
  const auto parsed = milkdawp::core::parseMilkdawp(text);
  REQUIRE(parsed.problems.empty());
  const auto compiled = milkdawp::core::compileForProjectM(parsed.preset);

  // On their defaults the preset is as written (Macro 3's default is 0.25).
  auto macros = defaultMacros(parsed.preset);
  CHECK(macros[1] == 0.5f);
  CHECK(macros[2] == 0.25f);
  setPresetInputs(*rig.instance, macros);
  rig.instance->loadPresetData(compiled.c_str(), false);
  int n = 0;
  std::vector<std::uint8_t> pixels;
  for (; n < 3; ++n) {
    pixels = rig.renderFrame(n);
  }
  auto px = borderPixel(pixels);
  INFO("defaults: " << px.r << "," << px.g << "," << px.b);
  CHECK(matchesColour(px.r, 0.2));
  CHECK(matchesColour(px.g, 0.5));
  CHECK(matchesColour(px.b, 0.25));

  // Half way to 1; +0.3; and the expression's own value.
  macros[0] = 0.5f;
  macros[1] = 0.8f;
  macros[2] = 0.75f;
  setPresetInputs(*rig.instance, macros);
  pixels = rig.renderFrame(n++);
  px = borderPixel(pixels);
  INFO("moved: " << px.r << "," << px.g << "," << px.b);
  CHECK(matchesColour(px.r, 0.6));
  CHECK(matchesColour(px.g, 0.8));
  CHECK(matchesColour(px.b, 0.75));
}

TEST_CASE(".milkdawp: Zoom, Rotation and Warp reach the preset's own zoom, rot and warp (8.10)",
          "[engine][headless][milkdawp]") {
  HeadlessRig rig;
  if (!rig.ready()) {
    reportUnavailable(rig.skipReason);
    return;
  }
  // Per-vertex code runs after all per-frame code (ours included) and starts
  // from its results. It copies them into registers, and per-frame code shows
  // them on the next frame: red = zoom, green = rot, blue = warp.
  auto text = borderPreset("", "ob_r = (reg01 - 1) * 5 + 0.5;\nob_g = reg02 * 5 + 0.5;\nob_b = reg03;",
                           "reg01 = zoom;\nreg02 = rot;\nreg03 = warp;");
  text.replace(text.find("warp=0\n"), 7, "warp=0.5\n");
  const auto compiled = milkdawp::core::compileForProjectM(milkdawp::core::parseMilkdawp(text).preset);
  const MacroValues macros{};

  setPresetInputs(*rig.instance, macros); // neutral
  rig.instance->loadPresetData(compiled.c_str(), false);
  int n = 0;
  std::vector<std::uint8_t> pixels;
  for (; n < 3; ++n) {
    pixels = rig.renderFrame(n);
  }
  auto px = borderPixel(pixels);
  INFO("neutral: " << px.r << "," << px.g << "," << px.b);
  CHECK(matchesColour(px.r, 0.5));
  CHECK(matchesColour(px.g, 0.5));
  CHECK(matchesColour(px.b, 0.5));

  setPresetInputs(*rig.instance, macros, 1.0f, 1.0f, 2.0f);
  for (const int last = n + 2; n < last; ++n) {
    pixels = rig.renderFrame(n);
  }
  px = borderPixel(pixels);
  INFO("turned: " << px.r << "," << px.g << "," << px.b);
  CHECK(matchesColour(px.r, 0.7)); // zoom x 1.04
  CHECK(matchesColour(px.g, 0.7)); // rot + 0.04
  CHECK(matchesColour(px.b, 1.0)); // warp 0.5 x 2
}
