// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/BeatClock.h"

using namespace milkdawp::core;

namespace {
constexpr double kSampleRate = 48000.0;
constexpr std::size_t kHopSize = 512;
} // namespace

TEST_CASE("BeatClock predicts and advances beats at a constant tempo with no onsets",
          "[core][BeatClock]") {
  BeatClock clock(kSampleRate, kHopSize);
  const TempoEstimate tempo{120.0f, 1.0f};
  const auto periodSamples = static_cast<std::uint64_t>(60.0 / 120.0 * kSampleRate);

  auto first = clock.processHop(0, tempo, std::nullopt);
  CHECK(first.beatIndex == 0);
  CHECK(first.nextBeatSample == periodSamples);

  // Advance sample position hop by hop until we cross the first predicted beat.
  BeatClockState state = first;
  std::uint64_t samplePos = 0;
  while (samplePos < periodSamples) {
    samplePos += kHopSize;
    state = clock.processHop(samplePos, tempo, std::nullopt);
  }

  CHECK(state.beatIndex == 1);
  CHECK(state.nextBeatSample == periodSamples * 2);
}

TEST_CASE("BeatClock's samplesUntilNextBeat counts down toward the predicted beat",
          "[core][BeatClock]") {
  BeatClock clock(kSampleRate, kHopSize);
  const TempoEstimate tempo{120.0f, 1.0f};

  auto state = clock.processHop(0, tempo, std::nullopt);
  const auto initialCountdown = clock.samplesUntilNextBeat(0);
  CHECK(initialCountdown == state.nextBeatSample);

  clock.processHop(kHopSize, tempo, std::nullopt);
  const auto laterCountdown = clock.samplesUntilNextBeat(kHopSize);
  CHECK(laterCountdown == initialCountdown - kHopSize);
}

TEST_CASE("BeatClock nudges its prediction toward a nearby onset", "[core][BeatClock]") {
  BeatClock clock(kSampleRate, kHopSize, /*phaseCorrectionGain=*/0.5f,
                   /*phaseCorrectionWindowFraction=*/0.2f);
  const TempoEstimate tempo{120.0f, 1.0f};

  auto initial = clock.processHop(0, tempo, std::nullopt);
  const auto predictedBeat = initial.nextBeatSample;

  // An onset arriving slightly early, well within the correction window.
  const Onset earlyOnset{predictedBeat - 200, 5.0f};
  auto corrected = clock.processHop(kHopSize, tempo, earlyOnset);

  // With gain 0.5, the new prediction should land halfway between the old
  // prediction and the onset.
  const auto expected = predictedBeat - 100;
  CHECK(corrected.nextBeatSample == Catch::Approx(static_cast<double>(expected)).margin(1.0));
}

TEST_CASE("BeatClock ignores an onset far outside the correction window", "[core][BeatClock]") {
  BeatClock clock(kSampleRate, kHopSize, /*phaseCorrectionGain=*/0.5f,
                   /*phaseCorrectionWindowFraction=*/0.1f);
  const TempoEstimate tempo{120.0f, 1.0f};

  auto initial = clock.processHop(0, tempo, std::nullopt);
  const auto predictedBeat = initial.nextBeatSample;

  // Half a beat period away -- nowhere near the 10% correction window.
  const auto periodSamples = static_cast<std::uint64_t>(60.0 / 120.0 * kSampleRate);
  const Onset farOnset{predictedBeat - periodSamples / 2, 5.0f};
  auto result = clock.processHop(kHopSize, tempo, farOnset);

  CHECK(result.nextBeatSample == predictedBeat);
}

TEST_CASE("BeatClock's downbeat heuristic favors the consistently louder beat phase",
          "[core][BeatClock]") {
  BeatClock clock(kSampleRate, kHopSize);
  const TempoEstimate tempo{120.0f, 1.0f};

  // Run for many bars. Beat phase 2 always gets a strong bass onset exactly
  // on the predicted beat; every other phase gets nothing.
  std::uint64_t samplePos = 0;
  BeatClockState state = clock.processHop(samplePos, tempo, std::nullopt);
  std::vector<std::uint32_t> barIncrementedAtPhase;
  std::vector<std::uint32_t> beatInBarAtBarStart;
  std::uint32_t lastBarIndex = state.barIndex;

  for (int beat = 0; beat < 64; ++beat) {
    const auto targetBeatSample = state.nextBeatSample;
    while (samplePos < targetBeatSample) {
      samplePos += kHopSize;
      const std::uint32_t phaseAboutToCross = static_cast<std::uint32_t>(state.beatIndex % 4);
      std::optional<Onset> onset;
      if (phaseAboutToCross == 2 && samplePos >= targetBeatSample) {
        onset = Onset{targetBeatSample, 5.0f};
      }
      state = clock.processHop(samplePos, tempo, onset);
      if (state.barIndex != lastBarIndex) {
        barIncrementedAtPhase.push_back(static_cast<std::uint32_t>((state.beatIndex - 1) % 4));
        beatInBarAtBarStart.push_back(state.beatInBar);
        lastBarIndex = state.barIndex;
      }
    }
  }
  // 8.12: a new bar starts on its first beat.
  for (std::size_t i = 2; i < beatInBarAtBarStart.size(); ++i) {
    CHECK(beatInBarAtBarStart[i] == 0);
  }

  REQUIRE_FALSE(barIncrementedAtPhase.empty());
  // Once the heuristic has converged (skip the first couple of bars, which
  // bootstrap with the default phase-0 guess), the bar boundary should land
  // exactly on phase 2's beat (downbeatPhase_ == 2), so the *previous* beat
  // -- recorded as (beatIndex - 1) % 4 at the moment barIndex changes -- is
  // consistently phase 1.
  for (std::size_t i = 2; i < barIncrementedAtPhase.size(); ++i) {
    CHECK(barIncrementedAtPhase[i] == 1);
  }
}
