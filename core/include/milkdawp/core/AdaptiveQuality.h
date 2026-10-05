// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>

namespace milkdawp::core {

struct AdaptiveQualityConfig {
  float targetFps = 60.0f;
  /// Step down when the smoothed frame cost stays above this fraction of
  /// the frame budget for `downSeconds`: there is no headroom left.
  float downFraction = 0.85f;
  float downSeconds = 0.5f;
  /// Step up when the next step up is predicted to cost under this fraction
  /// of the budget for `upSeconds`. Far below `downFraction`, so a step up is
  /// never followed by a step straight back down (the hysteresis).
  float upFraction = 0.55f;
  float upSeconds = 3.0f;
  /// After a change, frames are ignored this long: the first ones at a new
  /// size cost more (new buffers) and would trigger another change.
  float settleSeconds = 1.0f;
  /// Time constant of the frame cost's smoothing.
  float smoothingSeconds = 0.25f;
};

/// Adaptive render quality (5.3): picks the FBO scale from the GPU time the
/// render thread measures, so a heavy preset on a weak GPU drops resolution
/// instead of frame rate, and goes back up once there is headroom.
///
/// Discrete steps (100, 85, 70, 55, 40 %), not a continuous scale: every
/// change reallocates the frame buffers and shows as a resolution jump, so
/// fewer, larger changes look better. A frame's cost is predicted to scale
/// with pixel count (scale squared), which overestimates what a step up
/// costs (projectM's per-vertex work doesn't scale), so steps up stay
/// cautious.
///
/// Pure and deterministic: one call per rendered frame (render thread).
class AdaptiveQuality {
public:
  static constexpr std::array<float, 5> kSteps{1.0f, 0.85f, 0.7f, 0.55f, 0.4f};

  explicit AdaptiveQuality(AdaptiveQualityConfig config = {});

  void setTargetFps(float fps) noexcept;
  [[nodiscard]] const AdaptiveQualityConfig& config() const noexcept { return config_; }

  /// 5.6: the frame budget one of several renderers sharing a GPU (plugin
  /// instances in one DAW) holds itself to. `frameBudgetMs` is one frame at
  /// the target rate, `othersMs` what the other `sharers - 1` renderers cost
  /// per frame right now. It is whatever the others leave over, but never
  /// less than an equal share: a heavy instance gives way first, and a light
  /// one is never squeezed below its fair part by a heavy neighbour. Alone
  /// (`sharers <= 1`) it is the whole frame.
  [[nodiscard]] static float sharedBudgetMs(float frameBudgetMs, float othersMs, int sharers) noexcept;

  /// One rendered frame: its GPU time (<= 0: not measured, then `cpuMs`,
  /// which includes waiting for the GPU, stands in) and the seconds since the
  /// previous frame. Returns the scale for the next frame.
  float onFrame(float gpuMs, float cpuMs, float dtSeconds) noexcept;

  [[nodiscard]] float scale() const noexcept { return kSteps[step_]; }
  [[nodiscard]] std::size_t step() const noexcept { return step_; }
  /// The smoothed frame cost the decisions use, in ms.
  [[nodiscard]] float smoothedMs() const noexcept { return smoothedMs_; }

  /// Back to full scale with no history (Auto chosen again, a new context).
  void reset() noexcept;

private:
  void changeTo(std::size_t step) noexcept;

  AdaptiveQualityConfig config_;
  std::size_t step_ = 0;
  float smoothedMs_ = 0.0f;
  bool haveSample_ = false;
  float overSeconds_ = 0.0f;
  float underSeconds_ = 0.0f;
  float settleRemaining_ = 0.0f;
};

} // namespace milkdawp::core
