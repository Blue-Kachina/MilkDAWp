// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "milkdawp/core/AudioRing.h"

namespace milkdawp::engine {

/// Feeds projectM exactly the audio written since the previous render
/// frame (Phase 2.14). projectM keeps its own sample buffer, so each call
/// must hand over only *new* frames: v2's first cut pushed a fixed
/// "latest 512 frames" per frame, which dropped audio at low frame rates
/// (48 kHz / 60 fps = 800 new frames per frame) and fed the same samples
/// twice at high ones.
///
/// Keeps its own read cursor into the `AudioRing` (a second, independent
/// reader, alongside the analysis thread's consumeHop() cursor). If more
/// than `maxFramesPerCall` new frames are waiting (a slow frame, or the
/// first call), only the newest `maxFramesPerCall` are fed: projectM would
/// discard the older ones anyway (`projectm_pcm_get_max_samples()`).
///
/// Render thread only. The scratch buffer is sized once in the constructor,
/// so feed() never allocates.
class PcmFeeder {
public:
  PcmFeeder(std::size_t maxFramesPerCall, int numChannels)
      : maxFramesPerCall_(std::max<std::size_t>(maxFramesPerCall, 1)), numChannels_(numChannels),
        scratch_(maxFramesPerCall_ * static_cast<std::size_t>(std::max(numChannels, 1)), 0.0f) {}

  /// Calls `sink(const float* interleaved, std::size_t frames, int channels)`
  /// at most once, with the frames written to `ring` since the previous
  /// call (capped as described above). Returns the number of frames fed.
  template <typename Sink> std::size_t feed(const core::AudioRing& ring, Sink&& sink) {
    const std::uint64_t writePos = ring.samplePosition();
    if (!started_) {
      // Nothing has been fed yet: start from "now" minus one call's worth,
      // rather than replaying the whole ring from frame 0.
      started_ = true;
      readPos_ = writePos > maxFramesPerCall_ ? writePos - maxFramesPerCall_ : 0;
    }
    if (writePos <= readPos_) {
      return 0;
    }

    std::uint64_t available = writePos - readPos_;
    if (available > maxFramesPerCall_) {
      readPos_ = writePos - maxFramesPerCall_;
      available = maxFramesPerCall_;
    }

    const auto frames = static_cast<std::size_t>(available);
    ring.copyRange(scratch_.data(), static_cast<std::int64_t>(readPos_), frames);
    readPos_ = writePos;
    sink(static_cast<const float*>(scratch_.data()), frames, numChannels_);
    return frames;
  }

  /// Forget the cursor; the next feed() starts from "now" again. For when
  /// the ring is replaced or the projectM instance is recreated.
  void reset() noexcept { started_ = false; }

  [[nodiscard]] std::uint64_t readPosition() const noexcept { return readPos_; }
  [[nodiscard]] std::size_t maxFramesPerCall() const noexcept { return maxFramesPerCall_; }

private:
  std::size_t maxFramesPerCall_;
  int numChannels_;
  std::vector<float> scratch_;
  bool started_ = false;
  std::uint64_t readPos_ = 0;
};

} // namespace milkdawp::engine
