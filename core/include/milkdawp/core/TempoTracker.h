// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <deque>

namespace milkdawp::core {

/// A tempo hypothesis with confidence (Phase 1.5, §4.3 step 5).
struct TempoEstimate {
  float bpm = 0.0f;
  float confidence = 0.0f; // 0 (no periodicity found) .. 1 (strong, stable)
};

/// Autocorrelation-based tempo estimator over a rolling window of the onset
/// detection function (§4.3 step 5). Restricted to 60-200 BPM with
/// octave-error weighting toward 90-150 BPM (the range most material falls
/// in; autocorrelation is otherwise ambiguous between a tempo and its
/// double/half). The reported period is smoothed (not the raw per-hop
/// estimate) so BPM does not flicker hop to hop.
class TempoTracker {
public:
  explicit TempoTracker(double sampleRate, std::size_t hopSize = 512, float windowSeconds = 7.0f,
                         float minBpm = 60.0f, float maxBpm = 200.0f);

  /// Feed the ODF value for the next hop (typically
  /// AnalysisFrame::onsetStrength or ::bassOnsetStrength). Returns the
  /// current smoothed estimate.
  TempoEstimate processHop(float odfValue);

private:
  [[nodiscard]] static float octaveWeight(float bpm);

  double hopDurationSeconds_;
  std::size_t windowSizeHops_;
  std::size_t minLagHops_;
  std::size_t maxLagHops_;

  std::deque<float> window_;

  bool haveSmoothedPeriod_ = false;
  float smoothedPeriodHops_ = 0.0f;
  float lastConfidence_ = 0.0f;
};

} // namespace milkdawp::core
