// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

namespace milkdawp::app {

/// Decides what the input hint says (4.2): no device, no signal, or fine.
/// Pure and clock-agnostic (the caller passes the time), so it is unit-tested.
///
/// "No signal" needs the peak to stay under the threshold for a while, so a
/// quiet passage or a gap between tracks doesn't flash the hint; any peak
/// above it clears the hint at once.
class SignalMonitor {
public:
  enum class State { NoDevice, NoSignal, Signal };

  static constexpr float kThreshold = 0.001f;        // about -60 dBFS
  static constexpr double kSilenceBeforeHint = 3.0; // seconds

  /// `peak` is the largest absolute sample since the previous update.
  State update(bool deviceOpen, float peak, double nowSeconds) noexcept {
    if (!deviceOpen) {
      lastSoundAt_ = nowSeconds;
      state_ = State::NoDevice;
      return state_;
    }
    if (state_ == State::NoDevice) {
      lastSoundAt_ = nowSeconds; // a device just opened: give it time
      state_ = State::Signal;
    }
    if (peak > kThreshold) {
      lastSoundAt_ = nowSeconds;
      state_ = State::Signal;
    } else if (nowSeconds - lastSoundAt_ >= kSilenceBeforeHint) {
      state_ = State::NoSignal;
    }
    return state_;
  }

  [[nodiscard]] State state() const noexcept { return state_; }

private:
  State state_ = State::NoDevice;
  double lastSoundAt_ = 0.0;
};

} // namespace milkdawp::app
