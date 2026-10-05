// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/VisualControls.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::core {

bool postEffectsNeutral(const VisualControls& c) noexcept {
  return c.hueDegrees == 0.0f && c.saturation == 1.0f && c.brightness == 1.0f && c.zoom == 0.0f &&
         c.rotation == 0.0f && c.trails == 0.0f && c.pixelate == 0.0f && c.glow == 0.0f && c.blur == 0.0f &&
         c.mirror == MirrorMode::Off && c.kaleidoscopeSegments == 0 && c.rgbSplit == 0.0f;
}

namespace {

void glide(float& value, float target, float k, float snap) noexcept {
  value += (target - value) * k;
  if (std::abs(target - value) < snap) {
    value = target;
  }
}

} // namespace

VisualControls smoothVisualControls(const VisualControls& current, const VisualControls& target, float dtSeconds,
                                    float seconds) noexcept {
  const float k = seconds <= 0.0f ? 1.0f : std::clamp(1.0f - std::exp(-std::max(dtSeconds, 0.0f) / seconds), 0.0f, 1.0f);
  VisualControls out = current;
  glide(out.hueDegrees, target.hueDegrees, k, 0.05f);
  glide(out.saturation, target.saturation, k, 1.0e-3f);
  glide(out.brightness, target.brightness, k, 1.0e-3f);
  glide(out.speed, target.speed, k, 1.0e-3f);
  glide(out.zoom, target.zoom, k, 1.0e-3f);
  glide(out.rotation, target.rotation, k, 1.0e-3f);
  glide(out.warp, target.warp, k, 1.0e-3f);
  glide(out.trails, target.trails, k, 1.0e-3f);
  glide(out.waveSize, target.waveSize, k, 1.0e-3f);
  glide(out.pixelate, target.pixelate, k, 1.0e-3f);
  glide(out.glow, target.glow, k, 1.0e-3f);
  glide(out.blur, target.blur, k, 1.0e-3f);
  glide(out.rgbSplit, target.rgbSplit, k, 1.0e-3f);
  glide(out.mediaMix, target.mediaMix, k, 1.0e-3f);
  out.mirror = target.mirror;
  out.kaleidoscopeSegments = target.kaleidoscopeSegments;
  return out;
}

} // namespace milkdawp::core
