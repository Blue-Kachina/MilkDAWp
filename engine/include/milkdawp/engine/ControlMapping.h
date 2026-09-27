// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string_view>

#include "milkdawp/engine/Director.h"

namespace milkdawp::engine {

/// The raw values of the parameters that drive the engine, one field per
/// `core::ParameterModel` id, in that model's units (a Bool is 0/1, a Choice
/// its index). Both shells hold these: the plugin reads them from its APVTS
/// atomics every block, the app from its preferences and widgets. One
/// mapping (`toEngineControls`) turns them into `EngineControls`, so the two
/// shells cannot drift. Defaults match `core::allParameters()` (unit-tested).
struct ParameterValues {
  float beatSensitivity = 1.0f;
  float transitionDurationSeconds = 5.0f;
  float shuffle = 0.0f;
  float lockCurrentPreset = 0.0f;
  float presetIndex = 0.0f;
  float transitionJitterEnabled = 0.0f;
  float transitionDurationMin = 3.0f;
  float transitionDurationMax = 15.0f;
  float hardCutEnabled = 0.0f;
  float softCutDuration = 3.0f;
  float qualityOverride = 0.0f;
  float transitionMode = 2.0f;
  float transitionBars = 4.0f;
  float presetSelectionPolicy = 0.0f;
  float energyThreshold = 2.0f;
};

/// The field for a `ParameterModel` id, or nullptr for parameters the engine
/// does not read (the momentary triggers, and v1's hard-cut sensitivity and
/// interval, which have no v2 meaning yet).
[[nodiscard]] float* parameterField(ParameterValues& values, std::string_view id) noexcept;
[[nodiscard]] const float* parameterField(const ParameterValues& values, std::string_view id) noexcept;

/// qualityOverride's choices (Auto / Low / Medium / High) as an FBO scale.
/// Auto is full resolution until adaptive quality (5.3) drives the scale.
[[nodiscard]] float qualityScaleFor(int choice) noexcept;

/// Real-time safe: no allocation, no locks.
[[nodiscard]] EngineControls toEngineControls(const ParameterValues& values) noexcept;

} // namespace milkdawp::engine
