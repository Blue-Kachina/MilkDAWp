// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/AdaptiveQuality.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::core {

AdaptiveQuality::AdaptiveQuality(AdaptiveQualityConfig config) : config_(config) {}

void AdaptiveQuality::setTargetFps(float fps) noexcept {
  config_.targetFps = std::max(fps, 1.0f);
}

float AdaptiveQuality::sharedBudgetMs(float frameBudgetMs, float othersMs, int sharers) noexcept {
  if (sharers <= 1) {
    return frameBudgetMs;
  }
  const float fairShare = frameBudgetMs / static_cast<float>(sharers);
  return std::max(frameBudgetMs - std::max(othersMs, 0.0f), fairShare);
}

void AdaptiveQuality::reset() noexcept {
  step_ = 0;
  smoothedMs_ = 0.0f;
  haveSample_ = false;
  overSeconds_ = 0.0f;
  underSeconds_ = 0.0f;
  settleRemaining_ = 0.0f;
}

void AdaptiveQuality::changeTo(std::size_t step) noexcept {
  const float ratio = kSteps[step] / kSteps[step_];
  step_ = step;
  // What the new size should cost, so the next decision doesn't start from
  // the old size's cost.
  smoothedMs_ *= ratio * ratio;
  overSeconds_ = 0.0f;
  underSeconds_ = 0.0f;
  settleRemaining_ = config_.settleSeconds;
}

float AdaptiveQuality::onFrame(float gpuMs, float cpuMs, float dtSeconds) noexcept {
  const float cost = gpuMs > 0.0f ? gpuMs : cpuMs;
  if (!(cost >= 0.0f) || !(dtSeconds > 0.0f)) {
    return scale(); // nothing usable this frame
  }
  if (settleRemaining_ > 0.0f) {
    settleRemaining_ -= dtSeconds;
    return scale();
  }

  if (!haveSample_) {
    smoothedMs_ = cost;
    haveSample_ = true;
  } else {
    const float alpha = 1.0f - std::exp(-dtSeconds / std::max(config_.smoothingSeconds, 1.0e-3f));
    smoothedMs_ += alpha * (cost - smoothedMs_);
  }

  const float budgetMs = 1000.0f / std::max(config_.targetFps, 1.0f);
  const float current = kSteps[step_];

  if (smoothedMs_ > config_.downFraction * budgetMs) {
    underSeconds_ = 0.0f;
    overSeconds_ += dtSeconds;
    if (overSeconds_ >= config_.downSeconds && step_ + 1 < kSteps.size()) {
      // Far over budget: go straight to the first step predicted to fit
      // with some room, rather than one step per settle period.
      auto next = step_ + 1;
      while (next + 1 < kSteps.size()) {
        const float ratio = kSteps[next] / current;
        if (smoothedMs_ * ratio * ratio <= 0.7f * budgetMs) {
          break;
        }
        ++next;
      }
      changeTo(next);
    }
    return scale();
  }
  overSeconds_ = 0.0f;

  if (step_ > 0) {
    const float ratio = kSteps[step_ - 1] / current;
    if (smoothedMs_ * ratio * ratio < config_.upFraction * budgetMs) {
      underSeconds_ += dtSeconds;
      if (underSeconds_ >= config_.upSeconds) {
        changeTo(step_ - 1);
      }
      return scale();
    }
  }
  underSeconds_ = 0.0f;
  return scale();
}

} // namespace milkdawp::core
