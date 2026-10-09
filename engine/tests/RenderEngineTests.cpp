// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/engine/MediaSource.h"
#include "milkdawp/engine/RenderEngine.h"

using namespace milkdawp::engine;
using namespace std::chrono_literals;

namespace {
// Waits until the render thread has either come up or reported why not.
void waitForStartup(const RenderEngine& engine) {
  for (int i = 0; i < 400 && !engine.isAvailable() && engine.unavailableReason().empty(); ++i) {
    std::this_thread::sleep_for(10ms);
  }
}

// Polls `predicate` for up to four seconds.
template <typename Predicate> bool waitUntil(Predicate predicate) {
  for (int i = 0; i < 400 && !predicate(); ++i) {
    std::this_thread::sleep_for(10ms);
  }
  return predicate();
}

// Stats are published once per rendered frame, so they trail a layer change by a frame.
bool layerCountBecomes(const RenderEngine& engine, int count) {
  return waitUntil([&] { return engine.stats().layers == count; });
}
} // namespace

// These run whether or not projectM and a GL driver are present. Without
// them the engine must stay a valid, inert object with a reason; with them
// (a dev box with the overlay port installed) the render thread really runs.

TEST_CASE("RenderEngine::create never returns null and explains itself when unavailable", "[engine][RenderEngine]") {
  milkdawp::core::AudioRing ring(4096, 2);
  const auto engine = RenderEngine::create(ring);
  REQUIRE(engine != nullptr);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    CHECK_FALSE(engine->unavailableReason().empty());
  } else {
    CHECK(engine->unavailableReason().empty());
    CHECK_FALSE(engine->glDescription().empty());
    CHECK(engine->sharedContextHandle() != nullptr);
  }
}

TEST_CASE("RenderEngine surface slots are handed out and reclaimed", "[engine][RenderEngine]") {
  milkdawp::core::AudioRing ring(4096, 2);
  const auto engine = RenderEngine::create(ring);

  std::vector<int> slots;
  for (int i = 0; i < RenderEngine::kMaxSurfaces; ++i) {
    const int slot = engine->registerSurface();
    CHECK(slot >= 0);
    slots.push_back(slot);
  }
  CHECK(engine->registerSurface() == -1);
  engine->unregisterSurface(slots.front());
  CHECK(engine->registerSurface() == slots.front());
}

TEST_CASE("RenderEngine does no GPU work with no visible surface and renders once one is visible (2.10)",
          "[engine][RenderEngine]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }

  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, 320, 180, /*visible=*/false);
  std::this_thread::sleep_for(200ms);
  CHECK(engine->stats().paused);
  CHECK_FALSE(engine->latestFrame().has_value());

  engine->reportSurfaceSize(slot, 320, 180, /*visible=*/true);
  std::optional<RenderEngine::Frame> frame;
  for (int i = 0; i < 300 && !frame; ++i) {
    std::this_thread::sleep_for(10ms);
    frame = engine->latestFrame();
  }
  REQUIRE(frame.has_value());
  CHECK(frame->width == 320);
  CHECK(frame->height == 180);
  CHECK(frame->texture != 0);

  // Hidden again: frames stop advancing, and nothing is torn down.
  engine->reportSurfaceSize(slot, 320, 180, /*visible=*/false);
  std::this_thread::sleep_for(100ms);
  const auto pausedAt = engine->latestFrame()->number;
  std::this_thread::sleep_for(200ms);
  CHECK(engine->latestFrame()->number == pausedAt);
  CHECK(engine->stats().paused);
  CHECK(engine->isAvailable());

  // And visible once more: rendering resumes on the same context.
  engine->reportSurfaceSize(slot, 320, 180, /*visible=*/true);
  std::this_thread::sleep_for(200ms);
  CHECK(engine->latestFrame()->number > pausedAt);
  engine->unregisterSurface(slot);
}

TEST_CASE("RenderEngine sizes its frames to the largest visible surface times the quality scale",
          "[engine][RenderEngine]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }

  const int small = engine->registerSurface();
  const int large = engine->registerSurface();
  engine->reportSurfaceSize(small, 200, 100, true);
  engine->reportSurfaceSize(large, 640, 360, true);
  engine->setQualityScale(0.5f);

  bool sized = false;
  for (int i = 0; i < 300 && !sized; ++i) {
    std::this_thread::sleep_for(10ms);
    const auto frame = engine->latestFrame();
    sized = frame && frame->width == 320 && frame->height == 180;
  }
  CHECK(sized);
  engine->unregisterSurface(small);
  engine->unregisterSurface(large);
}

