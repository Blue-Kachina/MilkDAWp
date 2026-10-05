// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/ui/TransitionSettings.h"

using milkdawp::core::TransitionMode;
using namespace milkdawp::ui;

TEST_CASE("Manual mode only offers the cut settings", "[ui][TransitionSettings]") {
  const auto r = transitionSettingsRelevance(TransitionMode::Manual, false, false);
  CHECK_FALSE(r.bars);
  CHECK_FALSE(r.timedDuration);
  CHECK_FALSE(r.jitter);
  CHECK_FALSE(r.energyThreshold);
  CHECK(r.blend);
}

TEST_CASE("Timed mode offers duration, or the jitter range when jitter is on", "[ui][TransitionSettings]") {
  const auto fixed = transitionSettingsRelevance(TransitionMode::Timed, false, false);
  CHECK(fixed.timedDuration);
  CHECK(fixed.jitter);
  CHECK_FALSE(fixed.jitterRange);
  CHECK_FALSE(fixed.bars);

  const auto jittered = transitionSettingsRelevance(TransitionMode::Timed, true, false);
  CHECK_FALSE(jittered.timedDuration);
  CHECK(jittered.jitterRange);
}

TEST_CASE("BeatQuantized offers bars and its timed fallback", "[ui][TransitionSettings]") {
  const auto r = transitionSettingsRelevance(TransitionMode::BeatQuantized, false, false);
  CHECK(r.bars);
  CHECK(r.timedDuration);
  CHECK_FALSE(r.energyThreshold);
}

TEST_CASE("Energy mode offers bars and the threshold, not timing", "[ui][TransitionSettings]") {
  const auto r = transitionSettingsRelevance(TransitionMode::Energy, true, false);
  CHECK(r.bars);
  CHECK(r.energyThreshold);
  CHECK_FALSE(r.timedDuration);
  CHECK_FALSE(r.jitterRange);
}

TEST_CASE("Hard cuts make the blend time irrelevant", "[ui][TransitionSettings]") {
  CHECK_FALSE(transitionSettingsRelevance(TransitionMode::Hybrid, false, true).blend);
}

TEST_CASE("The bar grid applies to the beat-quantized modes, and its offset only once it is on",
          "[ui][TransitionSettings][layers]") {
  const auto off = transitionSettingsRelevance(TransitionMode::BeatQuantized, false, false, false);
  CHECK(off.grid);
  CHECK_FALSE(off.gridOffset);
  const auto on = transitionSettingsRelevance(TransitionMode::BeatQuantized, false, false, true);
  CHECK(on.grid);
  CHECK(on.gridOffset);

  CHECK(transitionSettingsRelevance(TransitionMode::Energy, false, false, true).gridOffset); // its fallback
  for (const auto mode : {TransitionMode::Manual, TransitionMode::Timed, TransitionMode::Hybrid}) {
    CHECK_FALSE(transitionSettingsRelevance(mode, false, false, true).grid);
    CHECK_FALSE(transitionSettingsRelevance(mode, false, false, true).gridOffset);
  }
}

TEST_CASE("Beat badge shows the source and confidence", "[ui][BeatBadge]") {
  CHECK(describeBeat(BeatBadgeSource::None, 120.0f, 1.0f).text.endsWith("--"));
  CHECK(describeBeat(BeatBadgeSource::Detected, 0.0f, 1.0f).text.endsWith("--"));

  const auto host = describeBeat(BeatBadgeSource::Host, 127.6f, 0.0f);
  CHECK(host.text.endsWith("128 host"));

  const auto sure = describeBeat(BeatBadgeSource::Detected, 120.0f, 0.8f);
  CHECK(sure.text.endsWith("120 80%"));
  const auto unsure = describeBeat(BeatBadgeSource::Detected, 120.0f, 0.1f);
  CHECK(unsure.text.endsWith("120 10%"));
  CHECK(unsure.colour != sure.colour);
  CHECK(unsure.tooltip.contains("falls back"));
}

TEST_CASE("The tag filter status counts what automatic picks can choose", "[ui][TransitionSettings]") {
  CHECK(describeAutoSelection(0, 0, false).text.isEmpty());
  CHECK(describeAutoSelection(340, 340, false).text == "all 340 presets");
  CHECK(describeAutoSelection(12, 340, false).text == "12 of 340 presets");
  CHECK_FALSE(describeAutoSelection(12, 340, false).warning);
  const auto nothing = describeAutoSelection(340, 340, true);
  CHECK(nothing.text == "no preset has these tags");
  CHECK(nothing.warning);
}

TEST_CASE("The render quality shows only when it is reduced", "[ui][TransitionSettings]") {
  CHECK(describeRenderQuality(1.0f, true).isEmpty());
  CHECK(describeRenderQuality(1.0f, false).isEmpty());
  CHECK(describeRenderQuality(0.7f, true) == "render 70%");
  CHECK(describeRenderQuality(0.5f, false) == "render 50% (fixed)");
}
