// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <functional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/AdaptiveQuality.h"

using namespace milkdawp::core;

namespace {

constexpr float kFrameSeconds = 1.0f / 60.0f;
constexpr float kBudgetMs = 1000.0f / 60.0f;

/// A GPU whose frame cost is a fixed part plus a part that scales with
/// pixel count, like projectM's (per-vertex work, then per-pixel shading).
struct Gpu {
  float fixedMs;
  float fullScalePixelMs;
  [[nodiscard]] float cost(float scale) const { return fixedMs + fullScalePixelMs * scale * scale; }
};

/// Runs `seconds` of frames; returns every scale change.
std::vector<float>
run(AdaptiveQuality& quality, float seconds, const std::function<float(float)>& costAtScale) {
  std::vector<float> changes;
  float scale = quality.scale();
  for (float t = 0.0f; t < seconds; t += kFrameSeconds) {
    const float next =
        quality.onFrame(costAtScale(scale), costAtScale(scale) + 1.0f, kFrameSeconds);
    if (next != scale) {
      changes.push_back(next);
      scale = next;
    }
  }
  return changes;
}

} // namespace

TEST_CASE("A light load stays at full resolution", "[core][AdaptiveQuality]") {
  AdaptiveQuality quality;
  const Gpu gpu{1.0f, 5.0f};
  CHECK(run(quality, 30.0f, [&](float s) { return gpu.cost(s); }).empty());
  CHECK(quality.scale() == 1.0f);
}

TEST_CASE("An overloaded GPU drops to a scale that fits, and stays there",
          "[core][AdaptiveQuality]") {
  AdaptiveQuality quality;
  const Gpu gpu{1.0f, 24.0f}; // 25 ms at full scale: 60 fps impossible
  const auto changes = run(quality, 60.0f, [&](float s) { return gpu.cost(s); });
  REQUIRE_FALSE(changes.empty());
  CHECK(changes.size() <= 2); // straight to a fitting step, no oscillation
  CHECK(gpu.cost(quality.scale()) < 0.85f * kBudgetMs);
  CHECK(quality.scale() >= 0.55f); // and not lower than it needs to be
}

TEST_CASE("Quality comes back up, one step at a time, once the load goes",
          "[core][AdaptiveQuality]") {
  AdaptiveQuality quality;
  const Gpu heavy{1.0f, 40.0f};
  run(quality, 10.0f, [&](float s) { return heavy.cost(s); });
  REQUIRE(quality.scale() < 1.0f);

  const Gpu light{1.0f, 4.0f};
  std::vector<float> scales;
  float scale = quality.scale();
  float sinceChange = 0.0f;
  float shortestGap = 1000.0f;
  for (float t = 0.0f; t < 40.0f; t += kFrameSeconds) {
    const float next = quality.onFrame(light.cost(scale), light.cost(scale) + 1.0f, kFrameSeconds);
    sinceChange += kFrameSeconds;
    if (next != scale) {
      CHECK(next > scale);
      shortestGap = std::min(shortestGap, sinceChange);
      sinceChange = 0.0f;
      scale = next;
    }
  }
  CHECK(quality.scale() == 1.0f);
  CHECK(shortestGap >= 3.0f); // upSeconds between steps
}

TEST_CASE("A one-frame spike (a preset load) changes nothing", "[core][AdaptiveQuality]") {
  AdaptiveQuality quality;
  for (int frame = 0; frame < 600; ++frame) {
    const float cost = frame == 300 ? 250.0f : 6.0f;
    quality.onFrame(cost, cost, kFrameSeconds);
  }
  CHECK(quality.scale() == 1.0f);
}

TEST_CASE("Without GPU timing the CPU frame time stands in", "[core][AdaptiveQuality]") {
  AdaptiveQuality quality;
  for (int frame = 0; frame < 120; ++frame) {
    quality.onFrame(0.0f, 30.0f, kFrameSeconds); // gpuMs 0: the query isn't supported
  }
  CHECK(quality.scale() < 1.0f);
}

TEST_CASE("A lower frame rate target leaves more room", "[core][AdaptiveQuality]") {
  AdaptiveQuality quality;
  quality.setTargetFps(30.0f); // 33 ms budget
  const Gpu gpu{1.0f, 20.0f};
  run(quality, 20.0f, [&](float s) { return gpu.cost(s); });
  CHECK(quality.scale() == 1.0f);
}

TEST_CASE("A shared GPU budget leaves each renderer what the others don't use, never under a fair share",
          "[core][AdaptiveQuality]") {
  // Alone: the whole frame, whatever `othersMs` says.
  CHECK(AdaptiveQuality::sharedBudgetMs(kBudgetMs, 0.0f, 1) == kBudgetMs);
  CHECK(AdaptiveQuality::sharedBudgetMs(kBudgetMs, 12.0f, 0) == kBudgetMs);
  // A heavy neighbour (12 ms) next to a light one (1 ms): the light one keeps
  // at least half the frame, the heavy one gets what the light one leaves.
  CHECK(AdaptiveQuality::sharedBudgetMs(kBudgetMs, 12.0f, 2) == kBudgetMs / 2.0f);
  CHECK(AdaptiveQuality::sharedBudgetMs(kBudgetMs, 1.0f, 2) == kBudgetMs - 1.0f);
  // Four instances that together overrun the frame: each falls back to a quarter.
  CHECK(AdaptiveQuality::sharedBudgetMs(kBudgetMs, 30.0f, 4) == kBudgetMs / 4.0f);
}

TEST_CASE("Two instances that together overrun the GPU: the heavy one gives way, the light one doesn't",
          "[core][AdaptiveQuality]") {
  // Each renderer budgets against what the other costs (sharedBudgetMs), the
  // way RenderEngine instances in one process do. Together 13 + 4 = 17 ms
  // overruns a 16.7 ms frame.
  const Gpu heavyGpu{1.0f, 12.0f};
  const Gpu lightGpu{1.0f, 3.0f};
  AdaptiveQuality heavy;
  AdaptiveQuality light;
  for (float t = 0.0f; t < 10.0f; t += kFrameSeconds) {
    const float heavyMs = heavyGpu.cost(heavy.scale());
    const float lightMs = lightGpu.cost(light.scale());
    heavy.setTargetFps(1000.0f / AdaptiveQuality::sharedBudgetMs(kBudgetMs, lightMs, 2));
    light.setTargetFps(1000.0f / AdaptiveQuality::sharedBudgetMs(kBudgetMs, heavyMs, 2));
    heavy.onFrame(heavyMs, heavyMs + 1.0f, kFrameSeconds);
    light.onFrame(lightMs, lightMs + 1.0f, kFrameSeconds);
  }
  CHECK(heavy.scale() < 1.0f);
  CHECK(light.scale() == 1.0f);
  CHECK(heavyGpu.cost(heavy.scale()) + lightGpu.cost(light.scale()) < 0.85f * kBudgetMs);
}

TEST_CASE("Reset returns to full scale", "[core][AdaptiveQuality]") {
  AdaptiveQuality quality;
  for (int frame = 0; frame < 120; ++frame) {
    quality.onFrame(40.0f, 41.0f, kFrameSeconds);
  }
  REQUIRE(quality.scale() < 1.0f);
  quality.reset();
  CHECK(quality.scale() == 1.0f);
  CHECK(quality.step() == 0);
}
