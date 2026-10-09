// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/TransitionScheduler.h"

using namespace milkdawp::core;

namespace {
constexpr double kSampleRate = 48000.0;
constexpr std::size_t kHopSize = 512;

/// A synthetic, perfectly steady BeatClockState stream at `bpm`, one entry
/// per hop, for `totalHops` hops -- the "simulated clock" the roadmap asks
/// Phase 1.12 to be tested against, without needing the full
/// Analyzer/OnsetDetector/TempoTracker/BeatClock pipeline running.
std::vector<BeatClockState> simulateSteadyClock(float bpm, float confidence, std::size_t totalHops) {
  std::vector<BeatClockState> states(totalHops);
  const auto periodSamples = static_cast<std::uint64_t>(60.0 / bpm * kSampleRate);

  std::uint64_t samplePos = 0;
  std::uint64_t beatIndex = 0;
  std::uint64_t nextBeatSample = periodSamples;

  for (std::size_t hop = 0; hop < totalHops; ++hop) {
    samplePos = hop * kHopSize;
    while (samplePos >= nextBeatSample) {
      ++beatIndex;
      nextBeatSample += periodSamples;
    }
    states[hop] = BeatClockState{bpm,        nextBeatSample, beatIndex, static_cast<std::uint32_t>(beatIndex / 4),
                                  confidence, 4,              static_cast<std::uint32_t>(beatIndex % 4)};
  }
  return states;
}
} // namespace

TEST_CASE("TransitionScheduler Manual mode never emits", "[core][TransitionScheduler]") {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::Manual;
  scheduler.setConfig(config);

  auto clock = simulateSteadyClock(120.0f, 1.0f, 2000);
  for (std::size_t hop = 0; hop < clock.size(); ++hop) {
    auto result = scheduler.tick(hop * kHopSize, true, false, clock[hop], 0.0f, false, 0, 0);
    CHECK_FALSE(result.has_value());
  }
}

TEST_CASE("TransitionScheduler Timed mode fires after the configured duration",
          "[core][TransitionScheduler]") {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::Timed;
  config.timedDurationSeconds = 2.0f;
  scheduler.setConfig(config);

  const BeatClockState noBeat{}; // Timed mode ignores the beat clock entirely
  const std::size_t totalHops = static_cast<std::size_t>(3.0 * kSampleRate / kHopSize);

  int fireCount = 0;
  std::int64_t firstDueAt = -1;
  for (std::size_t hop = 0; hop < totalHops; ++hop) {
    auto result = scheduler.tick(hop * kHopSize, true, false, noBeat, 0.0f, false, 0, 0);
    if (result) {
      ++fireCount;
      if (firstDueAt < 0) {
        firstDueAt = result->request.dueAtSample;
      }
    }
  }

  REQUIRE(fireCount == 1);
  const auto expectedSample = static_cast<std::int64_t>(2.0 * kSampleRate);
  CHECK(std::abs(firstDueAt - expectedSample) < static_cast<std::int64_t>(kHopSize));
}

TEST_CASE("TransitionScheduler Timed mode pauses while transport is stopped",
          "[core][TransitionScheduler]") {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::Timed;
  config.timedDurationSeconds = 1.0f;
  scheduler.setConfig(config);

  const BeatClockState noBeat{};
  const auto oneSecondHops = static_cast<std::size_t>(1.0 * kSampleRate / kHopSize);

  // Run for half the duration, then "stop" for a long time, then resume.
  std::size_t hop = 0;
  for (; hop < oneSecondHops / 2; ++hop) {
    auto result = scheduler.tick(hop * kHopSize, true, false, noBeat, 0.0f, false, 0, 0);
    CHECK_FALSE(result.has_value());
  }

  const std::uint64_t stoppedSample = hop * kHopSize;
  for (int i = 0; i < 200; ++i) { // stay stopped for a while (paused samplePos held constant)
    auto result = scheduler.tick(stoppedSample, false, false, noBeat, 0.0f, false, 0, 0);
    CHECK_FALSE(result.has_value());
  }

  // Resume. It should still take roughly the *remaining* half-second, not a
  // fresh full second and not an immediate fire.
  bool fired = false;
  std::size_t hopsAfterResume = 0;
  for (std::size_t i = 0; i < oneSecondHops; ++i) {
    const auto samplePos = stoppedSample + i * kHopSize;
    auto result = scheduler.tick(samplePos, true, false, noBeat, 0.0f, false, 0, 0);
    ++hopsAfterResume;
    if (result) {
      fired = true;
      break;
    }
  }

  REQUIRE(fired);
  // Loose tolerance: two independent integer-hop truncations (the initial
  // half-duration split and the pause/resume shift) can each round by up to
  // a hop; this asserts "resumed with roughly the remaining time", not exact
  // sample-accuracy (Phase 2's render-thread execution is where sample
  // accuracy actually matters, per §4.4).
  const auto expectedHopsAfterResume = (oneSecondHops / 2);
  CHECK(hopsAfterResume <= expectedHopsAfterResume + 4);
  CHECK(hopsAfterResume >= expectedHopsAfterResume - 4);
}

