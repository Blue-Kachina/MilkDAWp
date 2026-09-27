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
