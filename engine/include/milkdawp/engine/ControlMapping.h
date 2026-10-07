// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string_view>

#include "milkdawp/core/VisualControls.h"
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
  float useHostTempo = 0.0f;
  // Layers: how this instance is mixed onto another instance's canvas. Not part
  // of EngineControls (the Director never reads them); a shell applies them to
  // its `LayerChannel` (opacity, blend, visible = !mute, order).
  float layerOpacity = 1.0f;
  float layerBlend = 0.0f;
  float layerMute = 0.0f;
  float layerOrder = 0.0f;
  float transitionGridSync = 0.0f;
  float transitionGridOffset = 0.0f;
  // Phase 8.1: the Visual globals (EngineControls::visual), at their neutral values.
  float visualHue = 0.0f;
  float visualSaturation = 1.0f;
  float visualBrightness = 1.0f;
  float visualSpeed = 1.0f;
  float visualZoom = 0.0f;
  float visualRotation = 0.0f;
  float visualWarp = 1.0f;
  float visualTrails = 0.0f;
  float visualWaveSize = 1.0f;
  float visualPixelate = 0.0f;
  float visualGlow = 0.0f;
  float visualBlur = 0.0f;
  float visualMirror = 0.0f;
  float visualKaleidoscope = 0.0f;
  float visualRgbSplit = 0.0f;
  float visualMediaMix = 0.0f;
  // Macros: EngineControls::macros, which .milkdawp presets read (8.10).
  float macro1 = 0.0f;
  float macro2 = 0.0f;
  float macro3 = 0.0f;
  float macro4 = 0.0f;
  float macro5 = 0.0f;
  float macro6 = 0.0f;
  float macro7 = 0.0f;
  float macro8 = 0.0f;
  float lockMacros = 0.0f;
  // 8.2b: the layer gate (EngineControls::gate).
  float layerGateEnabled = 0.0f;
  float layerGateThreshold = -80.0f;
  float layerGateRelease = 80.0f;
};

/// The member for a `ParameterModel` id, or nullptr for parameters the engine
/// does not read (the momentary triggers, and v1's hard-cut sensitivity and
/// interval, which have no v2 meaning yet). Lets a shell look each id up once
/// and copy its values every block without searching again.
[[nodiscard]] float ParameterValues::* parameterMember(std::string_view id) noexcept;
/// The field for a `ParameterModel` id (see `parameterMember`).
[[nodiscard]] float* parameterField(ParameterValues& values, std::string_view id) noexcept;
[[nodiscard]] const float* parameterField(const ParameterValues& values, std::string_view id) noexcept;

/// The Visual globals in engine units. Real-time safe.
[[nodiscard]] core::VisualControls toVisualControls(const ParameterValues& values) noexcept;

/// qualityOverride's choices (Auto / Low / Medium / High) as an FBO scale.
/// Auto is 0: the render engine picks the scale itself (adaptive quality, 5.3).
[[nodiscard]] float qualityScaleFor(int choice) noexcept;

/// Real-time safe: no allocation, no locks.
[[nodiscard]] EngineControls toEngineControls(const ParameterValues& values) noexcept;

} // namespace milkdawp::engine
