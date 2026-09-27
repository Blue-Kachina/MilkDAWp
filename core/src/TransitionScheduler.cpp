// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/TransitionScheduler.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::core {

TransitionScheduler::TransitionScheduler(double sampleRate, std::size_t hopSize, std::uint64_t rngSeed)
    : sampleRate_(sampleRate),
      hopDurationSeconds_(static_cast<double>(hopSize) / sampleRate),
      rng_(static_cast<std::mt19937::result_type>(rngSeed)) {}

std::uint64_t TransitionScheduler::toSamples(float seconds) const noexcept {
  return static_cast<std::uint64_t>(static_cast<double>(seconds) * sampleRate_);
}

ScheduledTransition TransitionScheduler::makeTransition(std::uint64_t dueAtSample,
                                                          std::size_t nextPlaylistIndex,
                                                          std::uint32_t nextPresetId,
                                                          CutStyle cutStyleOverride) const {
  ScheduledTransition st;
  st.request.presetId = nextPresetId;
  st.request.cutStyle = cutStyleOverride;
  st.request.blendSeconds = config_.blendSeconds;
  st.request.dueAtSample = static_cast<std::int64_t>(dueAtSample);
  st.playlistIndex = nextPlaylistIndex;
  return st;
}

float TransitionScheduler::pickTimedDurationSeconds() {
  if (!config_.jitterEnabled) {
    return config_.timedDurationSeconds;
  }
  std::uniform_real_distribution<float> dist(config_.jitterMinSeconds, config_.jitterMaxSeconds);
  return dist(rng_);
}

std::optional<ScheduledTransition> TransitionScheduler::tickTimed(std::uint64_t currentSamplePos,
                                                                    bool transportPlaying,
                                                                    bool transportDiscontinuity,
                                                                    std::size_t nextPlaylistIndex,
                                                                    std::uint32_t nextPresetId) {
  (void)transportDiscontinuity; // handled centrally in tick() via timedTargetSet_ reset

  if (!transportPlaying) {
    if (timedWasPlaying_) {
      timedStoppedAtSample_ = currentSamplePos;
      timedWasPlaying_ = false;
    }
    return std::nullopt;
  }

  if (!timedWasPlaying_ && timedTargetSet_) {
    // Resuming: shift the target forward by the paused duration so the
    // remaining time-until-transition survives the pause (§2.9/§4.4).
    timedTargetSample_ += (currentSamplePos - timedStoppedAtSample_);
  }
  timedWasPlaying_ = true;

  if (!timedTargetSet_) {
    timedTargetSample_ = currentSamplePos + toSamples(pickTimedDurationSeconds());
    timedTargetSet_ = true;
    return std::nullopt;
  }

  if (currentSamplePos >= timedTargetSample_) {
    auto transition = makeTransition(timedTargetSample_, nextPlaylistIndex, nextPresetId);
    timedTargetSample_ = currentSamplePos + toSamples(pickTimedDurationSeconds());
    return transition;
  }
  return std::nullopt;
}

std::optional<ScheduledTransition> TransitionScheduler::tickBeatQuantized(
    bool beatJustCrossed, std::uint64_t crossedBeatIndex, std::uint64_t crossedBeatSample,
    std::size_t nextPlaylistIndex, std::uint32_t nextPresetId) {
  const std::uint64_t beatsPerCycle = static_cast<std::uint64_t>(config_.bars) * 4;

  if (!beatQuantizedTargetSet_) {
    if (!beatJustCrossed) {
      return std::nullopt;
    }
    beatQuantizedTargetBeatIndex_ = crossedBeatIndex + beatsPerCycle;
    beatQuantizedTargetSet_ = true;
    return std::nullopt;
  }

  if (beatJustCrossed && crossedBeatIndex >= beatQuantizedTargetBeatIndex_) {
    auto transition = makeTransition(crossedBeatSample, nextPlaylistIndex, nextPresetId);
    beatQuantizedTargetBeatIndex_ = crossedBeatIndex + beatsPerCycle;
    return transition;
  }
  return std::nullopt;
}

std::optional<ScheduledTransition> TransitionScheduler::tickHybrid(
    std::uint64_t currentSamplePos, bool transportPlaying, bool transportDiscontinuity,
    bool beatJustCrossed, std::uint64_t crossedBeatIndex, std::uint64_t crossedBeatSample,
    std::size_t nextPlaylistIndex, std::uint32_t nextPresetId) {
  (void)transportDiscontinuity;

  if (!transportPlaying) {
    if (timedWasPlaying_) {
      timedStoppedAtSample_ = currentSamplePos;
      timedWasPlaying_ = false;
    }
    return std::nullopt;
  }

  if (!timedWasPlaying_ && timedTargetSet_) {
    timedTargetSample_ += (currentSamplePos - timedStoppedAtSample_);
  }
  timedWasPlaying_ = true;

  if (!timedTargetSet_) {
    timedTargetSample_ = currentSamplePos + toSamples(pickTimedDurationSeconds());
    timedTargetSet_ = true;
    hybridWaitingForBar_ = false;
    return std::nullopt;
  }

  if (!hybridWaitingForBar_ && currentSamplePos >= timedTargetSample_) {
    hybridWaitingForBar_ = true;
  }

  // "Snapped forward to the next bar boundary" (§4.4): a plain 4-beats-per-bar
  // reading, independent of BeatClock's best-effort downbeat phase, since the
  // Hybrid mode's whole point is a simple, predictable snap.
  if (hybridWaitingForBar_ && beatJustCrossed && crossedBeatIndex % 4 == 0) {
    auto transition = makeTransition(crossedBeatSample, nextPlaylistIndex, nextPresetId);
    timedTargetSample_ = crossedBeatSample + toSamples(pickTimedDurationSeconds());
    hybridWaitingForBar_ = false;
    return transition;
  }
  return std::nullopt;
}

