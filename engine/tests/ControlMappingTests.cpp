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

TEST_CASE("toEngineControls maps the Visual globals and the gate", "[engine][ControlMapping]") {
  engine::ParameterValues values;
  auto controls = engine::toEngineControls(values);
  CHECK(controls.visual == core::VisualControls{}); // defaults are neutral
  CHECK_FALSE(controls.gate.enabled);
  CHECK(controls.gate.thresholdDb == -80.0f);
  CHECK(controls.gate.releaseMs == 80.0f);

  values.visualHue = 90.0f;
  values.visualMirror = 3.0f;
  values.visualKaleidoscope = 5.0f; // the sixth choice: 8 segments
  values.visualSpeed = 9.0f;        // out of range: clamped
  values.layerGateEnabled = 1.0f;
  values.layerGateThreshold = -30.0f;
  controls = engine::toEngineControls(values);
  CHECK(controls.visual.hueDegrees == 90.0f);
  CHECK(controls.visual.mirror == core::MirrorMode::Quad);
  CHECK(controls.visual.kaleidoscopeSegments == 8);
  CHECK(controls.visual.speed == 4.0f);
  CHECK(controls.gate.enabled);
  CHECK(controls.gate.thresholdDb == -30.0f);
}

TEST_CASE("visualKaleidoscope's choices line up with kKaleidoscopeSegments", "[engine][ControlMapping]") {
  const auto* spec = core::findParameter(core::allParameters(), "visualKaleidoscope");
  REQUIRE(spec != nullptr);
  REQUIRE(spec->choices.size() == core::kKaleidoscopeSegments.size());
  CHECK(spec->choices[0] == "Off");
  for (std::size_t i = 1; i < spec->choices.size(); ++i) {
    CHECK(spec->choices[i] == std::to_string(core::kKaleidoscopeSegments[i]));
  }
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
  CHECK(controls.qualityScale == 0.0f); // Auto: adaptive (5.3)

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
