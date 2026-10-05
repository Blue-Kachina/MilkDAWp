// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>

namespace milkdawp::core {

/// The `layerGate*` parameters (Phase 8.2b).
struct LayerGateSettings {
  bool enabled = false;
  float thresholdDb = -80.0f; // -100..0 dBFS
  float releaseMs = 80.0f;    // 0..2000: the fade-out once the gate closes
};

/// Like a noise gate on a guitar, for a layer's picture (exploration doc §4.4):
/// while the layer's own input stays below the threshold the layer fades out,
/// and it is back the moment the input returns. Clean stops in the music show
/// as clean stops in the picture.
///
/// Opens with no fade as soon as the input reaches the threshold, so a hit
/// lands on time. Closes only once the input falls 3 dB below it (fixed
/// hysteresis, so a decaying note around the threshold doesn't flicker) and has
/// stayed there for a short fixed hold; then the envelope falls to 0 over the
/// release time. Driven once per rendered frame with that frame's peak level:
/// a picture needs no sample-accurate detection, and a frame of latency is the
/// most there is. Real-time safe; owned by one thread (the render thread).
class LayerGate {
public:
  static constexpr float kHysteresisDb = 3.0f;
  static constexpr float kHoldSeconds = 0.05f;

  /// Advances by `dtSeconds` with `peakDb` the loudest sample since the last
  /// call (anything at or below -200 for silence, or no new audio). Returns
  /// the envelope: 1 open, 0 closed and faded, in between while fading.
  float process(float peakDb, float dtSeconds, const LayerGateSettings& settings) noexcept;

  [[nodiscard]] bool isOpen() const noexcept { return open_; }
  [[nodiscard]] float envelope() const noexcept { return envelope_; }
  /// Open and fully visible, as a fresh gate starts.
  void reset() noexcept;

private:
  bool open_ = true;
  float holdRemaining_ = kHoldSeconds;
  float envelope_ = 1.0f;
};

/// The peak of `samples` (any channel layout) in dBFS; -200 for silence.
[[nodiscard]] float peakDbfs(const float* samples, std::size_t count) noexcept;

} // namespace milkdawp::core
