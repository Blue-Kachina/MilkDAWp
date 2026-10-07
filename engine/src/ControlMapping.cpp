// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/ControlMapping.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::engine {

float ParameterValues::* parameterMember(std::string_view id) noexcept {
  struct Entry {
    std::string_view id;
    float ParameterValues::*field;
  };
  static constexpr Entry kFields[] = {
      {"beatSensitivity", &ParameterValues::beatSensitivity},
      {"transitionDurationSeconds", &ParameterValues::transitionDurationSeconds},
      {"shuffle", &ParameterValues::shuffle},
      {"lockCurrentPreset", &ParameterValues::lockCurrentPreset},
      {"presetIndex", &ParameterValues::presetIndex},
      {"transitionJitterEnabled", &ParameterValues::transitionJitterEnabled},
      {"transitionDurationMin", &ParameterValues::transitionDurationMin},
      {"transitionDurationMax", &ParameterValues::transitionDurationMax},
      {"hardCutEnabled", &ParameterValues::hardCutEnabled},
      {"softCutDuration", &ParameterValues::softCutDuration},
      {"qualityOverride", &ParameterValues::qualityOverride},
      {"transitionMode", &ParameterValues::transitionMode},
      {"transitionBars", &ParameterValues::transitionBars},
      {"presetSelectionPolicy", &ParameterValues::presetSelectionPolicy},
      {"energyThreshold", &ParameterValues::energyThreshold},
      {"useHostTempo", &ParameterValues::useHostTempo},
      {"layerOpacity", &ParameterValues::layerOpacity},
      {"layerBlend", &ParameterValues::layerBlend},
      {"layerMute", &ParameterValues::layerMute},
      {"layerOrder", &ParameterValues::layerOrder},
      {"transitionGridSync", &ParameterValues::transitionGridSync},
      {"transitionGridOffset", &ParameterValues::transitionGridOffset},
      {"visualHue", &ParameterValues::visualHue},
      {"visualSaturation", &ParameterValues::visualSaturation},
      {"visualBrightness", &ParameterValues::visualBrightness},
      {"visualSpeed", &ParameterValues::visualSpeed},
      {"visualZoom", &ParameterValues::visualZoom},
      {"visualRotation", &ParameterValues::visualRotation},
      {"visualWarp", &ParameterValues::visualWarp},
      {"visualTrails", &ParameterValues::visualTrails},
      {"visualWaveSize", &ParameterValues::visualWaveSize},
      {"visualPixelate", &ParameterValues::visualPixelate},
      {"visualGlow", &ParameterValues::visualGlow},
      {"visualBlur", &ParameterValues::visualBlur},
      {"visualMirror", &ParameterValues::visualMirror},
      {"visualKaleidoscope", &ParameterValues::visualKaleidoscope},
      {"visualRgbSplit", &ParameterValues::visualRgbSplit},
      {"visualMediaMix", &ParameterValues::visualMediaMix},
      {"macro1", &ParameterValues::macro1},
      {"macro2", &ParameterValues::macro2},
      {"macro3", &ParameterValues::macro3},
      {"macro4", &ParameterValues::macro4},
      {"macro5", &ParameterValues::macro5},
      {"macro6", &ParameterValues::macro6},
      {"macro7", &ParameterValues::macro7},
      {"macro8", &ParameterValues::macro8},
      {"lockMacros", &ParameterValues::lockMacros},
      {"layerGateEnabled", &ParameterValues::layerGateEnabled},
      {"layerGateThreshold", &ParameterValues::layerGateThreshold},
      {"layerGateRelease", &ParameterValues::layerGateRelease},
  };
  for (const auto& entry : kFields) {
    if (entry.id == id) {
      return entry.field;
    }
  }
  return nullptr;
}

float* parameterField(ParameterValues& values, std::string_view id) noexcept {
  const auto member = parameterMember(id);
  return member != nullptr ? &(values.*member) : nullptr;
}

const float* parameterField(const ParameterValues& values, std::string_view id) noexcept {
  const auto member = parameterMember(id);
  return member != nullptr ? &(values.*member) : nullptr;
}

float qualityScaleFor(int choice) noexcept {
  switch (choice) {
  case 1:
    return 0.5f;
  case 2:
    return 0.75f;
  case 3:
    return 1.0f;
  default:
    return 0.0f; // Auto
  }
}