TEST_CASE("RenderEngine loads a handed-over preset through its primary layer", "[engine][RenderEngine][layers]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }
  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, 320, 180, /*visible=*/true);

  const auto fixture = juce::File(MILKDAWP_FIXTURES_DIR).getChildFile("presets").getChildFile("mdw-wave.milk");
  REQUIRE(fixture.existsAsFile());
  const auto text = fixture.loadFileAsString().toStdString();

  // The same calls a Director makes: text through the handoff, then a request.
  constexpr std::uint32_t goodId = 7;
  REQUIRE(engine->presetHandoff().offer(goodId, text));
  REQUIRE(engine->pushTransition({goodId, milkdawp::core::CutStyle::Hard, 0.0f, 0}));
  for (int i = 0; i < 300 && engine->stats().presetsLoaded == 0; ++i) {
    std::this_thread::sleep_for(10ms);
  }
  CHECK(engine->stats().presetsLoaded == 1);
  CHECK(engine->stats().presetsFailed == 0);
  CHECK(engine->stats().currentPresetId == goodId);
  CHECK_FALSE(engine->presetHandoff().popFailure().has_value());
  // (The rejected-preset path is not covered here: projectM 4.2 accepts arbitrary
  // text in loadPresetData, so there is no reliably "bad" input to hand it.)
  engine->unregisterSurface(slot);
}

TEST_CASE("RenderEngine adds, composes and removes layers while it keeps rendering", "[engine][RenderEngine][layers]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }
  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, 320, 180, /*visible=*/true);
  engine->addReadbackClient();

  const auto waitFor = [](auto predicate) { return waitUntil(predicate); };
  // Frames keep arriving, and every pixel of the canvas is opaque.
  const auto framesAdvance = [&] {
    const auto before = engine->latestFrame();
    REQUIRE(before.has_value());
    return waitFor([&] {
      const auto now = engine->latestFrame();
      return now && now->number > before->number + 2;
    });
  };
  const auto canvasIsOpaque = [&] {
    waitFor([&] { return static_cast<bool>(engine->latestReadbackFrame()); });
    const auto frame = engine->latestReadbackFrame();
    REQUIRE(frame);
    for (std::size_t i = 3; i < frame->rgba.size(); i += 4 * 97) {
      if (frame->rgba[i] != 255) {
        return false;
      }
    }
    return true;
  };

  REQUIRE(waitFor([&] { return engine->latestFrame().has_value(); }));
  CHECK(engine->stats().layers == 1);

  const auto fixture = [](const char* name) {
    return juce::File(MILKDAWP_FIXTURES_DIR).getChildFile("presets").getChildFile(name).loadFileAsString().toStdString();
  };

  milkdawp::core::AudioRing secondRing(1 << 14, 2);
  milkdawp::engine::LayerChannel second(secondRing, 48000.0);
  second.setBlend(milkdawp::engine::LayerBlend::Add);
  second.setOpacity(0.6f);
  second.setOrder(1);

  REQUIRE(engine->addLayer(second));
  CHECK(layerCountBecomes(*engine, 2));
  CHECK_FALSE(engine->addLayer(second)); // already attached
  REQUIRE(second.presetHandoff().offer(1, fixture("mdw-wave.milk")));
  REQUIRE(second.pushTransition({1, milkdawp::core::CutStyle::Hard, 0.0f, 0}));
  CHECK(framesAdvance());
  CHECK(canvasIsOpaque());

  // The layer can be hidden, shown and re-blended on the fly.
  second.setVisible(false);
  CHECK(framesAdvance());
  second.setVisible(true);
  second.setBlend(milkdawp::engine::LayerBlend::Multiply);
  CHECK(framesAdvance());
  CHECK(canvasIsOpaque());

  engine->removeLayer(second);
  CHECK(layerCountBecomes(*engine, 1));
  engine->removeLayer(second); // not attached any more: a no-op
  engine->removeLayer(engine->primaryLayer()); // the primary cannot be removed
  CHECK(layerCountBecomes(*engine, 1));
  CHECK(framesAdvance());

  // And it can come back.
  REQUIRE(engine->addLayer(second));
  CHECK(layerCountBecomes(*engine, 2));
  engine->removeLayer(second);

  engine->removeReadbackClient();
  engine->unregisterSurface(slot);
}

