// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace milkdawp::engine {

/// CPU copies of rendered frames, from the render thread to the surfaces that
/// cannot share the engine's GL context (ADR-0009's readback fallback: a
/// driver that refuses to share, Linux until EGL sharing exists, Android
/// under stock JUCE, ADR-0010). Those surfaces upload the pixels into a
/// texture of their own.
///
/// One writer (the render thread), any number of readers (surface GL
/// threads). A reader pins the buffer it reads, and the writer never picks a
/// pinned or the published buffer, so a frame is never overwritten while it
/// is being uploaded. If every other buffer is pinned, the writer drops that
/// frame instead of waiting. The mutex guards only the choice of buffer and
/// the published index; pixels are copied outside it.
class FrameReadbackExchange {
public:
  struct Buffer {
    std::vector<std::uint8_t> rgba; // tightly packed RGBA8, bottom row first (glReadPixels order)
    int width = 0;
    int height = 0;
    std::uint64_t number = 0; // the engine's frame number
    mutable std::atomic<int> readers{0};
  };

  /// A pinned, published frame; empty before the first one. Unpins on
  /// destruction.
  class ReadLock {
  public:
    ReadLock() = default;
    explicit ReadLock(const Buffer* buffer) noexcept : buffer_(buffer) {}
    ~ReadLock() { release(); }
    ReadLock(ReadLock&& other) noexcept : buffer_(other.buffer_) { other.buffer_ = nullptr; }
    ReadLock& operator=(ReadLock&& other) noexcept {
      if (this != &other) {
        release();
        buffer_ = other.buffer_;
        other.buffer_ = nullptr;
      }
      return *this;
    }
    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;

    explicit operator bool() const noexcept { return buffer_ != nullptr; }
    const Buffer& operator*() const noexcept { return *buffer_; }
    const Buffer* operator->() const noexcept { return buffer_; }

  private:
    void release() noexcept {
      if (buffer_ != nullptr) {
        buffer_->readers.fetch_sub(1, std::memory_order_release);
        buffer_ = nullptr;
      }
    }
    const Buffer* buffer_ = nullptr;
  };

  // ---- writer (render thread) ----

  /// Picks a free buffer sized for `width` x `height` and returns its pixel
  /// storage, or nullptr if every candidate is pinned (drop this frame).
  /// Allocates only when the size grows.
  [[nodiscard]] std::uint8_t* beginWrite(int width, int height) {
    {
      const std::lock_guard lock(mutex_);
      writing_ = -1;
      for (int i = 0; i < kBufferCount; ++i) {
        if (i != published_ && buffers_[static_cast<std::size_t>(i)].readers.load(std::memory_order_acquire) == 0) {
          writing_ = i;
          break;
        }
      }
    }
    if (writing_ < 0 || width <= 0 || height <= 0) {
      writing_ = -1;
      return nullptr;
    }
    auto& buffer = buffers_[static_cast<std::size_t>(writing_)];
    buffer.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    buffer.width = width;
    buffer.height = height;
    return buffer.rgba.data();
  }

  /// Publishes the buffer from the last successful beginWrite().
  void commitWrite(std::uint64_t frameNumber) {
    if (writing_ < 0) {
      return;
    }
    buffers_[static_cast<std::size_t>(writing_)].number = frameNumber;
    const std::lock_guard lock(mutex_);
    published_ = writing_;
    writing_ = -1;
  }

  /// Forgets the published frame (readback stopped): readers get nothing
  /// until the next commit, rather than a stale frame from before.
  void clear() {
    const std::lock_guard lock(mutex_);
    published_ = -1;
  }

  // ---- readers (any thread) ----

  [[nodiscard]] ReadLock acquireLatest() const {
    const std::lock_guard lock(mutex_);
    if (published_ < 0) {
      return {};
    }
    const auto& buffer = buffers_[static_cast<std::size_t>(published_)];
    buffer.readers.fetch_add(1, std::memory_order_relaxed);
    return ReadLock(&buffer);
  }

private:
  static constexpr int kBufferCount = 3;

  mutable std::mutex mutex_;
  std::array<Buffer, kBufferCount> buffers_;
  int published_ = -1; // guarded by mutex_
  int writing_ = -1;   // writer thread only (read under mutex_ in commitWrite)
};

} // namespace milkdawp::engine