core::VisualControls toVisualControls(const ParameterValues& values) noexcept {
  core::VisualControls visual;
  visual.hueDegrees = std::clamp(values.visualHue, -180.0f, 180.0f);
  visual.saturation = std::clamp(values.visualSaturation, 0.0f, 2.0f);
  visual.brightness = std::clamp(values.visualBrightness, 0.0f, 2.0f);
  visual.speed = std::clamp(values.visualSpeed, 0.0f, 4.0f);
  visual.zoom = std::clamp(values.visualZoom, -1.0f, 1.0f);
  visual.rotation = std::clamp(values.visualRotation, -1.0f, 1.0f);
  visual.warp = std::clamp(values.visualWarp, 0.0f, 3.0f);
  visual.trails = std::clamp(values.visualTrails, 0.0f, 1.0f);
  visual.waveSize = std::clamp(values.visualWaveSize, 0.0f, 3.0f);
  visual.pixelate = std::clamp(values.visualPixelate, 0.0f, 1.0f);
  visual.glow = std::clamp(values.visualGlow, 0.0f, 1.0f);
  visual.blur = std::clamp(values.visualBlur, 0.0f, 1.0f);
  visual.mirror = static_cast<core::MirrorMode>(std::clamp(static_cast<int>(std::lround(values.visualMirror)), 0, 3));
  const auto kaleidoscope = std::clamp(static_cast<int>(std::lround(values.visualKaleidoscope)), 0,
                                       static_cast<int>(core::kKaleidoscopeSegments.size()) - 1);
  visual.kaleidoscopeSegments = core::kKaleidoscopeSegments[static_cast<std::size_t>(kaleidoscope)];
  visual.rgbSplit = std::clamp(values.visualRgbSplit, 0.0f, 1.0f);
  visual.mediaMix = std::clamp(values.visualMediaMix, 0.0f, 1.0f);
  return visual;
}

EngineControls toEngineControls(const ParameterValues& values) noexcept {
  EngineControls controls;
  controls.transitionMode = static_cast<core::TransitionMode>(
      std::clamp(static_cast<int>(values.transitionMode), 0, static_cast<int>(core::TransitionMode::Energy)));
  controls.transitionBars = static_cast<std::uint32_t>(std::max(1.0f, values.transitionBars));
  controls.gridAnchored = values.transitionGridSync > 0.5f;
  controls.gridOffsetBeats = static_cast<std::uint32_t>(std::max(0.0f, std::round(values.transitionGridOffset)));
  controls.timedDurationSeconds = values.transitionDurationSeconds;
  controls.jitterEnabled = values.transitionJitterEnabled > 0.5f;
  controls.jitterMinSeconds = values.transitionDurationMin;
  controls.jitterMaxSeconds = values.transitionDurationMax;
  controls.cutStyle = values.hardCutEnabled > 0.5f ? core::CutStyle::Hard : core::CutStyle::Soft;
  controls.blendSeconds = values.softCutDuration;
  controls.energyThreshold = values.energyThreshold;
  controls.useHostTempo = values.useHostTempo > 0.5f;
  controls.locked = values.lockCurrentPreset > 0.5f;
  // v1's Shuffle toggle wins over the v2 policy choice when on.
  controls.policy = values.shuffle > 0.5f ? core::PlaylistPolicy::ShuffleNoRepeat
                                          : static_cast<core::PlaylistPolicy>(
                                                std::clamp(static_cast<int>(values.presetSelectionPolicy), 0, 2));
  controls.presetIndex = static_cast<std::int32_t>(std::lround(values.presetIndex));
  controls.beatSensitivity = values.beatSensitivity;
  controls.qualityScale = qualityScaleFor(static_cast<int>(values.qualityOverride));
  controls.visual = toVisualControls(values);
  controls.gate.enabled = values.layerGateEnabled > 0.5f;
  controls.gate.thresholdDb = std::clamp(values.layerGateThreshold, -100.0f, 0.0f);
  controls.gate.releaseMs = std::clamp(values.layerGateRelease, 0.0f, 2000.0f);
  controls.macros = {values.macro1, values.macro2, values.macro3, values.macro4,
                     values.macro5, values.macro6, values.macro7, values.macro8};
  for (auto& macro : controls.macros) {
    macro = std::clamp(macro, 0.0f, 1.0f);
  }
  return controls;
}

} // namespace milkdawp::engine