TEST_CASE("RenderEngine measures what each layer costs, and loads presets for several layers without starving any",
          "[engine][RenderEngine][layers]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }
  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, 320, 180, /*visible=*/true);

  const auto text = juce::File(MILKDAWP_FIXTURES_DIR)
                        .getChildFile("presets")
                        .getChildFile("mdw-feedback.milk")
                        .loadFileAsString()
                        .toStdString();

  std::vector<std::unique_ptr<milkdawp::core::AudioRing>> rings;
  std::vector<std::unique_ptr<milkdawp::engine::LayerChannel>> channels;
  for (int i = 0; i < 4; ++i) {
    rings.push_back(std::make_unique<milkdawp::core::AudioRing>(1 << 12, 2));
    channels.push_back(std::make_unique<milkdawp::engine::LayerChannel>(*rings.back(), 48000.0));
    REQUIRE(engine->addLayer(*channels.back()));
  }
  // Every layer, the primary included, has a preset due at once. Only one
  // compiles per frame, but each must get its turn.
  REQUIRE(engine->presetHandoff().offer(1, text));
  REQUIRE(engine->pushTransition({1, milkdawp::core::CutStyle::Hard, 0.0f, 0}));
  for (auto& channel : channels) {
    REQUIRE(channel->presetHandoff().offer(1, text));
    REQUIRE(channel->pushTransition({1, milkdawp::core::CutStyle::Hard, 0.0f, 0}));
  }
  CHECK(waitUntil([&] {
    return engine->stats().presetsLoaded == 1 &&
           std::all_of(channels.begin(), channels.end(), [](const auto& c) { return c->presetsLoaded() == 1; });
  }));

  // Each drawn layer reports a GPU time; a hidden one reports none.
  channels[0]->setVisible(false);
  CHECK(waitUntil([&] { return channels[0]->gpuMs() == 0.0f; }));
  CHECK(waitUntil([&] { return channels[1]->gpuMs() > 0.0f; })); // needs GPU timer queries, which this driver has
  CHECK(waitUntil([&] { return engine->stats().gpuFrameMs > 0.0f; })); // the total is the sum, not zero

  for (auto& channel : channels) {
    engine->removeLayer(*channel);
  }
  engine->unregisterSurface(slot);
}

TEST_CASE("RenderEngine caps the number of layers", "[engine][RenderEngine][layers]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }
  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, 160, 90, /*visible=*/true);

  std::vector<std::unique_ptr<milkdawp::core::AudioRing>> rings;
  std::vector<std::unique_ptr<milkdawp::engine::LayerChannel>> channels;
  for (int i = 0; i < RenderEngine::kMaxLayers; ++i) {
    rings.push_back(std::make_unique<milkdawp::core::AudioRing>(1 << 12, 2));
    channels.push_back(std::make_unique<milkdawp::engine::LayerChannel>(*rings.back(), 48000.0));
  }
  // The primary counts, so kMaxLayers - 1 more fit and the next is refused.
  for (int i = 0; i < RenderEngine::kMaxLayers - 1; ++i) {
    INFO("layer " << i);
    CHECK(engine->addLayer(*channels[static_cast<std::size_t>(i)]));
  }
  CHECK(layerCountBecomes(*engine, RenderEngine::kMaxLayers));
  CHECK_FALSE(engine->addLayer(*channels.back()));

  for (int i = 0; i < RenderEngine::kMaxLayers - 1; ++i) {
    engine->removeLayer(*channels[static_cast<std::size_t>(i)]);
  }
  CHECK(layerCountBecomes(*engine, 1));
  engine->unregisterSurface(slot);
}

