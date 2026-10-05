// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/LayerGate.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::core {

float LayerGate::process(float peakDb, float dtSeconds, const LayerGateSettings& settings) noexcept {
  dtSeconds = std::max(dtSeconds, 0.0f);
  if (!settings.enabled) {
    reset();
    return envelope_;
  }
  if (open_) {
    if (peakDb < settings.thresholdDb - kHysteresisDb) {
      holdRemaining_ -= dtSeconds;
      if (holdRemaining_ <= 0.0f) {
        open_ = false;
      }
    } else {
      holdRemaining_ = kHoldSeconds;
    }
  } else if (peakDb >= settings.thresholdDb) {
    open_ = true;
    holdRemaining_ = kHoldSeconds;
  }

  if (open_) {
    envelope_ = 1.0f; // instant: the hit lands on time
  } else if (settings.releaseMs <= 0.0f) {
    envelope_ = 0.0f;
  } else {
    envelope_ = std::max(0.0f, envelope_ - dtSeconds * 1000.0f / settings.releaseMs);
  }
  return envelope_;
}

void LayerGate::reset() noexcept {
  open_ = true;
  holdRemaining_ = kHoldSeconds;
  envelope_ = 1.0f;
}

float peakDbfs(const float* samples, std::size_t count) noexcept {
  float peak = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    peak = std::max(peak, std::abs(samples[i]));
  }
  return peak > 1.0e-10f ? 20.0f * std::log10(peak) : -200.0f;
}

} // namespace milkdawp::core
