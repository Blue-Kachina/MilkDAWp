// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace milkdawp::core {

/// One entry of an engine's recent-errors log (5.9). A message repeated back
/// to back is kept once, with a count and the time it last happened.
struct DiagnosticsError {
  std::int64_t lastTimeMs = 0; // milliseconds since the Unix epoch
  std::string source;          // "projectM", "preset", ...
  std::string message;
  std::uint32_t count = 1;
};

/// Everything the diagnostics panel (5.9) shows and copies, as plain data:
/// the engine fills its part (`engine::Visualizer::diagnostics()`), the shell
/// adds its own (`shell`, `surface`, `input`), and `ui::DiagnosticsPanel`
/// formats it. Plain data so the three layers can share it without `ui`
/// depending on `engine` (§4.1).
struct DiagnosticsInfo {
  // Shell
  std::string shell;   // e.g. "MilkDAWp2 0.1.0 (VST3 in REAPER)"
  std::string surface; // how the picture reaches the window, e.g. "shared context"
  std::string input;   // the app's audio input; empty in the plugin

  // Render engine
  bool engineAvailable = false;
  std::string engineUnavailableReason; // empty while starting or when available
  std::string projectMVersion;
  std::string glDescription; // vendor / renderer / version
  bool paused = true;
  float framesPerSecond = 0.0f;
  float cpuFrameMs = 0.0f;
  float gpuFrameMs = -1.0f; // -1: not measured
  int width = 0;
  int height = 0;
  float qualityScale = 1.0f;
  bool qualityAuto = true;
  int gpuSharers = 1;
  int layers = 1;
  float lastPresetLoadMs = 0.0f;
  std::uint32_t presetsLoaded = 0;

  // Director
  std::string beatSource; // "detected", "host", "none"
  float bpm = 0.0f;
  float beatConfidence = 0.0f;
  std::uint32_t playlistSize = 0;
  std::uint32_t presetsSkipped = 0;
  std::string currentPreset; // display name, empty when none
  float bassLevelDb = -120.0f;
  float bassReferenceDb = -120.0f;
  bool inBreakdown = false;
  std::uint32_t dropsDetected = 0;

  /// Oldest first.
  std::vector<DiagnosticsError> recentErrors;
};

} // namespace milkdawp::core
