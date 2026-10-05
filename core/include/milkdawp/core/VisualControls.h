// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>

namespace milkdawp::core {

/// Mirror's choices, in the `visualMirror` parameter's order.
enum class MirrorMode : int { Off = 0, LeftRight = 1, TopBottom = 2, Quad = 3 };

/// Kaleidoscope's segment counts, indexed by the `visualKaleidoscope` choice (0 = off).
inline constexpr std::array<int, 7> kKaleidoscopeSegments{0, 3, 4, 5, 6, 8, 12};

/// The 16 Visual globals in engine units (Phase 8.1, exploration doc §6.2). A
/// plain struct, so it crosses threads through a SeqlockSnapshot. Every
/// default is the control's neutral value.
///
/// Stage A serves Hue, Saturation, Brightness, Zoom, Rotation, Trails,
/// Pixelate, Glow, Blur, Mirror, Kaleidoscope and RGB Split as post effects
/// (`engine::EffectsChain`), and Speed through the preset clock
/// (`PresetClock`). Warp and Wave Size need a `.milkdawp` preset (Stage B),
/// Media Mix a media source (8.6); until then they are carried but unused.
struct VisualControls {
  float hueDegrees = 0.0f;  // -180..180
  float saturation = 1.0f;  // 0..2
  float brightness = 1.0f;  // 0..2
  float speed = 1.0f;       // 0..4
  float zoom = 0.0f;        // -1..1: the frame scales by 2^zoom
  float rotation = 0.0f;    // -1..1: a speed, in half-turns per second
  float warp = 1.0f;        // 0..3 (Stage B)
  float trails = 0.0f;      // 0..1
  float waveSize = 1.0f;    // 0..3 (Stage B)
  float pixelate = 0.0f;    // 0..1
  float glow = 0.0f;        // 0..1
  float blur = 0.0f;        // 0..1
  MirrorMode mirror = MirrorMode::Off;
  int kaleidoscopeSegments = 0; // 0 = off, else 3..12
  float rgbSplit = 0.0f;    // 0..1
  float mediaMix = 0.0f;    // 0..1 (8.6)

  friend bool operator==(const VisualControls&, const VisualControls&) = default;
};

/// True when none of the post effects would change a pixel. Speed, Warp, Wave
/// Size and Media Mix are not post effects, so they do not count here, and a
/// frame still turned by an earlier Rotation is the caller's to check.
[[nodiscard]] bool postEffectsNeutral(const VisualControls& controls) noexcept;

/// One step of the short smoothing every control gets (§6.4): automation
/// arrives per block and can step, so continuous values glide towards
/// `target` with a `seconds` time constant. Choices (Mirror, Kaleidoscope)
/// switch at once. Values within a hair of the target snap to it, so a
/// control turned back to neutral really reaches neutral (and costs nothing).
[[nodiscard]] VisualControls smoothVisualControls(const VisualControls& current, const VisualControls& target,
                                                  float dtSeconds, float seconds = 0.05f) noexcept;

} // namespace milkdawp::core
