// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>

#include "milkdawp/core/BeatClock.h"
#include "milkdawp/core/Messages.h"

namespace milkdawp::core {

enum class TransitionMode { Manual, Timed, BeatQuantized, Hybrid, Energy };

struct TransitionSchedulerConfig {
  TransitionMode mode = TransitionMode::BeatQuantized;
  std::uint32_t bars = 4; // BeatQuantized/Hybrid/Energy-fallback granularity

  float timedDurationSeconds = 5.0f;
  bool jitterEnabled = false;
  float jitterMinSeconds = 3.0f;
  float jitterMaxSeconds = 15.0f;

  CutStyle cutStyle = CutStyle::Soft;
  float blendSeconds = 2.0f;

  // BeatQuantized: fall back to Timed behaviour when beat confidence stays
  // below this threshold for longer than the given number of seconds (§4.4).
  float beatConfidenceFallbackThreshold = 0.3f;
  float beatConfidenceLowSecondsBeforeFallback = 4.0f;

  // Energy mode: a hop counts as a "drop" when broadband energy exceeds the
  // rolling window's mean by this many standard deviations (a practical
  // stand-in for "a rolling percentile", cheaper to maintain incrementally)
  // and a strong bass onset lands on the same hop. Cooldown is in bars.
  float energyThresholdMultiplier = 2.0f;
  float energyWindowSeconds = 8.0f;
  std::uint32_t energyCooldownBars = 2;
};

/// TransitionRequest carries a presetId already (Messages.h); this pairs one
/// with the playlist index it came from, since Phase 1 has no interned
/// preset-id table yet (that's PresetLibrary, Phase 2.5).
struct ScheduledTransition {
  TransitionRequestMessage request;
  std::size_t playlistIndex = 0;
};

/// Pure state machine (§4.4): given the current BeatClock, band energy, and
/// transport state, decides when to advance the playlist and how to cut.
/// Ticked once per hop by the analysis thread; does not touch the playlist
/// or the render thread directly -- it emits ScheduledTransition and the
/// caller (engine, Phase 2) is responsible for acting on it (advancing a
/// Playlist, sending the TransitionRequestMessage to the render thread).
class TransitionScheduler {
public:
  explicit TransitionScheduler(double sampleRate, std::size_t hopSize = 512,
                                std::uint64_t rngSeed = std::mt19937::default_seed);

  void setConfig(const TransitionSchedulerConfig& config) { config_ = config; }
  [[nodiscard]] const TransitionSchedulerConfig& config() const noexcept { return config_; }

  /// Call once per hop, in hop order. `transportDiscontinuity` is true on
  /// exactly the hop a stop->play transition, loop, or relocate happened
  /// (§4.4/§2.9: the Timed clock pauses on stop and resets on loop/relocate).
  /// `nextPlaylistIndex`/`nextPresetId` are what a resulting transition
  /// should advance the playlist to -- Playlist itself decides *which*
  /// index that is (policy-dependent); the scheduler only decides *when*.
  std::optional<ScheduledTransition> tick(std::uint64_t currentSamplePos, bool transportPlaying,
                                           bool transportDiscontinuity, const BeatClockState& beatClock,
                                           float broadbandEnergy, bool strongBassOnsetThisHop,
                                           std::size_t nextPlaylistIndex, std::uint32_t nextPresetId);

private:
  [[nodiscard]] float pickTimedDurationSeconds();
  [[nodiscard]] std::optional<ScheduledTransition> tickTimed(std::uint64_t currentSamplePos,
                                                               bool transportPlaying,
                                                               bool transportDiscontinuity,
                                                               std::size_t nextPlaylistIndex,
                                                               std::uint32_t nextPresetId);
  [[nodiscard]] std::optional<ScheduledTransition> tickBeatQuantized(bool beatJustCrossed,
                                                                       std::uint64_t crossedBeatIndex,
                                                                       std::uint64_t crossedBeatSample,
                                                                       std::size_t nextPlaylistIndex,
                                                                       std::uint32_t nextPresetId);
  [[nodiscard]] std::optional<ScheduledTransition> tickHybrid(std::uint64_t currentSamplePos,
                                                                bool transportPlaying,
                                                                bool transportDiscontinuity,
                                                                bool beatJustCrossed,
                                                                std::uint64_t crossedBeatIndex,
                                                                std::uint64_t crossedBeatSample,
                                                                std::size_t nextPlaylistIndex,
                                                                std::uint32_t nextPresetId);
  [[nodiscard]] std::optional<ScheduledTransition> tickEnergy(std::uint64_t currentSamplePos,
                                                                const BeatClockState& beatClock,
                                                                bool beatJustCrossed,
                                                                std::uint64_t crossedBeatIndex,
                                                                std::uint64_t crossedBeatSample,
                                                                float broadbandEnergy,
                                                                bool strongBassOnsetThisHop,
                                                                std::size_t nextPlaylistIndex,
                                                                std::uint32_t nextPresetId);

  [[nodiscard]] std::uint64_t toSamples(float seconds) const noexcept;
  ScheduledTransition makeTransition(std::uint64_t dueAtSample, std::size_t nextPlaylistIndex,
                                      std::uint32_t nextPresetId, CutStyle cutStyleOverride) const;
  ScheduledTransition makeTransition(std::uint64_t dueAtSample, std::size_t nextPlaylistIndex,
                                      std::uint32_t nextPresetId) const {
    return makeTransition(dueAtSample, nextPlaylistIndex, nextPresetId, config_.cutStyle);
  }

  double sampleRate_;
  double hopDurationSeconds_;
  TransitionSchedulerConfig config_;
  std::mt19937 rng_;

  // Timed / Hybrid state.
  bool timedTargetSet_ = false;
  std::uint64_t timedTargetSample_ = 0;
  bool timedWasPlaying_ = true;
  std::uint64_t timedStoppedAtSample_ = 0;
  bool hybridWaitingForBar_ = false;

  // BeatQuantized state.
  bool beatQuantizedTargetSet_ = false;
  std::uint64_t beatQuantizedTargetBeatIndex_ = 0;
  float lowConfidenceSecondsAccumulated_ = 0.0f;

  // Energy state.
  std::deque<float> energyWindow_;
  bool haveLastEnergyTransitionSample_ = false;
  std::uint64_t lastEnergyTransitionSample_ = 0;

  // Beat-crossing detection shared by all modes (see the .cpp for why).
  bool haveLastBeat_ = false;
  std::uint64_t lastBeatIndex_ = 0;
  std::uint64_t lastNextBeatSample_ = 0;
};

} // namespace milkdawp::core
