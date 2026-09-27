// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/OnsetDetector.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::core {

OnsetDetector::OnsetDetector(double sampleRate, std::size_t hopSize, float windowSeconds,
                              float thresholdMultiplier, float minIntervalSeconds)
    : hopSize_(hopSize),
      windowSizeHops_(std::max<std::size_t>(
          1, static_cast<std::size_t>(windowSeconds * sampleRate / static_cast<double>(hopSize)))),
      thresholdMultiplier_(thresholdMultiplier),
      minIntervalSamples_(static_cast<std::uint64_t>(minIntervalSeconds * sampleRate)) {}

std::optional<Onset> OnsetDetector::processHop(float odfValue) {
  window_.push_back(odfValue);
  if (window_.size() > windowSizeHops_) {
    window_.pop_front();
  }

  std::optional<Onset> result;

  if (havePrevOdf_ && havePrevPrevOdf_) {
    const bool isLocalMax = prevOdf_ > prevPrevOdf_ && prevOdf_ >= odfValue;
    if (isLocalMax) {
      const double n = static_cast<double>(window_.size());
      double mean = 0.0;
      for (float v : window_) {
        mean += v;
      }
      mean /= n;

      double variance = 0.0;
      for (float v : window_) {
        const double d = static_cast<double>(v) - mean;
        variance += d * d;
      }
      variance /= n;
      const double stddev = std::sqrt(variance);

      const double threshold = mean + static_cast<double>(thresholdMultiplier_) * stddev;

      // The candidate peak is at hop (hopIndex_ - 1): hopIndex_ is the index
      // of the value we're about to process next, so the previous hop -- the
      // one holding prevOdf_ -- is hopIndex_ - 1.
      const std::uint64_t candidateHop = hopIndex_ - 1;
      const std::uint64_t candidateSamplePos = candidateHop * hopSize_;

      const bool pastRefractoryPeriod = !haveLastOnset_ ||
                                         (candidateSamplePos - lastOnsetSamplePos_) >= minIntervalSamples_;

      if (static_cast<double>(prevOdf_) > threshold && pastRefractoryPeriod) {
        result = Onset{candidateSamplePos, prevOdf_};
        lastOnsetSamplePos_ = candidateSamplePos;
        haveLastOnset_ = true;
      }
    }
  }

  prevPrevOdf_ = prevOdf_;
  havePrevPrevOdf_ = havePrevOdf_;
  prevOdf_ = odfValue;
  havePrevOdf_ = true;
  ++hopIndex_;

  return result;
}

} // namespace milkdawp::core
