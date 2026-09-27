// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/TempoTracker.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace milkdawp::core {

namespace {
// Smoothing time constant for the tracked period (not the raw per-hop
// estimate) so BPM does not flicker hop to hop (§4.3 step 5).
constexpr float kPeriodSmoothingSeconds = 1.5f;

// Empirical scale mapping a normalized-autocorrelation peak to a 0..1
// confidence. Real musical ODFs rarely exceed ~0.3-0.5 normalized
// correlation even for a very steady beat (unlike a pure click train, which
// approaches 1.0); this keeps confidence usefully spread across that range
// rather than saturating at 1.0 for anything above a tiny threshold.
constexpr float kConfidenceScale = 3.0f;

// Octave-error weighting toward 90-150 BPM (§4.3 step 5): autocorrelation
// alone can't distinguish a tempo from its double or half, so bias toward
// the range most material actually falls in.
constexpr float kOctaveWeightCenterBpm = 120.0f;
constexpr float kOctaveWeightSigmaOctaves = 0.5f;
} // namespace

TempoTracker::TempoTracker(double sampleRate, std::size_t hopSize, float windowSeconds, float minBpm,
                            float maxBpm)
    : hopDurationSeconds_(static_cast<double>(hopSize) / sampleRate),
      windowSizeHops_(static_cast<std::size_t>(windowSeconds / hopDurationSeconds_)),
      minLagHops_(std::max<std::size_t>(1, static_cast<std::size_t>(
                                                std::lround((60.0 / maxBpm) / hopDurationSeconds_)))),
      maxLagHops_(static_cast<std::size_t>(std::lround((60.0 / minBpm) / hopDurationSeconds_))) {}

float TempoTracker::octaveWeight(float bpm) {
  const float octavesFromCenter = std::log2(bpm / kOctaveWeightCenterBpm);
  const float z = octavesFromCenter / kOctaveWeightSigmaOctaves;
  return std::exp(-0.5f * z * z);
}

TempoEstimate TempoTracker::processHop(float odfValue) {
  window_.push_back(odfValue);
  if (window_.size() > windowSizeHops_) {
    window_.pop_front();
  }

  // Need enough history to evaluate the longest lag we care about at least
  // roughly twice over, or the autocorrelation estimate is unreliable.
  if (window_.size() < maxLagHops_ * 2) {
    return TempoEstimate{haveSmoothedPeriod_
                              ? static_cast<float>(60.0 / (static_cast<double>(smoothedPeriodHops_) *
                                                            hopDurationSeconds_))
                              : 0.0f,
                          0.0f};
  }

  const std::size_t n = window_.size();
  std::vector<float> x(n);
  double mean = 0.0;
  for (float v : window_) {
    mean += v;
  }
  mean /= static_cast<double>(n);
  {
    std::size_t i = 0;
    for (float v : window_) {
      x[i++] = static_cast<float>(static_cast<double>(v) - mean);
    }
  }

  double energy = 0.0;
  for (float v : x) {
    energy += static_cast<double>(v) * static_cast<double>(v);
  }

  if (energy <= 1e-12) {
    // Silence or a perfectly flat signal: no periodicity to find.
    return TempoEstimate{haveSmoothedPeriod_
                              ? static_cast<float>(60.0 / (static_cast<double>(smoothedPeriodHops_) *
                                                            hopDurationSeconds_))
                              : 0.0f,
                          0.0f};
  }

  std::size_t bestLag = minLagHops_;
  double bestWeightedScore = -1.0;
  double bestRawCorrelation = 0.0;

  for (std::size_t lag = minLagHops_; lag <= maxLagHops_ && lag < n; ++lag) {
    double correlation = 0.0;
    const std::size_t count = n - lag;
    for (std::size_t i = 0; i < count; ++i) {
      correlation += static_cast<double>(x[i]) * static_cast<double>(x[i + lag]);
    }
    const double normalized = correlation / energy;

    const double bpmForLag = 60.0 / (static_cast<double>(lag) * hopDurationSeconds_);
    const double weighted = normalized * static_cast<double>(octaveWeight(static_cast<float>(bpmForLag)));

    if (weighted > bestWeightedScore) {
      bestWeightedScore = weighted;
      bestRawCorrelation = normalized;
      bestLag = lag;
    }
  }

  if (!haveSmoothedPeriod_) {
    smoothedPeriodHops_ = static_cast<float>(bestLag);
    haveSmoothedPeriod_ = true;
  } else {
    const float alpha = std::exp(-static_cast<float>(hopDurationSeconds_) / kPeriodSmoothingSeconds);
    smoothedPeriodHops_ = alpha * smoothedPeriodHops_ + (1.0f - alpha) * static_cast<float>(bestLag);
  }

  const float bpm = static_cast<float>(60.0 / (static_cast<double>(smoothedPeriodHops_) * hopDurationSeconds_));
  const float confidence = std::clamp(static_cast<float>(bestRawCorrelation) * kConfidenceScale, 0.0f, 1.0f);
  lastConfidence_ = confidence;

  return TempoEstimate{bpm, confidence};
}

} // namespace milkdawp::core
