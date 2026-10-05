// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "milkdawp/core/VisualControls.h"

namespace milkdawp::engine {

class GlFrameTarget;

/// One place the effects chain runs (a layer, or the canvas): the smoothed
/// controls, the Rotation angle they have turned the frame to, and the
/// targets the passes draw through, including Trails' previous frame. Render
/// thread only; the targets need its context. Holds no GL objects while its
/// effects are neutral.
class EffectsState {
public:
  EffectsState();
  ~EffectsState();
  EffectsState(const EffectsState&) = delete;
  EffectsState& operator=(const EffectsState&) = delete;

  /// Moves the smoothed controls towards `target` and turns the frame by
  /// Rotation over `dtSeconds`. With Rotation back at 0, the frame settles
  /// upright again (to the nearest whole turn) over about a second, so the
  /// chain can go neutral. Returns whether there is anything to draw: false
  /// means the layer goes out as is, and this state frees its targets.
  bool advance(const core::VisualControls& target, float dtSeconds);

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] const core::VisualControls& controls() const noexcept { return smoothed_; }
  [[nodiscard]] float angleRadians() const noexcept { return angle_; }

  /// Sets the controls and angle outright, without smoothing (tests, and the
  /// first frame).
  void setImmediate(const core::VisualControls& controls, float angleRadians = 0.0f);

private:
  friend class EffectsChain;
  struct Targets;

  core::VisualControls smoothed_;
  float angle_ = 0.0f;
  bool primed_ = false;
  bool active_ = false;
  std::unique_ptr<Targets> targets_;
};

/// Phase 8.2: the post effects the Visual globals drive, in the engine's one
/// GL context, after a layer renders and/or on the mixed canvas (exploration
/// doc §4.4). Fixed order: colour (hue, saturation, brightness) and geometry
/// (mirror, kaleidoscope, rotation, zoom, pixelate, RGB split) in one pass,
/// then blur, glow (quarter-resolution bloom), and Trails (the previous output
/// mixed back in). Each stage is skipped while its amounts are neutral, and
/// the whole chain is never called when all of them are (`EffectsState::advance`).
///
/// Sizes scale with the frame height, so a lowered render quality (5.3) looks
/// the same, only softer. Render thread only, with the context current and
/// `juce::gl` loaded; if the shaders fail to build, `ok()` is false, `error()`
/// says why, and `apply()` copies the source unchanged. GL state it touches is
/// restored, since projectM shares the context.
class EffectsChain {
public:
  EffectsChain();
  ~EffectsChain();
  EffectsChain(const EffectsChain&) = delete;
  EffectsChain& operator=(const EffectsChain&) = delete;

  [[nodiscard]] bool ok() const noexcept { return main_ != 0; }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }

  /// Draws `sourceTexture` through `state`'s current effects into `output`
  /// (the source is the same size as the output, and not the output itself).
  void apply(EffectsState& state, std::uint32_t sourceTexture, const GlFrameTarget& output);

  /// How strongly Trails keeps the previous frame at its maximum (Trails = 1).
  static constexpr float kMaxTrailsDecay = 0.96f;

private:
  void draw(std::uint32_t program, const GlFrameTarget& target, std::uint32_t texture,
            std::uint32_t second = 0) const;

  std::uint32_t main_ = 0;
  std::uint32_t blur_ = 0;
  std::uint32_t bright_ = 0;
  std::uint32_t combine_ = 0;
  std::uint32_t vertexArray_ = 0;
  std::string error_;
};

} // namespace milkdawp::engine
