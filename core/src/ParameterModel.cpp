// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/ParameterModel.h"

#include <algorithm>
#include <sstream>

namespace milkdawp::core {

const std::vector<ParameterSpec>& allParameters() {
  static const std::vector<ParameterSpec> params = {
      // --- Carried forward from v1 unchanged (§2.9) ---
      // 5.5: projectM's own beat sensitivity: how strongly presets react to the
      // bass/mid/treble levels they animate from. It detects no beats and causes
      // no transitions; our detector's threshold is deliberately not tied to it
      // (scaling it broke beat tracking on the fixtures; see the roadmap's 5.5).
      {"beatSensitivity", "Beat Sensitivity", ParameterType::Float, 0.0f, 2.0f, 1.0f, true,
       "beatSensitivity", {}},
      {"transitionDurationSeconds", "Transition Duration (s)", ParameterType::Float, 0.1f, 30.0f, 5.0f,
       true, "transitionDurationSeconds", {}},
      {"shuffle", "Shuffle", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true, "shuffle", {}},
      {"lockCurrentPreset", "Lock Current Preset", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true,
       "lockCurrentPreset", {}},
      {"presetIndex", "Preset Index", ParameterType::Int, 0.0f, 4095.0f, 0.0f, true, "presetIndex", {}},
      {"triggerNext", "Next Preset", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true, "triggerNext", {}},
      {"triggerPrev", "Previous Preset", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true, "triggerPrev", {}},
      {"transitionJitterEnabled", "Transition Jitter", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true,
       "transitionJitterEnabled", {}},
      {"transitionDurationMin", "Transition Duration Min (s)", ParameterType::Float, 0.1f, 30.0f, 3.0f,
       true, "transitionDurationMin", {}},
      {"transitionDurationMax", "Transition Duration Max (s)", ParameterType::Float, 0.1f, 30.0f, 15.0f,
       true, "transitionDurationMax", {}},
      {"hardCutEnabled", "Hard Cuts", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true, "hardCutEnabled", {}},
      {"hardCutSensitivity", "Hard Cut Sensitivity", ParameterType::Float, 0.0f, 1.0f, 0.5f, true,
       "hardCutSensitivity", {}},
      {"softCutDuration", "Blend Time (s)", ParameterType::Float, 0.5f, 10.0f, 3.0f, true,
       "softCutDuration", {}},
      {"hardCutDuration", "Min. Cut Interval (s)", ParameterType::Float, 1.0f, 30.0f, 5.0f, true,
       "hardCutDuration", {}},
      {"qualityOverride", "Quality", ParameterType::Choice, 0.0f, 3.0f, 0.0f, true, "qualityOverride",
       {"Auto", "Low", "Medium", "High"}},

      // --- New in v2 (§4.4 TransitionScheduler; defaults per §7 Phase 3.4) ---
      {"transitionMode", "Transition Mode", ParameterType::Choice, 0.0f, 4.0f, 2.0f, true, "",
       {"Manual", "Timed", "BeatQuantized", "Hybrid", "Energy"}},
      {"transitionBars", "Transition Bars (N)", ParameterType::Int, 1.0f, 16.0f, 4.0f, true, "", {}},
      {"presetSelectionPolicy", "Preset Selection", ParameterType::Choice, 0.0f, 2.0f, 0.0f, true, "",
       {"Sequential", "ShuffleNoRepeat", "Weighted"}},
      // Energy mode: how far the bass must jump back up after a breakdown to count
      // as a drop: 4 + 3 x this in dB (SectionDetectorConfig::jumpDb, set in Director.cpp).
      {"energyThreshold", "Energy Threshold", ParameterType::Float, 0.5f, 4.0f, 2.0f, true, "", {}},
      // Off: the audio-detected tempo always drives the beat modes, even while the host plays
      // (no click, or a host tempo that doesn't match the music). On: the host's beat grid wins.
      {"useHostTempo", "BPM From DAW", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true, "", {}},

      // --- Layers (layers_like_shrek.md §2.3): how this instance's picture is mixed when its Output
      // targets another instance's canvas. They do nothing while an instance shows its own window.
      {"layerOpacity", "Layer Opacity", ParameterType::Float, 0.0f, 1.0f, 1.0f, true, "", {}},
      {"layerBlend", "Layer Blend", ParameterType::Choice, 0.0f, 4.0f, 0.0f, true, "",
       {"Normal", "Add", "Screen", "Multiply", "Luma key"}},
      {"layerMute", "Layer Mute", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true, "", {}},
      // Lower draws first (bottom). Equal values keep the order the layers joined the canvas.
      {"layerOrder", "Layer Order", ParameterType::Int, 0.0f, 7.0f, 0.0f, true, "", {}},

      // --- Layers: cutting together (layers_like_shrek.md L7) ---
      // On: beat-quantized cuts land on the beats where beat % (bars x 4) == the offset, instead of every N bars
      // counted from when this instance started. With "BPM From DAW" every instance shares the host's beat
      // numbers, so equal settings cut together and different offsets stagger ("kick on 1, vocal on 3").
      {"transitionGridSync", "Sync Cuts to Bar Grid", ParameterType::Bool, 0.0f, 1.0f, 0.0f, true, "", {}},
      {"transitionGridOffset", "Grid Offset (beats)", ParameterType::Int, 0.0f, 15.0f, 0.0f, true, "", {}},
  };
  return params;
}

const ParameterSpec* findParameter(const std::vector<ParameterSpec>& params, const std::string& id) {
  auto it = std::find_if(params.begin(), params.end(), [&](const ParameterSpec& p) { return p.id == id; });
  return it != params.end() ? &(*it) : nullptr;
}

const ParameterSpec* findByV1Alias(const std::vector<ParameterSpec>& params, const std::string& v1Id) {
  if (v1Id.empty()) {
    return nullptr;
  }
  auto it = std::find_if(params.begin(), params.end(),
                          [&](const ParameterSpec& p) { return p.v1Alias == v1Id; });
  return it != params.end() ? &(*it) : nullptr;
}

namespace {
std::string formatFloat(float value) {
  std::ostringstream out;
  out.precision(4);
  out << value;
  return out.str();
}

std::string formatDefault(const ParameterSpec& p) {
  switch (p.type) {
  case ParameterType::Bool:
    return p.defaultValue != 0.0f ? "true" : "false";
  case ParameterType::Choice: {
    const auto index = static_cast<std::size_t>(p.defaultValue);
    return index < p.choices.size() ? p.choices[index] : std::to_string(p.defaultValue);
  }
  case ParameterType::Int:
    return std::to_string(static_cast<int>(p.defaultValue));
  case ParameterType::Float:
  default:
    return formatFloat(p.defaultValue);
  }
}

std::string formatRange(const ParameterSpec& p) {
  switch (p.type) {
  case ParameterType::Bool:
    return "true / false";
  case ParameterType::Choice: {
    std::string s;
    for (std::size_t i = 0; i < p.choices.size(); ++i) {
      if (i > 0) {
        s += " / ";
      }
      s += p.choices[i];
    }
    return s;
  }
  case ParameterType::Int:
    return std::to_string(static_cast<int>(p.minValue)) + " .. " + std::to_string(static_cast<int>(p.maxValue));
  case ParameterType::Float:
  default:
    return formatFloat(p.minValue) + " .. " + formatFloat(p.maxValue);
  }
}

const char* typeName(ParameterType type) {
  switch (type) {
  case ParameterType::Float:
    return "Float";
  case ParameterType::Bool:
    return "Bool";
  case ParameterType::Int:
    return "Int";
  case ParameterType::Choice:
    return "Choice";
  }
  return "?";
}
} // namespace

std::string renderParameterDocsMarkdown(const std::vector<ParameterSpec>& params) {
  std::ostringstream out;
  out << "| ID | Name | Type | Range / Choices | Default | Automatable | v1 alias |\n";
  out << "|---|---|---|---|---|---|---|\n";
  for (const auto& p : params) {
    out << "| `" << p.id << "` | " << p.displayName << " | " << typeName(p.type) << " | "
        << formatRange(p) << " | " << formatDefault(p) << " | " << (p.automatable ? "yes" : "no")
        << " | " << (p.v1Alias.empty() ? "*(new in v2)*" : ("`" + p.v1Alias + "`")) << " |\n";
  }
  return out.str();
}

} // namespace milkdawp::core