TEST_CASE("TransitionScheduler BeatQuantized fires exactly every N bars on the predicted downbeat",
          "[core][TransitionScheduler]") {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::BeatQuantized;
  config.bars = 2;
  scheduler.setConfig(config);

  auto clock = simulateSteadyClock(120.0f, 1.0f, 4000);

  std::vector<std::uint64_t> firedAtBeat;
  std::uint64_t lastSeenBeat = 0;
  for (std::size_t hop = 0; hop < clock.size(); ++hop) {
    auto result = scheduler.tick(hop * kHopSize, true, false, clock[hop], 0.0f, false, 0, 0);
    lastSeenBeat = clock[hop].beatIndex;
    if (result) {
      firedAtBeat.push_back(lastSeenBeat);
    }
  }

  REQUIRE(firedAtBeat.size() >= 3);
  // 2 bars == 8 beats apart, every time.
  for (std::size_t i = 1; i < firedAtBeat.size(); ++i) {
    CHECK(firedAtBeat[i] - firedAtBeat[i - 1] == 8);
  }
}

namespace {
// Beat indices at which a scheduler fires over `clock`, starting at hop `firstHop`.
std::vector<std::uint64_t> firedBeats(const TransitionSchedulerConfig& config,
                                      const std::vector<BeatClockState>& clock, std::size_t firstHop = 0) {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  scheduler.setConfig(config);
  std::vector<std::uint64_t> fired;
  for (std::size_t hop = firstHop; hop < clock.size(); ++hop) {
    if (scheduler.tick(hop * kHopSize, true, false, clock[hop], 0.0f, false, 0, 0)) {
      fired.push_back(clock[hop].beatIndex);
    }
  }
  return fired;
}
} // namespace

TEST_CASE("TransitionScheduler grid-anchored BeatQuantized cuts only on its offset within the bar grid",
          "[core][TransitionScheduler][layers]") {
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::BeatQuantized;
  config.bars = 2; // an 8-beat cycle
  config.gridAnchored = true;
  config.gridOffsetBeats = 3;
  const auto clock = simulateSteadyClock(120.0f, 1.0f, 4000);

  const auto fired = firedBeats(config, clock);
  REQUIRE(fired.size() >= 3);
  for (const auto beat : fired) {
    CHECK(beat % 8 == 3);
  }
  for (std::size_t i = 1; i < fired.size(); ++i) {
    CHECK(fired[i] - fired[i - 1] == 8);
  }
}

TEST_CASE("TransitionScheduler grid-anchored instances that started at different times cut on the same beats",
          "[core][TransitionScheduler][layers]") {
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::BeatQuantized;
  config.bars = 1;
  config.gridAnchored = true;
  const auto clock = simulateSteadyClock(120.0f, 1.0f, 6000);

  const auto early = firedBeats(config, clock, 0);
  const auto late = firedBeats(config, clock, 700); // joined the project later, mid-bar
  REQUIRE(late.size() >= 3);
  // Whatever the late one fires, the early one fired on the very same beat.
  for (const auto beat : late) {
    CHECK(std::find(early.begin(), early.end(), beat) != early.end());
  }

  // Un-anchored, the late starter would count its own 4 beats from its own start:
  // the two would be out of step. This is what the grid fixes.
  config.gridAnchored = false;
  const auto earlyFree = firedBeats(config, clock, 0);
  const auto lateFree = firedBeats(config, clock, 700);
  REQUIRE(lateFree.size() >= 2);
  CHECK(std::find(earlyFree.begin(), earlyFree.end(), lateFree.front()) == earlyFree.end());
}

