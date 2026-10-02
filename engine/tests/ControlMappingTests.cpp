// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/engine/ControlMapping.h"

using namespace milkdawp;

TEST_CASE("ParameterValues defaults match the parameter model", "[engine][ControlMapping]") {
  const engine::ParameterValues defaults;
  // Parameters the engine deliberately does not read.
  const std::set<std::string> unmapped{"triggerNext", "triggerPrev", "hardCutSensitivity", "hardCutDuration"};
  for (const auto& spec : core::allParameters()) {
    INFO("parameter " << spec.id);
    const float* field = engine::parameterField(defaults, spec.id);
    if (unmapped.contains(spec.id)) {
      CHECK(field == nullptr);
      continue;
    }
    REQUIRE(field != nullptr);
    CHECK(*field == spec.defaultValue);
  }
  CHECK(engine::parameterField(defaults, "noSuchParameter") == nullptr);
}

TEST_CASE("toEngineControls maps the bar-grid sync and offset", "[engine][ControlMapping][layers]") {
  engine::ParameterValues values;
  auto controls = engine::toEngineControls(values);
  CHECK_FALSE(controls.gridAnchored); // off by default: cuts count bars from when the instance started
  CHECK(controls.gridOffsetBeats == 0);

  values.transitionGridSync = 1.0f;
  values.transitionGridOffset = 5.0f;
  controls = engine::toEngineControls(values);
  CHECK(controls.gridAnchored);
  CHECK(controls.gridOffsetBeats == 5);

  values.transitionGridOffset = -3.0f; // out of range input never wraps to a huge unsigned value
  CHECK(engine::toEngineControls(values).gridOffsetBeats == 0);
}

TEST_CASE("parameterField writes through to the struct", "[engine][ControlMapping]") {
  engine::ParameterValues values;
  *engine::parameterField(values, "transitionBars") = 8.0f;
  CHECK(values.transitionBars == 8.0f);
}

TEST_CASE("toEngineControls maps model units to engine controls", "[engine][ControlMapping]") {
  engine::ParameterValues values;
  auto controls = engine::toEngineControls(values);
  CHECK(controls.transitionMode == core::TransitionMode::BeatQuantized);
  CHECK(controls.transitionBars == 4);
  CHECK(controls.cutStyle == core::CutStyle::Soft);
  CHECK(controls.policy == core::PlaylistPolicy::Sequential);
  CHECK(controls.qualityScale == 1.0f);

  values.transitionMode = 99.0f;  // clamped
  values.transitionBars = 0.0f;   // at least one bar
  values.hardCutEnabled = 1.0f;
  values.presetSelectionPolicy = 2.0f;
  values.qualityOverride = 1.0f;  // Low
  values.lockCurrentPreset = 1.0f;
  values.presetIndex = 6.6f;
  controls = engine::toEngineControls(values);
  CHECK(controls.transitionMode == core::TransitionMode::Energy);
  CHECK(controls.transitionBars == 1);
  CHECK(controls.cutStyle == core::CutStyle::Hard);
  CHECK(controls.policy == core::PlaylistPolicy::Weighted);
  CHECK(controls.qualityScale == 0.5f);
  CHECK(controls.locked);
  CHECK(controls.presetIndex == 7);

  values.shuffle = 1.0f; // v1's Shuffle wins over the policy choice
  CHECK(engine::toEngineControls(values).policy == core::PlaylistPolicy::ShuffleNoRepeat);
}
