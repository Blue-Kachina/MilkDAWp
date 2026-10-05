// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/SectionDetector.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::core {

namespace {

constexpr float kSilenceDb = -120.0f;
constexpr double kHistoryStepSeconds = 0.25;

float toDb(double energy) {
  return energy > 1.0e-12 ? static_cast<float>(10.0 * std::log10(energy)) : kSilenceDb;
}

std::size_t hopsFor(double seconds, double hopSeconds) {
  return static_cast<std::size_t>(std::max(0.0, std::round(seconds / hopSeconds)));
}

} // namespace

SectionDetector::SectionDetector(double sampleRate,
                                 std::size_t hopSize,
                                 SectionDetectorConfig config)
    : hopSeconds_(static_cast<double>(hopSize) / sampleRate), config_(config) {
  resize();
}

void SectionDetector::setConfig(const SectionDetectorConfig& config) {
  const bool sameWindows = config.quietSeconds == config_.quietSeconds &&
                           config.guardSeconds == config_.guardSeconds &&
                           config.levelSeconds == config_.levelSeconds &&
                           config.historySeconds == config_.historySeconds;
  config_ = config;
  if (!sameWindows) {
    resize(); // the thresholds can change on the fly; the windows start over
  }
}

void SectionDetector::resize() {
  levelHops_ = std::max<std::size_t>(1, hopsFor(config_.levelSeconds, hopSeconds_));
  quietHops_ = std::max<std::size_t>(1, hopsFor(config_.quietSeconds, hopSeconds_));
  guardHops_ = hopsFor(config_.guardSeconds, hopSeconds_);
  historyStepHops_ = std::max<std::size_t>(1, hopsFor(kHistoryStepSeconds, hopSeconds_));
  const auto historySize = std::max<std::size_t>(
      1, hopsFor(config_.historySeconds, hopSeconds_ * static_cast<double>(historyStepHops_)));

  levelRing_.assign(levelHops_, 0.0f);
  recentLevels_.assign(quietHops_ + guardHops_, kSilenceDb);
  history_.assign(historySize, kSilenceDb);
  historyScratch_.reserve(historySize);
  reset();
}

void SectionDetector::reset() {
  std::fill(levelRing_.begin(), levelRing_.end(), 0.0f);
  levelPos_ = 0;
  levelCount_ = 0;
  levelSum_ = 0.0;
  recentPos_ = 0;
  recentCount_ = 0;
  historyPos_ = 0;
  historyCount_ = 0;
  hopsSinceHistory_ = 0;
  referenceDb_ = kSilenceDb;
  hopsSinceDrop_ = 0;
  haveDropped_ = false;
}

float SectionDetector::reference() {
  historyScratch_.assign(history_.begin(),
                         history_.begin() + static_cast<std::ptrdiff_t>(historyCount_));
  const auto percentile = std::clamp(config_.referencePercentile, 0.0f, 1.0f);
  const auto index =
      static_cast<std::size_t>(percentile * static_cast<float>(historyScratch_.size() - 1));
  std::nth_element(historyScratch_.begin(),
                   historyScratch_.begin() + static_cast<std::ptrdiff_t>(index),
                   historyScratch_.end());
  return historyScratch_[index];
}

SectionFrame SectionDetector::processHop(float bassEnergy, bool strongBassOnset) {
  SectionFrame frame;
  const auto energy = std::max(0.0f, bassEnergy);
  frame.bassDb = toDb(energy);

  levelSum_ += static_cast<double>(energy) - static_cast<double>(levelRing_[levelPos_]);
  levelRing_[levelPos_] = energy;
  levelPos_ = (levelPos_ + 1) % levelHops_;
  levelCount_ = std::min(levelCount_ + 1, levelHops_);
  frame.levelDb = toDb(std::max(0.0, levelSum_) / static_cast<double>(levelCount_));

  if (historyCount_ == 0) {
    referenceDb_ = frame.levelDb; // nothing to compare with yet
  }
  frame.referenceDb = referenceDb_;

  // The quiet stretch: the oldest `quietHops_` of the levels before this
  // hop, leaving out the newest `guardHops_`.
  const auto recentSize = recentLevels_.size();
  const bool quietWindowFull = recentCount_ == recentSize;
  if (quietWindowFull) {
    float quietMax = kSilenceDb;
    for (std::size_t i = 0; i < quietHops_; ++i) {
      quietMax = std::max(quietMax,
                          recentLevels_[(recentPos_ + i) % recentSize]); // recentPos_: the oldest
    }
    frame.quietMaxDb = quietMax;
    frame.breakdown = quietMax <= referenceDb_ - config_.jumpDb;

    // One drop per breakdown: the hits right after it still see the same
    // quiet stretch behind them.
    const bool settled = !haveDropped_ || hopsSinceDrop_ >= recentSize;
    frame.drop = strongBassOnset && settled && energy > config_.floorEnergy &&
                 frame.bassDb >= quietMax + config_.jumpDb &&
                 frame.bassDb >= referenceDb_ - config_.recoverDb;
  }

  if (frame.drop) {
    haveDropped_ = true;
    hopsSinceDrop_ = 0;
  } else if (haveDropped_) {
    ++hopsSinceDrop_;
  }

  recentLevels_[recentPos_] = frame.levelDb;
  recentPos_ = (recentPos_ + 1) % recentSize;
  recentCount_ = std::min(recentCount_ + 1, recentSize);

  if (++hopsSinceHistory_ >= historyStepHops_) {
    hopsSinceHistory_ = 0;
    history_[historyPos_] = frame.levelDb;
    historyPos_ = (historyPos_ + 1) % history_.size();
    historyCount_ = std::min(historyCount_ + 1, history_.size());
    referenceDb_ = reference();
  }
  return frame;
}

} // namespace milkdawp::core
