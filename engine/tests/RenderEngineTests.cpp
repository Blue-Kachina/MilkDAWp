// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

#include "milkdawp/core/AudioRing.h"
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
