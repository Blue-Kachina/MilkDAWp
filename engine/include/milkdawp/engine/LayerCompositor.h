// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "milkdawp/engine/LayerChannel.h"

namespace milkdawp::engine {

class GlFrameTarget;

/// One layer's texture and how it goes onto the canvas.
struct LayerDraw {
  std::uint32_t texture = 0; // a texture in the compositor's context, same size as the canvas
  float opacity = 1.0f;      // 0..1
  LayerBlend blend = LayerBlend::Normal;
  /// Media (8.6): the texture's own alpha multiplies the opacity (a PNG's
  /// transparent parts stay clear). Layers rendered by projectM ignore it.
  bool textureAlpha = false;
  /// Texture coordinates are scaled by this about the centre: below 1 shows the
  /// middle part of a texture whose shape differs from the canvas
  /// (`coverUvScale`). (1, 1) maps it as is.
  float uvScaleX = 1.0f;
  float uvScaleY = 1.0f;
};

/// Draws layers' textures one over another into a canvas target (Layers L2,
/// layers_like_shrek.md §3). Each layer is a single fullscreen draw with GL
/// blend state doing the mixing, so the canvas is never read while it is
/// written. The canvas stays opaque: layers change its colour, never its alpha.
///
/// Render thread only, with the engine's GL context current and `juce::gl`
/// functions loaded. Construction compiles one small GLSL 3.30 program, which
/// both the Windows compatibility context and Linux's core context accept; if
/// that fails `ok()` is false, `error()` says why, and `compose()` does nothing.
/// GL state it touches is restored, since projectM shares the context.
class LayerCompositor {
public:
  LayerCompositor();
  ~LayerCompositor();
  LayerCompositor(const LayerCompositor&) = delete;
  LayerCompositor& operator=(const LayerCompositor&) = delete;

  [[nodiscard]] bool ok() const noexcept { return program_ != 0; }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }

  /// Clears `canvas` to opaque black, then draws `layers` bottom to top.
  void compose(const GlFrameTarget& canvas, std::span<const LayerDraw> layers);
  /// Draws `layers` over what `canvas` already holds (no clear): media over a
  /// layer's picture (8.6).
  void overlay(const GlFrameTarget& canvas, std::span<const LayerDraw> layers);
  /// Replaces `target` with `layer` alone, unblended: its colour, and as alpha
  /// its opacity (times the texture's alpha when `textureAlpha`), everything
  /// else transparent. Burn in (8.6f) hands projectM the result, so projectM's
  /// own blend applies Media Mix and the crop.
  void stamp(const GlFrameTarget& target, const LayerDraw& layer);

private:
  void draw(const GlFrameTarget& canvas, std::span<const LayerDraw> layers, bool clear);
  std::int32_t textureAlphaUniform_ = -1;
  std::int32_t uvScaleUniform_ = -1;
  std::int32_t belowUniform_ = -1;
  std::unique_ptr<GlFrameTarget> below_; // Displace's copy of the canvas, made when first needed
  std::uint32_t program_ = 0;
  std::uint32_t vertexArray_ = 0;
  std::int32_t textureUniform_ = -1;
  std::int32_t opacityUniform_ = -1;
  std::int32_t modeUniform_ = -1;
  std::string error_;
};

} // namespace milkdawp::engine
