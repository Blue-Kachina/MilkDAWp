// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

namespace milkdawp::engine {

/// A colour texture with a framebuffer object rendering into it: what
/// projectM draws each frame into (§4.5), and what `OutputSurface`s sample
/// through a shared context. Construction, resize() and destruction need a
/// current GL context, with `juce::gl` functions loaded.
///
/// resize() re-specifies the texture's storage in place, so the texture
/// *name* never changes over the target's lifetime. Other contexts that
/// share it can keep using the same name across resizes.
class GlFrameTarget {
public:
  GlFrameTarget(int width, int height);
  ~GlFrameTarget();
  GlFrameTarget(const GlFrameTarget&) = delete;
  GlFrameTarget& operator=(const GlFrameTarget&) = delete;

  void resize(int width, int height);

  [[nodiscard]] std::uint32_t texture() const noexcept { return texture_; }
  [[nodiscard]] std::uint32_t framebuffer() const noexcept { return framebuffer_; }
  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] int height() const noexcept { return height_; }

  /// Reads the target back as tightly packed RGBA8, bottom row first.
  /// Slow; for tests and diagnostics only.
  void readPixels(std::vector<std::uint8_t>& rgba) const;

private:
  std::uint32_t texture_ = 0;
  std::uint32_t framebuffer_ = 0;
  int width_ = 0;
  int height_ = 0;
};

} // namespace milkdawp::engine