TEST_CASE("RenderEngine hands its primary layer to another engine and takes it back", "[engine][RenderEngine][layers]") {
  // The Layers hand-over: a sender instance stops driving its own primary
  // layer, a hub engine adopts that layer's channel, and later it all reverses.
  milkdawp::core::AudioRing senderRing(1 << 14, 2);
  milkdawp::core::AudioRing hubRing(1 << 14, 2);
  const auto sender = RenderEngine::create(senderRing);
  const auto hub = RenderEngine::create(hubRing);
  waitForStartup(*sender);
  waitForStartup(*hub);
  if (!sender->isAvailable() || !hub->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  for (auto* engine : {sender.get(), hub.get()}) {
    const int slot = engine->registerSurface();
    engine->reportSurfaceSize(slot, 160, 90, true);
  }
  REQUIRE(waitUntil([&] { return sender->latestFrame().has_value() && hub->latestFrame().has_value(); }));

  sender->yieldPrimaryLayer(true); // returns once the render thread has let go of the channel
  CHECK(sender->isPrimaryLayerYielded());
  CHECK(sender->stats().paused);
  const auto frozenAt = sender->latestFrame()->number;

  REQUIRE(hub->addLayer(sender->primaryLayer()));
  CHECK(layerCountBecomes(*hub, 2));
  std::this_thread::sleep_for(150ms);
  CHECK(sender->latestFrame()->number == frozenAt); // the sender itself renders nothing meanwhile
  const auto hubFrame = hub->latestFrame()->number;
  CHECK(waitUntil([&] { return hub->latestFrame()->number > hubFrame + 2; }));

  hub->removeLayer(sender->primaryLayer());
  CHECK(layerCountBecomes(*hub, 1));
  sender->yieldPrimaryLayer(false);
  CHECK_FALSE(sender->isPrimaryLayerYielded());
  CHECK(waitUntil([&] { return sender->latestFrame()->number > frozenAt + 2; })); // it renders again
}

TEST_CASE("RenderEngine refuses layer changes when it is not running", "[engine][RenderEngine][layers]") {
  milkdawp::core::AudioRing ring(1 << 12, 2);
  milkdawp::engine::LayerChannel channel(ring, 48000.0);
  std::unique_ptr<RenderEngine> engine;
  {
    engine = RenderEngine::create(ring);
    waitForStartup(*engine);
  }
  if (engine->isAvailable()) {
    SUCCEED("this engine is running; the not-running case needs an unavailable one");
    return;
  }
  CHECK_FALSE(engine->addLayer(channel)); // returns at once, no hang
  engine->removeLayer(channel);
}

TEST_CASE("RenderEngine publishes CPU copies of its frames while a readback client is registered (ADR-0009)",
          "[engine][RenderEngine][readback]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }

  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, 320, 180, /*visible=*/true);
  std::this_thread::sleep_for(200ms);
  CHECK_FALSE(engine->latestReadbackFrame()); // no client, no readback work

  engine->addReadbackClient();
  std::uint64_t firstNumber = 0;
  for (int i = 0; i < 300 && firstNumber == 0; ++i) {
    std::this_thread::sleep_for(10ms);
    if (const auto frame = engine->latestReadbackFrame()) {
      CHECK(frame->width == 320);
      CHECK(frame->height == 180);
      CHECK(frame->rgba.size() == 320U * 180U * 4U);
      firstNumber = frame->number;
    }
  }
  REQUIRE(firstNumber != 0);
  std::this_thread::sleep_for(200ms);
  REQUIRE(engine->latestReadbackFrame());
  CHECK(engine->latestReadbackFrame()->number > firstNumber); // keeps up with rendering
  // One frame of latency: the CPU copy trails the GPU frame, never leads it.
  CHECK(engine->latestReadbackFrame()->number <= engine->latestFrame()->number);

  engine->removeReadbackClient();
  std::this_thread::sleep_for(200ms);
  CHECK_FALSE(engine->latestReadbackFrame()); // stale frames are withdrawn
  engine->unregisterSurface(slot);
}

TEST_CASE("RenderEngine reports its quality scale, chosen adaptively in Auto (5.3)", "[engine][RenderEngine]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }
  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, 320, 180, true);

  // Auto (0): a tiny frame costs next to nothing, so it stays at full scale.
  engine->setQualityScale(0.0f);
  bool reported = false;
  for (int i = 0; i < 300 && !reported; ++i) {
    std::this_thread::sleep_for(10ms);
    const auto stats = engine->stats();
    reported = stats.framesRendered > 30 && stats.qualityAuto;
  }
  REQUIRE(reported);
  CHECK(engine->stats().qualityScale == 1.0f);
  CHECK(engine->stats().width == 320);

  // A fixed choice is reported as one.
  engine->setQualityScale(0.75f);
  bool fixed = false;
  for (int i = 0; i < 300 && !fixed; ++i) {
    std::this_thread::sleep_for(10ms);
    const auto stats = engine->stats();
    fixed = !stats.qualityAuto && stats.qualityScale == 0.75f && stats.width == 240;
  }
  CHECK(fixed);
  engine->unregisterSurface(slot);
}

