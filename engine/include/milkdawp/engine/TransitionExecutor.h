// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "milkdawp/core/Messages.h"

namespace milkdawp::engine {

/// Executes `TransitionRequestMessage`s at the right sample, on the render
/// thread (§4.2, §4.4, Phase 2.6). A pure sample-position state machine --
/// no GL, no projectM, no JUCE -- so it's fully deterministic under test
/// with a simulated clock, the same way `core::TransitionScheduler` (which
/// produces the messages this class consumes) already is.
///
/// Soft cuts are issued `blendSeconds / 2` early so the perceptual midpoint
/// of the blend lands on `dueAtSample` (§4.4: "the request is issued
/// blend/2 early so the perceptual midpoint sits on the beat"); Hard cuts
/// are issued exactly at `dueAtSample`. "Issued" means the callback passed
/// to onTick() fires; `RenderEngine` owns one and, in that callback, loads
/// the preset text the director handed over for `presetId`
/// (`PresetHandoff`).
///
/// The scheduler currently decides on the hop a beat is crossed, so
/// `dueAtSample` is usually already in the past when a request arrives and
/// it fires on the next frame; the early issue only helps once transitions
/// are scheduled ahead of the beat.
class TransitionExecutor {
public:
  explicit TransitionExecutor(double sampleRate) : sampleRate_(sampleRate) {
    // Room for every queued request plus as many again already pending, so
    // drainQueueIntoPending() never allocates on the render thread.
    pending_.reserve(kQueueCapacity * 2);
  }

  /// Render thread. Sample rate of the clock `dueAtSample` is expressed in
  /// (used to convert a soft cut's blend/2 early issue into samples).
  void setSampleRate(double sampleRate) noexcept { sampleRate_ = sampleRate; }

  /// Analysis thread (TransitionScheduler's owner). Never blocks; returns
  /// false (and drops the request) if the queue is momentarily full.
  bool pushRequest(const core::TransitionRequestMessage& request) noexcept { return queue_.push(request); }

  /// A request that became due this tick.
  struct DueTransition {
    core::TransitionRequestMessage request;
    std::int64_t actualIssueSample;
    std::int64_t landingErrorSamples; // actualIssueSample - intended issue sample; 0 is perfect
  };

  /// Render thread, once per frame, with the current absolute sample
  /// position (e.g. `AudioRing::samplePosition()`). Invokes `onDue` once
  /// for every pending request whose issue sample has been reached (in the
  /// order they were pushed) and removes them from the pending set. Newly
  /// queued requests are picked up before the due-check, so a request whose
  /// issue sample is already in the past when pushed fires on the very next
  /// tick rather than being delayed by one extra frame.
  ///
  /// Template rather than `std::function` deliberately: this runs on the
  /// render thread, where an allocating callback wrapper would be exactly
  /// the kind of hidden allocation §4.2 rules out.
  template <typename Callback> void onTick(std::int64_t currentSample, Callback&& onDue) {
    drainQueueIntoPending();

    for (auto it = pending_.begin(); it != pending_.end();) {
      const auto issueSample = issueSampleFor(*it);
      if (currentSample >= issueSample) {
        onDue(DueTransition{*it, currentSample, currentSample - issueSample});
        it = pending_.erase(it);
      } else {
        ++it;
      }
    }
  }

  [[nodiscard]] std::size_t pendingCount() const noexcept { return pending_.size(); }

private:
  [[nodiscard]] std::int64_t issueSampleFor(const core::TransitionRequestMessage& request) const noexcept;
  void drainQueueIntoPending();

  double sampleRate_;
  static constexpr std::size_t kQueueCapacity = 32;
  core::SpscQueue<core::TransitionRequestMessage, kQueueCapacity> queue_;
  std::vector<core::TransitionRequestMessage> pending_;
};

} // namespace milkdawp::engine
