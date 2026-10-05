// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/ControlMapping.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::engine {

float* parameterField(ParameterValues& values, std::string_view id) noexcept {
  return const_cast<float*>(parameterField(static_cast<const ParameterValues&>(values), id));
}

const float* parameterField(const ParameterValues& values, std::string_view id) noexcept {
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
  };
  for (const auto& entry : kFields) {
    if (entry.id == id) {
      return &(values.*entry.field);
    }
  }
  return nullptr;
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
  return controls;
}

} // namespace milkdawp::engine
