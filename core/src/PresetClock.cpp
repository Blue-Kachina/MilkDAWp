// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/PresetClock.h"

#include <algorithm>

namespace milkdawp::core {

double PresetClock::advance(double dtSeconds, float speed) noexcept {
  const double dt = std::clamp(dtSeconds, 0.0, kMaxStepSeconds);
  double rate = std::max(static_cast<double>(speed), 0.0);
  if (blendRemaining_ > 0.0) {
    rate = std::max(rate, 1.0);
    blendRemaining_ = std::max(0.0, blendRemaining_ - dt);
  }
  time_ += dt * rate;
  return time_;
}

void PresetClock::onSoftCut(double blendSeconds) noexcept { blendRemaining_ = std::max(blendSeconds, 0.0); }

} // namespace milkdawp::core
