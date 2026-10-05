// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

namespace milkdawp::core {

/// The time a projectM instance is told it is (Phase 8.3, the Speed global):
/// `dt x speed` integrated frame by frame and handed to
/// `projectm_set_frame_time`. Never `time x speed`, so turning the knob changes
/// how fast the preset moves from now on and never jumps it.
///
/// Speed slows only what a preset drives from its clock (`time`, and what
/// projectM animates by elapsed time). It can't slow per-frame decay and
/// feedback (they advance once per rendered frame whatever the clock says) or
/// the reaction to audio, which arrives in real time.
///
/// projectM measures a soft cut's blend on this clock too, so a slow (or 0)
/// Speed would stretch a blend out (or freeze it half done). While a blend runs
/// the clock goes at least real time; the blend may finish sooner with Speed
/// above 1, as everything does.
class PresetClock {
public:
  /// Advances by `dtSeconds` of wall-clock time (clamped to 0..0.25, so a
  /// stall or a paused render thread doesn't fast-forward the preset) and
  /// returns the new time, in seconds.
  double advance(double dtSeconds, float speed) noexcept;
  /// A soft cut of `blendSeconds` (wall clock) has just started.
  void onSoftCut(double blendSeconds) noexcept;
  [[nodiscard]] double time() const noexcept { return time_; }

  static constexpr double kMaxStepSeconds = 0.25;

private:
  double time_ = 0.0;
  double blendRemaining_ = 0.0;
};

} // namespace milkdawp::core
