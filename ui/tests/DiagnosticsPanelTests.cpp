// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/ui/DiagnosticsPanel.h"

using namespace milkdawp;
using namespace milkdawp::ui;

namespace {

core::DiagnosticsInfo runningEngine() {
  core::DiagnosticsInfo info;
  info.shell = "MilkDAWp2 0.1.0 (VST3 in REAPER)";
  info.surface = "shared context";
  info.engineAvailable = true;
  info.projectMVersion = "4.2.0";
  info.glDescription = "NVIDIA Corporation / GeForce RTX 4070 Ti / 4.6.0";
  info.paused = false;
  info.framesPerSecond = 59.9f;
  info.cpuFrameMs = 0.6f;
  info.gpuFrameMs = 0.4f;
  info.width = 1280;
  info.height = 720;
  info.beatSource = "detected";
  info.bpm = 128.0f;
  info.beatConfidence = 0.82f;
  info.playlistSize = 120;
  info.presetsLoaded = 34;
  info.presetsSkipped = 2;
  info.currentPreset = "Geiss/Spiral";
  return info;
}

bool anyLineContains(const juce::StringArray& lines, const juce::String& text) {
  for (const auto& line : lines) {
    if (line.contains(text)) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CASE("The diagnostics lines name the renderer, frame timing, beat and presets (5.9)", "[ui][Diagnostics]") {
  const auto lines = formatDiagnosticsLines(runningEngine(), DiagnosticsPanel::kVisibleErrors);
  CHECK(anyLineContains(lines, "projectM 4.2.0"));
  CHECK(anyLineContains(lines, "GeForce RTX 4070 Ti"));
  CHECK(anyLineContains(lines, "59.9 fps"));
  CHECK(anyLineContains(lines, "gpu 0.4 ms"));
  CHECK(anyLineContains(lines, "128.0 BPM, confidence 0.82"));
  CHECK(anyLineContains(lines, "120 in folder, 34 loaded, 2 skipped"));
  CHECK(anyLineContains(lines, "Geiss/Spiral"));
  CHECK(anyLineContains(lines, "surface: shared context"));
  CHECK(anyLineContains(lines, "errors: none"));
  CHECK_FALSE(anyLineContains(lines, "GPU shared")); // only with several instances
}

TEST_CASE("The diagnostics lines explain an unavailable or paused engine", "[ui][Diagnostics]") {
  auto info = runningEngine();
  info.paused = true;
  CHECK(anyLineContains(formatDiagnosticsLines(info, 5), "paused"));
  info.gpuSharers = 3;
  info.gpuFrameMs = -1.0f;
  CHECK(anyLineContains(formatDiagnosticsLines(info, 5), "GPU shared by 3 instances"));
  CHECK(anyLineContains(formatDiagnosticsLines(info, 5), "gpu n/a"));

  core::DiagnosticsInfo unavailable;
  unavailable.engineUnavailableReason = "could not locate or load 'projectM-4.dll'";
  CHECK(formatDiagnosticsLines(unavailable, 5)[0] == "projectM: unavailable (could not locate or load 'projectM-4.dll')");
  CHECK(formatDiagnosticsLines(core::DiagnosticsInfo{}, 5)[0] == "projectM: starting...");
}

TEST_CASE("The panel shows the newest errors; the copied report has them all", "[ui][Diagnostics]") {
  auto info = runningEngine();
  for (int i = 0; i < 8; ++i) {
    info.recentErrors.push_back({0, "preset", "p" + std::to_string(i) + ".milk skipped: empty file", 1});
  }
  info.recentErrors.back().count = 3;

  const auto lines = formatDiagnosticsLines(info, 5);
  CHECK(anyLineContains(lines, "recent errors (8, newest 5):"));
  CHECK_FALSE(anyLineContains(lines, "p2.milk"));
  CHECK(anyLineContains(lines, "p3.milk"));
  CHECK(anyLineContains(lines, "p7.milk skipped: empty file (x3)"));

  const auto report = formatDiagnosticsReport(info);
  CHECK(report.startsWith("MilkDAWp2 0.1.0 (VST3 in REAPER)\n"));
  CHECK(report.contains("p0.milk"));
  CHECK(report.contains("p7.milk"));
  CHECK(report.contains("GeForce RTX 4070 Ti"));
}

TEST_CASE("DiagnosticsPanel grows with its lines", "[ui][Diagnostics]") {
  juce::ScopedJuceInitialiser_GUI juce;
  DiagnosticsPanel panel;
  panel.update(runningEngine());
  const int quiet = panel.preferredHeight();
  auto info = runningEngine();
  info.recentErrors.push_back({0, "projectM", "shader failed to compile", 1});
  panel.update(info);
  CHECK(panel.preferredHeight() > quiet);
  CHECK(panel.info().recentErrors.size() == 1);
}