TEST_CASE("TransitionScheduler grid offsets stagger instances, and wrap around the cycle",
          "[core][TransitionScheduler][layers]") {
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::BeatQuantized;
  config.bars = 1; // a 4-beat cycle
  config.gridAnchored = true;
  const auto clock = simulateSteadyClock(120.0f, 1.0f, 4000);

  config.gridOffsetBeats = 0;
  const auto onOne = firedBeats(config, clock);
  config.gridOffsetBeats = 2;
  const auto onThree = firedBeats(config, clock);
  config.gridOffsetBeats = 6; // 6 % 4 == 2: the same as 2
  const auto wrapped = firedBeats(config, clock);

  REQUIRE(onOne.size() >= 3);
  for (const auto beat : onOne) {
    CHECK(beat % 4 == 0);
  }
  for (const auto beat : onThree) {
    CHECK(beat % 4 == 2);
  }
  CHECK(wrapped == onThree);
}

TEST_CASE("TransitionScheduler BeatQuantized falls back to Timed after sustained low confidence",
          "[core][TransitionScheduler]") {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::BeatQuantized;
  config.bars = 1;
  config.timedDurationSeconds = 3.0f;
  config.beatConfidenceFallbackThreshold = 0.3f;
  config.beatConfidenceLowSecondsBeforeFallback = 1.0f;
  scheduler.setConfig(config);

  // Low confidence throughout -- BeatQuantized should never get a chance to
  // establish a target beat-aligned cycle; Timed should take over instead
  // once low confidence has persisted long enough.
  auto clock = simulateSteadyClock(120.0f, 0.05f, 4000);

  bool fired = false;
  for (std::size_t hop = 0; hop < clock.size(); ++hop) {
    auto result = scheduler.tick(hop * kHopSize, true, false, clock[hop], 0.0f, false, 0, 0);
    if (result) {
      fired = true;
      break;
    }
  }

  CHECK(fired);
}

TEST_CASE("TransitionScheduler Hybrid mode snaps the timed target forward to the next bar",
          "[core][TransitionScheduler]") {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::Hybrid;
  config.timedDurationSeconds = 1.0f; // lands mid-bar at 120 bpm (2 beats/sec -> beat 2, not a bar line)
  scheduler.setConfig(config);

  auto clock = simulateSteadyClock(120.0f, 1.0f, 4000);

  std::optional<ScheduledTransition> fired;
  std::uint64_t firedAtBeat = 0;
  for (std::size_t hop = 0; hop < clock.size() && !fired; ++hop) {
    fired = scheduler.tick(hop * kHopSize, true, false, clock[hop], 0.0f, false, 0, 0);
    if (fired) {
      firedAtBeat = clock[hop].beatIndex;
    }
  }

  REQUIRE(fired.has_value());
  // Must land on a bar boundary (multiple of 4 beats), not at the raw
  // 1-second timed target (which falls mid-bar at 120 bpm).
  CHECK(firedAtBeat % 4 == 0);
}

namespace {

/// Energy mode's input: a kick on every beat of the simulated clock while a
/// section is loud (bass envelope 1.0, falling back with the Analyzer's
/// 200 ms release), and only a pad's faint bass while it is quiet.
struct EnergySection {
  double seconds;
  bool loud;
};

struct EnergyRun {
  std::vector<ScheduledTransition> cuts;
  std::vector<std::size_t> cutHops;
  std::vector<std::size_t> sectionStartHops;
};

EnergyRun runEnergy(TransitionScheduler& scheduler, const std::vector<EnergySection>& sections) {
  const double hopSeconds = static_cast<double>(kHopSize) / kSampleRate;
  std::size_t totalHops = 0;
  EnergyRun run;
  for (const auto& section : sections) {
    run.sectionStartHops.push_back(totalHops);
    totalHops += static_cast<std::size_t>(section.seconds / hopSeconds);
  }
  const auto clock = simulateSteadyClock(120.0f, 1.0f, totalHops);

  float envelope = 0.0f;
  std::size_t section = 0;
  for (std::size_t hop = 0; hop < totalHops; ++hop) {
    while (section + 1 < run.sectionStartHops.size() && hop >= run.sectionStartHops[section + 1]) {
      ++section;
    }
    const bool beat = hop > 0 && clock[hop].beatIndex != clock[hop - 1].beatIndex;
    const bool kick = beat && sections[section].loud;
    envelope = kick ? 1.0f : static_cast<float>(envelope * std::exp(-hopSeconds / 0.2));
    const float bass = std::max(envelope, 0.001f) * 1.0e5f; // a full-scale kick's bassEnergy
    if (auto cut = scheduler.tick(hop * kHopSize, true, false, clock[hop], bass, kick, 0, 0)) {
      run.cuts.push_back(*cut);
      run.cutHops.push_back(hop);
    }
  }
  return run;
}

int hardCuts(const EnergyRun& run) {
  return static_cast<int>(std::count_if(run.cuts.begin(), run.cuts.end(),
                                        [](const auto& cut) { return cut.request.cutStyle == CutStyle::Hard; }));
}

} // namespace