TEST_CASE("Several engines in one process share the GPU budget; a hidden one does no work (5.6)",
          "[engine][RenderEngine]") {
  // Two plugin instances in one DAW: each its own engine, context and projectM
  // instance, over one shared projectM library.
  milkdawp::core::AudioRing ringA(1 << 14, 2);
  milkdawp::core::AudioRing ringB(1 << 14, 2);
  const auto a = RenderEngine::create(ringA);
  const auto b = RenderEngine::create(ringB);
  waitForStartup(*a);
  waitForStartup(*b);
  if (!a->isAvailable() || !b->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here");
    return;
  }
  CHECK(a->projectMVersion() == b->projectMVersion());
  a->setQualityScale(0.0f); // Auto: the one that budgets against the other
  const int slotA = a->registerSurface();
  const int slotB = b->registerSurface();
  a->reportSurfaceSize(slotA, 160, 90, true);
  b->reportSurfaceSize(slotB, 160, 90, true);

  // Both render, and each counts the other as sharing the GPU.
  CHECK(waitUntil([&] { return a->stats().gpuSharers == 2 && b->stats().gpuSharers == 2; }));
  CHECK(waitUntil([&] { return a->stats().framesRendered > 10 && b->stats().framesRendered > 10; }));
  // Two tiny frames fit easily: sharing alone doesn't cost quality.
  CHECK(a->stats().qualityScale == 1.0f);

  // B's editor closes: B stops (no frames, no GPU), A renders on, alone.
  b->reportSurfaceSize(slotB, 160, 90, false);
  CHECK(waitUntil([&] { return b->stats().paused && a->stats().gpuSharers == 1; }));
  const auto bFrozenAt = b->stats().framesRendered;
  const auto aBefore = a->stats().framesRendered;
  std::this_thread::sleep_for(200ms);
  CHECK(b->stats().framesRendered == bFrozenAt);
  CHECK(a->stats().framesRendered > aBefore);

  a->unregisterSurface(slotA);
  b->unregisterSurface(slotB);
}

namespace {

// A layer's picture as the readback copy has it: the frame after `after`.
std::vector<std::uint8_t> frameAfter(const RenderEngine& engine, std::uint64_t after, std::uint64_t& number) {
  std::vector<std::uint8_t> rgba;
  waitUntil([&] {
    const auto frame = engine.latestReadbackFrame();
    if (!frame || frame->number <= after) {
      return false;
    }
    rgba = frame->rgba;
    number = frame->number;
    return true;
  });
  return rgba;
}

struct Pixel {
  int r = 0;
  int g = 0;
  int b = 0;
};

Pixel pixelAt(const std::vector<std::uint8_t>& rgba, int width, int x, int y) { // y from the bottom
  const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
  return {rgba[i], rgba[i + 1], rgba[i + 2]};
}

bool loadPreset(RenderEngine& engine, std::uint32_t id, const std::string& text) {
  const auto before = engine.stats().presetsLoaded;
  return engine.presetHandoff().offer(id, text) &&
         engine.pushTransition({id, milkdawp::core::CutStyle::Hard, 0.0f, 0}) &&
         waitUntil([&] { return engine.stats().presetsLoaded > before; });
}

std::string compositePreset(const std::string& perFrame, const std::string& composite) {
  return "[preset00]\n"
         "MILKDROP_PRESET_VERSION=201\n"
         "PSVERSION=2\n"
         "PSVERSION_WARP=2\n"
         "PSVERSION_COMP=2\n"
         "fDecay=0.9\n"
         "fWaveAlpha=0\n"
         "ob_size=0.5\n"
         "ob_a=1\n"
         "per_frame_1=" +
         perFrame +
         "\n"
         "comp_1=`shader_body\n"
         "comp_2=`{\n"
         "comp_3=`ret = " +
         composite +
         ";\n"
         "comp_4=`}\n";
}

} // namespace

