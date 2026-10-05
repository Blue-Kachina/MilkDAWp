// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 8.2b: the layer gate, driven frame by frame as the render thread does,
// with synthetic audio whose stops and decays land on known frames.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "milkdawp/core/LayerGate.h"

using namespace milkdawp::core;
using Catch::Approx;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kFps = 60;
constexpr int kSamplesPerFrame = static_cast<int>(kSampleRate) / kFps;
constexpr float kDt = 1.0f / static_cast<float>(kFps);

// A sine whose amplitude follows `level(frame)` (linear), one render frame at a time.
template <typename Level> std::vector<float> peaksPerFrame(int frames, Level level) {
  std::vector<float> peaks;
  std::vector<float> block(kSamplesPerFrame);
  for (int f = 0; f < frames; ++f) {
    for (int i = 0; i < kSamplesPerFrame; ++i) {
      const double t = (f * kSamplesPerFrame + i) / kSampleRate;
      block[static_cast<std::size_t>(i)] = static_cast<float>(level(f) * std::sin(2.0 * std::numbers::pi * 220.0 * t));
    }
    peaks.push_back(peakDbfs(block.data(), block.size()));
  }
  return peaks;
}

float fromDb(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

TEST_CASE("peakDbfs measures the loudest sample", "[core][LayerGate]") {
  const std::vector<float> half{0.1f, -0.5f, 0.25f};
  CHECK(peakDbfs(half.data(), half.size()) == Approx(-6.02f).margin(0.01f));
  const std::vector<float> silence(64, 0.0f);
  CHECK(peakDbfs(silence.data(), silence.size()) == -200.0f);
  CHECK(peakDbfs(nullptr, 0) == -200.0f);
}

TEST_CASE("A disabled gate never hides the layer", "[core][LayerGate]") {
  LayerGate gate;
  const LayerGateSettings off; // enabled = false
  for (int i = 0; i < 120; ++i) {
    CHECK(gate.process(-200.0f, kDt, off) == 1.0f);
  }
  CHECK(gate.isOpen());
}

TEST_CASE("The gate follows clean stops: open on the hit, gone after hold + release", "[core][LayerGate]") {
  // 0.5 s of guitar at -20 dB, 0.5 s of silence, three times.
  const auto peaks = peaksPerFrame(180, [](int f) { return (f / 30) % 2 == 0 ? fromDb(-20.0f) : 0.0f; });
  LayerGateSettings settings;
  settings.enabled = true;
  settings.thresholdDb = -40.0f;
  settings.releaseMs = 80.0f;

  LayerGate gate;
  std::vector<float> envelope;
  std::vector<bool> open;
  for (const float peak : peaks) {
    envelope.push_back(gate.process(peak, kDt, settings));
    open.push_back(gate.isOpen());
  }

  for (int burst = 0; burst < 3; ++burst) {
    const int start = burst * 60;      // first frame of the note
    const int stop = start + 30;       // first silent frame
    INFO("burst " << burst);
    // Instant open: the note's first frame is fully visible.
    CHECK(envelope[static_cast<std::size_t>(start)] == 1.0f);
    for (int f = start; f < stop; ++f) {
      CHECK(open[static_cast<std::size_t>(f)]);
    }
    // Held open for 50 ms (3 frames at 60 fps), then closed.
    CHECK(open[static_cast<std::size_t>(stop + 1)]);
    const int closedBy = stop + 3;
    CHECK_FALSE(open[static_cast<std::size_t>(closedBy)]);
    // Faded out over the 80 ms release (5 frames), then fully gone until the next note.
    CHECK(envelope[static_cast<std::size_t>(closedBy)] < 1.0f);
    for (int f = closedBy + 5; f < stop + 30; ++f) {
      CHECK(envelope[static_cast<std::size_t>(f)] == 0.0f);
    }
  }
}

TEST_CASE("The gate does not flicker on a note decaying through the threshold", "[core][LayerGate]") {
  // A note that decays from -20 dB to -70 dB over two seconds, with a wobble
  // (tremolo, beating strings) riding on it, past a -40 dB threshold. The
  // wobble is 2.4 dB peak to peak: inside the 3 dB hysteresis, as intended.
  const auto peaks = peaksPerFrame(120, [](int f) {
    const float db = -20.0f - 50.0f * static_cast<float>(f) / 120.0f + 1.2f * std::sin(static_cast<float>(f) * 1.7f);
    return fromDb(db);
  });
  LayerGateSettings settings;
  settings.enabled = true;
  settings.thresholdDb = -40.0f;

  LayerGate gate;
  int changes = 0;
  bool wasOpen = true;
  for (const float peak : peaks) {
    gate.process(peak, kDt, settings);
    changes += gate.isOpen() != wasOpen ? 1 : 0;
    wasOpen = gate.isOpen();
  }
  CHECK(changes == 1); // closed once, near the end, and stayed closed
  CHECK_FALSE(gate.isOpen());
}

TEST_CASE("Release 0 is a hard cut, and a long release fades", "[core][LayerGate]") {
  LayerGateSettings settings;
  settings.enabled = true;
  settings.thresholdDb = -40.0f;

  SECTION("hard cut") {
    settings.releaseMs = 0.0f;
    LayerGate gate;
    gate.process(-200.0f, 0.06f, settings); // past the hold
    CHECK(gate.envelope() == 0.0f);
  }
  SECTION("one second fade") {
    settings.releaseMs = 1000.0f;
    LayerGate gate;
    gate.process(-200.0f, 0.06f, settings); // closes (hold elapsed): first fade step
    gate.process(-200.0f, 0.5f, settings);
    CHECK(gate.envelope() == Approx(0.44f).margin(0.01f));
    gate.process(-10.0f, kDt, settings); // the next hit reopens at full strength
    CHECK(gate.envelope() == 1.0f);
  }
}