TEST_CASE("TransitionScheduler Energy mode never hard-cuts on steady kicks", "[core][TransitionScheduler]") {
  // 5.1's regression: the old mean + 2 sd rule took nearly every kick of a
  // steady section for a drop and cut every `energyCooldownBars`.
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::Energy;
  config.bars = 16;
  scheduler.setConfig(config);

  const auto run = runEnergy(scheduler, {{70.0, true}});
  CHECK(hardCuts(run) == 0);
  CHECK(run.cuts.size() == 2); // the beat-quantized ones, every 16 bars (32 s)
}

TEST_CASE("TransitionScheduler Energy mode hard-cuts on the drop after a breakdown",
          "[core][TransitionScheduler]") {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::Energy;
  config.bars = 4;
  config.cutStyle = CutStyle::Soft;
  scheduler.setConfig(config);

  const auto run = runEnergy(scheduler, {{16.0, true}, {8.0, false}, {20.0, true}});
  const auto dropHop = run.sectionStartHops[2];
  REQUIRE(hardCuts(run) == 1);

  const auto hard = std::find_if(run.cuts.begin(), run.cuts.end(),
                                 [](const auto& cut) { return cut.request.cutStyle == CutStyle::Hard; });
  const auto hardIndex = static_cast<std::size_t>(hard - run.cuts.begin());
  // On the drop's first kick (the first beat crossing of the section).
  CHECK(run.cutHops[hardIndex] >= dropHop);
  CHECK(run.cutHops[hardIndex] <= dropHop + 50);

  // The beat-quantized count starts again from the drop: the next cut is
  // 4 bars (8 s at 120 bpm) after it, not wherever the old count was.
  REQUIRE(hardIndex + 1 < run.cuts.size());
  const auto gapSeconds = static_cast<double>(run.cuts[hardIndex + 1].request.dueAtSample -
                                              run.cuts[hardIndex].request.dueAtSample) /
                          kSampleRate;
  CHECK(gapSeconds > 7.9);
  CHECK(gapSeconds < 8.1);
}

TEST_CASE("TransitionScheduler Energy mode's cooldown suppresses a second drop", "[core][TransitionScheduler]") {
  // Two breakdown/drop pairs 6 s apart.
  const std::vector<EnergySection> track{{16.0, true}, {4.0, false}, {2.0, true}, {4.0, false}, {10.0, true}};

  TransitionSchedulerConfig config;
  config.mode = TransitionMode::Energy;
  config.bars = 16;

  SECTION("a long cooldown keeps one") {
    TransitionScheduler scheduler(kSampleRate, kHopSize);
    config.energyCooldownBars = 8; // 16 s
    scheduler.setConfig(config);
    CHECK(hardCuts(runEnergy(scheduler, track)) == 1);
  }
  SECTION("a short one lets both through") {
    TransitionScheduler scheduler(kSampleRate, kHopSize);
    config.energyCooldownBars = 1; // 2 s
    scheduler.setConfig(config);
    CHECK(hardCuts(runEnergy(scheduler, track)) == 2);
  }
}

TEST_CASE("TransitionScheduler Energy mode behaves like BeatQuantized when there is no drop",
          "[core][TransitionScheduler]") {
  TransitionScheduler scheduler(kSampleRate, kHopSize);
  TransitionSchedulerConfig config;
  config.mode = TransitionMode::Energy;
  config.bars = 1;
  scheduler.setConfig(config);

  auto clock = simulateSteadyClock(120.0f, 1.0f, 4000);

  int fireCount = 0;
  for (std::size_t hop = 0; hop < clock.size(); ++hop) {
    // Perfectly flat energy: never a "drop" (zero variance -> nothing ever
    // exceeds mean + k*stddev), so this should fall through to
    // BeatQuantized's every-1-bar scheduling instead.
    auto result = scheduler.tick(hop * kHopSize, true, false, clock[hop], 1.0f, false, 0, 0);
    if (result) {
      ++fireCount;
      CHECK(result->request.cutStyle != CutStyle::Hard);
    }
  }

  CHECK(fireCount > 5);
}
