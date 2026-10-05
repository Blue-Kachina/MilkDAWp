// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 8.1-8.3: the Visual globals' engine form, Lock Macros, and the Speed
// clock. All GL-free.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/MacroLock.h"
#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/PresetClock.h"
#include "milkdawp/core/VisualControls.h"

using namespace milkdawp::core;
using Catch::Approx;

TEST_CASE("VisualControls defaults are every control's neutral value", "[core][VisualControls]") {
  const VisualControls neutral;
  CHECK(postEffectsNeutral(neutral));
  // The parameters' defaults are the same neutral values (the engine's mapping
  // is checked against them in ControlMappingTests).
  const auto& params = allParameters();
  CHECK(findParameter(params, "visualHue")->defaultValue == neutral.hueDegrees);
  CHECK(findParameter(params, "visualSaturation")->defaultValue == neutral.saturation);
  CHECK(findParameter(params, "visualBrightness")->defaultValue == neutral.brightness);
  CHECK(findParameter(params, "visualSpeed")->defaultValue == neutral.speed);
  CHECK(findParameter(params, "visualTrails")->defaultValue == neutral.trails);
}

TEST_CASE("postEffectsNeutral notices any post effect, and ignores the rest", "[core][VisualControls]") {
  VisualControls c;
  c.speed = 2.0f;     // the preset clock, not a post effect
  c.warp = 2.0f;      // Stage B
  c.waveSize = 0.5f;  // Stage B
  c.mediaMix = 1.0f;  // 8.6
  CHECK(postEffectsNeutral(c));

  for (auto change : {+[](VisualControls& v) { v.hueDegrees = 10.0f; }, +[](VisualControls& v) { v.saturation = 0.0f; },
                      +[](VisualControls& v) { v.brightness = 1.5f; }, +[](VisualControls& v) { v.zoom = 0.1f; },
                      +[](VisualControls& v) { v.rotation = -0.2f; }, +[](VisualControls& v) { v.trails = 0.3f; },
                      +[](VisualControls& v) { v.pixelate = 0.5f; }, +[](VisualControls& v) { v.glow = 0.5f; },
                      +[](VisualControls& v) { v.blur = 0.5f; }, +[](VisualControls& v) { v.mirror = MirrorMode::Quad; },
                      +[](VisualControls& v) { v.kaleidoscopeSegments = 6; },
                      +[](VisualControls& v) { v.rgbSplit = 0.2f; }}) {
    VisualControls changed;
    change(changed);
    CHECK_FALSE(postEffectsNeutral(changed));
  }
}

TEST_CASE("smoothVisualControls glides, then lands exactly on the target", "[core][VisualControls]") {
  VisualControls current;
  VisualControls target;
  target.glow = 1.0f;
  target.mirror = MirrorMode::LeftRight;

  current = smoothVisualControls(current, target, 1.0f / 60.0f);
  CHECK(current.glow > 0.0f);
  CHECK(current.glow < 0.5f); // one frame of a 50 ms glide
  CHECK(current.mirror == MirrorMode::LeftRight); // choices switch at once

  // Back to neutral: after half a second it is neutral again, not almost.
  target = VisualControls{};
  for (int frame = 0; frame < 30; ++frame) {
    current = smoothVisualControls(current, target, 1.0f / 60.0f);
  }
  CHECK(current == VisualControls{});
  CHECK(postEffectsNeutral(current));
}

TEST_CASE("Lock Macros on keeps every Macro; off moves them to the preset's defaults", "[core][MacroLock]") {
  MacroDefaults preset{};
  preset[0] = 0.75f; // the preset uses Macro 1 and 3
  preset[2] = 0.25f;

  const auto locked = macrosAfterPresetChange(true, preset);
  for (const auto& move : locked) {
    CHECK_FALSE(move.has_value());
  }

  const auto unlocked = macrosAfterPresetChange(false, preset);
  REQUIRE(unlocked[0].has_value());
  CHECK(*unlocked[0] == 0.75f);
  REQUIRE(unlocked[2].has_value());
  CHECK(*unlocked[2] == 0.25f);
  // Unused slots go back to 0 (§6.3); a plain .milk declares none, so all of them do.
  REQUIRE(unlocked[1].has_value());
  CHECK(*unlocked[1] == 0.0f);
  for (const auto& move : macrosAfterPresetChange(false, MacroDefaults{})) {
    REQUIRE(move.has_value());
    CHECK(*move == 0.0f);
  }
}

TEST_CASE("PresetClock integrates dt x speed, so turning Speed never jumps", "[core][PresetClock]") {
  PresetClock clock;
  constexpr double kFrame = 1.0 / 60.0;
  for (int i = 0; i < 60; ++i) {
    clock.advance(kFrame, 1.0f);
  }
  CHECK(clock.time() == Approx(1.0));

  // Doubling the speed doubles the rate from now on, from where the clock is.
  const double before = clock.time();
  clock.advance(kFrame, 2.0f);
  CHECK(clock.time() == Approx(before + 2.0 * kFrame));
  CHECK(clock.time() - before < 0.05); // continuous: no jump to 2 x time

  // Speed 0 holds time still; it never runs backwards.
  const double held = clock.time();
  for (int i = 0; i < 10; ++i) {
    clock.advance(kFrame, 0.0f);
  }
  CHECK(clock.time() == held);
  clock.advance(kFrame, -3.0f);
  CHECK(clock.time() == held);
}

TEST_CASE("PresetClock caps a stalled frame", "[core][PresetClock]") {
  PresetClock clock;
  clock.advance(5.0, 1.0f); // the render thread was paused for five seconds
  CHECK(clock.time() == Approx(PresetClock::kMaxStepSeconds));
}

TEST_CASE("PresetClock keeps a soft cut moving at Speed 0", "[core][PresetClock]") {
  PresetClock clock;
  clock.onSoftCut(0.5);
  clock.advance(0.25, 0.0f);
  CHECK(clock.time() == Approx(0.25)); // real time while blending
  clock.advance(0.25, 0.0f);
  CHECK(clock.time() == Approx(0.5));
  clock.advance(0.25, 0.0f); // blend over: Speed 0 holds again
  CHECK(clock.time() == Approx(0.5));

  PresetClock fast;
  fast.onSoftCut(1.0);
  fast.advance(0.25, 3.0f); // faster than real time is kept
  CHECK(fast.time() == Approx(0.75));
}
