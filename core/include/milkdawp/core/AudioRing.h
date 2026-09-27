// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace milkdawp::core {

/// Lock-free single-producer/single-consumer ring buffer for interleaved
/// float PCM (Phase 1.1, §4.2/§4.3). The audio thread is the sole writer
/// (write() never allocates or locks); the analysis thread is the sole
/// reader, pulling fixed-size hops via consumeHop(). copyLatest() is safe to
/// call from a third thread (e.g. a UI meter) since it only reads what's
/// already been published, but is *not* itself a second consumer: it does
/// not advance the hop-consumption cursor.
///
/// Capacity is in frames (samples per channel) and must be large enough to
/// cover the largest expected gap between audio callbacks and analysis hops;
/// write() overwrites the oldest unread frames if the analysis thread falls
/// behind (audio must never block on a full ring).
class AudioRing {
public:
  AudioRing(std::size_t capacityFrames, int numChannels);

  [[nodiscard]] std::size_t capacityFrames() const noexcept { return capacityFrames_; }
  [[nodiscard]] int numChannels() const noexcept { return numChannels_; }

  /// Absolute number of frames written since construction. Monotonic.
  [[nodiscard]] std::uint64_t samplePosition() const noexcept {
    return writePosition_.load(std::memory_order_acquire);
  }

  /// Audio thread only. Copies `numFrames` interleaved frames into the ring
  /// and advances the write cursor. Never allocates or locks.
  void write(const float* interleaved, std::size_t numFrames) noexcept;

  /// Copies the most recently written `numFrames` interleaved frames into
  /// `dest` (size numFrames * numChannels). Frames not yet written are left
  /// as zero. Safe to call concurrently with write(); does not affect
  /// consumeHop()'s cursor.
  void copyLatest(float* dest, std::size_t numFrames) const noexcept;

  /// Copies frames [fromFrame, fromFrame + numFrames) by absolute position
  /// (the same numbering as samplePosition()). Frames not written yet, or
  /// already overwritten, come back as zero. Safe to call concurrently with
  /// write(); does not affect consumeHop()'s cursor. This is what lets a
  /// second reader (the render thread feeding projectM, Phase 2.14) keep
  /// its own cursor and read exactly the frames it hasn't seen.
  void copyRange(float* dest, std::int64_t fromFrame, std::size_t numFrames) const noexcept {
    copyFrames(dest, fromFrame, numFrames);
  }

  /// Analysis thread only. Attempts to consume the next `numFrames`
  /// interleaved frames in write order, advancing the read cursor. Returns
  /// false (and writes nothing) if fewer than `numFrames` are available yet.
  /// If the writer has overwritten frames the reader hadn't consumed yet
  /// (reader fell behind by more than the ring's capacity), the read cursor
  /// is snapped forward to the oldest frame still available.
  bool consumeHop(float* dest, std::size_t numFrames) noexcept;

  /// Analysis thread only. Absolute frame number consumeHop() reads next,
  /// i.e. one past the last frame it returned.
  [[nodiscard]] std::uint64_t readPosition() const noexcept { return readPosition_; }

private:
  void copyFrames(float* dest, std::int64_t fromFrame, std::size_t numFrames) const noexcept;

  static_assert(std::atomic<float>::is_always_lock_free, "AudioRing needs lock-free float atomics");

  std::size_t capacityFrames_;
  int numChannels_;
  // capacityFrames_ * numChannels_, interleaved. Relaxed atomics: write()
  // may overwrite a slot while a reader copies it (the ring never blocks the
  // audio thread), and readers detect that via writeReserve_ afterwards.
  std::vector<std::atomic<float>> buffer_;

  std::atomic<std::uint64_t> writePosition_{0}; // total frames written so far (published)
  std::atomic<std::uint64_t> writeReserve_{0};  // frames write() may have overwritten, >= writePosition_
  std::uint64_t readPosition_ = 0;              // consumeHop's cursor (reader-owned, not atomic)
};

} // namespace milkdawp::core
