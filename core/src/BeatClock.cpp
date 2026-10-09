// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/BeatClock.h"

#include <cmath>

namespace milkdawp::core {

BeatClock::BeatClock(double sampleRate, std::size_t /*hopSize*/, float phaseCorrectionGain,
                      float phaseCorrectionWindowFraction)
    : sampleRate_(sampleRate), phaseCorrectionGain_(phaseCorrectionGain),
      phaseCorrectionWindowFraction_(phaseCorrectionWindowFraction) {}

BeatClockState BeatClock::processHop(std::uint64_t currentSamplePos, const TempoEstimate& tempo,
                                      const std::optional<Onset>& bassOnset) {
  if (tempo.bpm > 0.0f) {
    const double newPeriod = 60.0 / static_cast<double>(tempo.bpm) * sampleRate_;
    if (!established_) {
      periodSamples_ = newPeriod;
      nextBeatSample_ = currentSamplePos + static_cast<std::uint64_t>(periodSamples_);
      beatIndex_ = 0;
      established_ = true;
    } else {
      periodSamples_ = newPeriod;
    }
  }

  if (established_ && bassOnset.has_value()) {
    const auto phase = static_cast<std::uint32_t>(beatIndex_ % 4);
    ++phaseObservationCount_[phase];
    phaseEnergyAverage_[phase] += (bassOnset->strength - phaseEnergyAverage_[phase]) /
                                   static_cast<float>(phaseObservationCount_[phase]);

    // PLL-style phase correction: nudge the prediction toward an onset that
    // lands close to it, rather than snapping outright (§4.3 step 6).
    //
    // The onset's raw distance to nextBeatSample_ can be almost a full
    // period even when the onset sits right on the beat grid -- e.g. right
    // after establishing the very first (essentially arbitrary) phase
    // anchor. Wrap the delta to the nearest equivalent phase (mod period)
    // first, or a real on-grid onset outside the *immediate* next beat's
    // correction window would never pull the phase in at all.
    const double window = periodSamples_ * static_cast<double>(phaseCorrectionWindowFraction_);
    double delta = static_cast<double>(bassOnset->samplePos) - static_cast<double>(nextBeatSample_);
    if (periodSamples_ > 0.0) {
      delta = std::fmod(delta, periodSamples_);
      if (delta > periodSamples_ / 2.0) {
        delta -= periodSamples_;
      } else if (delta < -periodSamples_ / 2.0) {
        delta += periodSamples_;
      }
    }
    if (std::abs(delta) <= window) {
      const double corrected =
          static_cast<double>(nextBeatSample_) + static_cast<double>(phaseCorrectionGain_) * delta;
      nextBeatSample_ = corrected > 0.0 ? static_cast<std::uint64_t>(corrected) : 0;
    }
  }

  while (established_ && currentSamplePos >= nextBeatSample_) {
    ++beatIndex_;
    nextBeatSample_ += static_cast<std::uint64_t>(periodSamples_);

    if (beatIndex_ % 4 == 0) {
      ++barCount_;
      std::uint32_t best = 0;
      for (std::uint32_t p = 1; p < 4; ++p) {
        if (phaseEnergyAverage_[p] > phaseEnergyAverage_[best]) {
          best = p;
        }
      }
      downbeatPhase_ = best;
    }
  }

  BeatClockState state;
  state.bpm = tempo.bpm;
  state.nextBeatSample = nextBeatSample_;
  state.beatIndex = beatIndex_;
  state.barIndex = (established_ && beatIndex_ >= downbeatPhase_)
                        ? static_cast<std::uint32_t>((beatIndex_ - downbeatPhase_) / 4)
                        : 0;
  state.confidence = tempo.confidence;
  state.beatInBar = static_cast<std::uint32_t>((beatIndex_ + 4 - downbeatPhase_ % 4) % 4);
  return state;
}

std::uint64_t BeatClock::samplesUntilNextBeat(std::uint64_t currentSamplePos) const noexcept {
  return nextBeatSample_ > currentSamplePos ? nextBeatSample_ - currentSamplePos : 0;
}

} // namespace milkdawp::core