TEST_CASE("RenderEngine hands a layer's media to presets as sampler_camera and sampler_video (8.12)",
          "[engine][RenderEngine][externaltex]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }
  constexpr int kW = 320;
  constexpr int kH = 180;
  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, kW, kH, /*visible=*/true);
  engine->addReadbackClient();

  // Wider than the frame (2:1), so it is cropped at the sides: a red top half
  // and a blue bottom half, with green bands at the far left and right that a
  // crop to fill leaves out. Bottom row first, like every MediaFrame.
  MediaFrame image;
  image.width = 64;
  image.height = 32;
  image.rgba.resize(static_cast<std::size_t>(image.width * image.height) * 4);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      auto* p = &image.rgba[static_cast<std::size_t>(y * image.width + x) * 4];
      const bool band = x < 3 || x >= image.width - 3; // the crop takes 3.5 texels each side
      const bool top = y >= image.height / 2;
      p[0] = !band && top ? 255 : 0;
      p[1] = band ? 255 : 0;
      p[2] = !band && !top ? 255 : 0;
      p[3] = 255;
    }
  }
  engine->primaryLayer().setMediaSource(std::make_shared<ImageMediaSource>(std::move(image), "test"));
  // Media Mix stays 0: the preset's texture doesn't depend on it.

  for (const char* sampler : {"sampler_camera", "sampler_fc_video"}) {
    INFO(sampler);
    REQUIRE(loadPreset(*engine, sampler[8] == 'c' ? 21U : 22U,
                       compositePreset("", std::string("tex2D(") + sampler + ", uv).xyz")));
    std::uint64_t number = engine->latestFrame() ? engine->latestFrame()->number : 0;
    const auto rgba = frameAfter(*engine, number + 2, number);
    REQUIRE(rgba.size() == static_cast<std::size_t>(kW * kH * 4));
    const auto top = pixelAt(rgba, kW, kW / 2, kH * 3 / 4);
    const auto bottom = pixelAt(rgba, kW, kW / 2, kH / 4);
    const auto left = pixelAt(rgba, kW, 2, kH * 3 / 4);
    INFO("top " << top.r << "," << top.g << "," << top.b << " bottom " << bottom.r << "," << bottom.g << ","
                << bottom.b << " left " << left.r << "," << left.g << "," << left.b);
    CHECK(top.r > 200); // upright
    CHECK(top.b < 50);
    CHECK(bottom.b > 200);
    CHECK(bottom.r < 50);
    CHECK(left.g < 50); // cropped to fill, not stretched
  }

  // No media: the texture goes black.
  engine->primaryLayer().setMediaSource(nullptr);
  std::uint64_t number = engine->latestFrame()->number;
  const auto rgba = frameAfter(*engine, number + 2, number);
  const auto middle = pixelAt(rgba, kW, kW / 2, kH * 3 / 4);
  CHECK(middle.r + middle.g + middle.b < 30);

  engine->removeReadbackClient();
  engine->unregisterSurface(slot);
}

TEST_CASE("RenderEngine gives presets the layer's beat as mdw_bpm and the phases (8.12)",
          "[engine][RenderEngine][presetvars]") {
  milkdawp::core::AudioRing ring(1 << 14, 2);
  const auto engine = RenderEngine::create(ring);
  waitForStartup(*engine);
  if (!engine->isAvailable()) {
    SUCCEED("projectM or a GL context is unavailable here: " + engine->unavailableReason());
    return;
  }
  constexpr int kW = 320;
  constexpr int kH = 180;
  const int slot = engine->registerSurface();
  engine->reportSurfaceSize(slot, kW, kH, /*visible=*/true);
  engine->addReadbackClient();

  // The border (the whole frame at ob_size 0.5) shows mdw_bpm / 240 in red,
  // and in green whether both phases are in range.
  REQUIRE(loadPreset(*engine, 31,
                     compositePreset("ob_r = mdw_bpm / 240; ob_g = (mdw_beat_phase >= 0) * (mdw_beat_phase < 1) * "
                                     "(mdw_bar_phase >= 0) * (mdw_bar_phase < 1); ob_b = 0;",
                                     "tex2D(sampler_main, uv).xyz")));
  std::uint64_t number = engine->latestFrame() ? engine->latestFrame()->number : 0;
  auto rgba = frameAfter(*engine, number + 2, number);
  auto px = pixelAt(rgba, kW, 4, 4);
  INFO("no beat: " << px.r << "," << px.g);
  CHECK(px.r < 5); // no beat: 0

  milkdawp::core::BeatSnapshot beat;
  beat.bpm = 120.0f;
  beat.confidence = 1.0f;
  beat.nextBeatSample = 24000;
  engine->primaryLayer().setBeat(beat);
  rgba = frameAfter(*engine, number + 2, number);
  px = pixelAt(rgba, kW, 4, 4);
  INFO("120 bpm: " << px.r << "," << px.g);
  CHECK(std::abs(px.r - 128) <= 4);
  CHECK(px.g > 250);

  engine->removeReadbackClient();
  engine->unregisterSurface(slot);
}