std::optional<ScheduledTransition> TransitionScheduler::tickEnergy(
    std::uint64_t currentSamplePos, const BeatClockState& beatClock, bool beatJustCrossed,
    std::uint64_t crossedBeatIndex, std::uint64_t crossedBeatSample, float broadbandEnergy,
    bool strongBassOnsetThisHop, std::size_t nextPlaylistIndex, std::uint32_t nextPresetId) {
  const auto windowSizeHops =
      std::max<std::size_t>(4, static_cast<std::size_t>(config_.energyWindowSeconds / hopDurationSeconds_));
  energyWindow_.push_back(broadbandEnergy);
  while (energyWindow_.size() > windowSizeHops) {
    energyWindow_.pop_front();
  }

  bool isDrop = false;
  if (strongBassOnsetThisHop && energyWindow_.size() >= 4) {
    double mean = 0.0;
    for (float v : energyWindow_) {
      mean += v;
    }
    mean /= static_cast<double>(energyWindow_.size());

    double variance = 0.0;
    for (float v : energyWindow_) {
      const double d = static_cast<double>(v) - mean;
      variance += d * d;
    }
    variance /= static_cast<double>(energyWindow_.size());
    const double stddev = std::sqrt(variance);

    const double threshold = mean + static_cast<double>(config_.energyThresholdMultiplier) * stddev;
    isDrop = static_cast<double>(broadbandEnergy) > threshold;
  }

  const double bpmForCooldown = beatClock.bpm > 0.0f ? static_cast<double>(beatClock.bpm) : 120.0;
  const double secondsPerBar = (60.0 / bpmForCooldown) * 4.0;
  const auto cooldownSamples =
      static_cast<std::uint64_t>(secondsPerBar * static_cast<double>(config_.energyCooldownBars) * sampleRate_);
  const bool cooldownElapsed = !haveLastEnergyTransitionSample_ ||
                                (currentSamplePos - lastEnergyTransitionSample_) >= cooldownSamples;

  if (isDrop && cooldownElapsed) {
    haveLastEnergyTransitionSample_ = true;
    lastEnergyTransitionSample_ = currentSamplePos;
    return makeTransition(currentSamplePos, nextPlaylistIndex, nextPresetId, CutStyle::Hard);
  }

  return tickBeatQuantized(beatJustCrossed, crossedBeatIndex, crossedBeatSample, nextPlaylistIndex,
                            nextPresetId);
}

std::optional<ScheduledTransition> TransitionScheduler::tick(
    std::uint64_t currentSamplePos, bool transportPlaying, bool transportDiscontinuity,
    const BeatClockState& beatClock, float broadbandEnergy, bool strongBassOnsetThisHop,
    std::size_t nextPlaylistIndex, std::uint32_t nextPresetId) {
  if (transportDiscontinuity) {
    timedTargetSet_ = false;
    beatQuantizedTargetSet_ = false;
    hybridWaitingForBar_ = false;
  }

  bool beatJustCrossed = false;
  std::uint64_t crossedBeatIndex = 0;
  std::uint64_t crossedBeatSample = 0;

  if (haveLastBeat_ && beatClock.beatIndex > lastBeatIndex_) {
    beatJustCrossed = true;
    if (beatClock.beatIndex == lastBeatIndex_ + 1) {
      crossedBeatIndex = beatClock.beatIndex;
      crossedBeatSample = lastNextBeatSample_;
    } else {
      // Multiple beats advanced in one hop (e.g. a host relocate jump):
      // approximate with the current position rather than a stale prediction.
      crossedBeatIndex = beatClock.beatIndex;
      crossedBeatSample = currentSamplePos;
    }
  }

  std::optional<ScheduledTransition> result;

  switch (config_.mode) {
  case TransitionMode::Manual:
    break;

  case TransitionMode::Timed:
    result = tickTimed(currentSamplePos, transportPlaying, transportDiscontinuity, nextPlaylistIndex,
                        nextPresetId);
    break;

  case TransitionMode::BeatQuantized: {
    if (beatClock.confidence < config_.beatConfidenceFallbackThreshold) {
      lowConfidenceSecondsAccumulated_ += static_cast<float>(hopDurationSeconds_);
    } else {
      lowConfidenceSecondsAccumulated_ = 0.0f;
    }

    if (lowConfidenceSecondsAccumulated_ >= config_.beatConfidenceLowSecondsBeforeFallback) {
      result = tickTimed(currentSamplePos, transportPlaying, transportDiscontinuity, nextPlaylistIndex,
                          nextPresetId);
    } else {
      result = tickBeatQuantized(beatJustCrossed, crossedBeatIndex, crossedBeatSample, nextPlaylistIndex,
                                  nextPresetId);
    }
    break;
  }

  case TransitionMode::Hybrid:
    result = tickHybrid(currentSamplePos, transportPlaying, transportDiscontinuity, beatJustCrossed,
                         crossedBeatIndex, crossedBeatSample, nextPlaylistIndex, nextPresetId);
    break;

  case TransitionMode::Energy:
    result = tickEnergy(currentSamplePos, beatClock, beatJustCrossed, crossedBeatIndex, crossedBeatSample,
                         broadbandEnergy, strongBassOnsetThisHop, nextPlaylistIndex, nextPresetId);
    break;
  }

  haveLastBeat_ = true;
  lastBeatIndex_ = beatClock.beatIndex;
  lastNextBeatSample_ = beatClock.nextBeatSample;

  return result;
}

} // namespace milkdawp::core
