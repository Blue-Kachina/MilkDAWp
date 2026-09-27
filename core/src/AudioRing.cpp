// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/AudioRing.h"

#include <algorithm>
#include <cassert>

namespace milkdawp::core {

namespace {

void storeFloats(std::atomic<float>* dest, const float* src, std::size_t count) noexcept {
  for (std::size_t i = 0; i < count; ++i) {
    dest[i].store(src[i], std::memory_order_relaxed);
  }
}

void loadFloats(float* dest, const std::atomic<float>* src, std::size_t count) noexcept {
  for (std::size_t i = 0; i < count; ++i) {
    dest[i] = src[i].load(std::memory_order_relaxed);
  }
}

} // namespace

AudioRing::AudioRing(std::size_t capacityFrames, int numChannels)
    : capacityFrames_(capacityFrames), numChannels_(numChannels),
      buffer_(capacityFrames * static_cast<std::size_t>(numChannels)) { // value-initialized to 0.0f (C++20)
  assert(capacityFrames_ > 0);
  assert(numChannels_ > 0);
}

void AudioRing::write(const float* interleaved, std::size_t numFrames) noexcept {
  const std::size_t channels = static_cast<std::size_t>(numChannels_);
  const std::uint64_t writePosBefore = writePosition_.load(std::memory_order_relaxed);

  std::size_t framesToStore = numFrames;
  const float* src = interleaved;
  if (framesToStore > capacityFrames_) {
    // Only the most recent capacityFrames_ frames can survive; the rest are
    // dropped (the reader could never have kept up with them anyway).
    const std::size_t skip = framesToStore - capacityFrames_;
    src += skip * channels;
    framesToStore = capacityFrames_;
  }

  const std::uint64_t startFrame = writePosBefore + (numFrames - framesToStore);
  const std::size_t startIndex = static_cast<std::size_t>(startFrame % capacityFrames_);

  // Seqlock-style: announce the frames about to be overwritten before
  // touching the buffer, so a reader that sees any new sample also sees
  // writeReserve_ covering it (the fences pair with copyFrames()'s).
  writeReserve_.store(writePosBefore + numFrames, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);

  const std::size_t firstChunk = std::min(framesToStore, capacityFrames_ - startIndex);
  storeFloats(&buffer_[startIndex * channels], src, firstChunk * channels);

  if (firstChunk < framesToStore) {
    const std::size_t remaining = framesToStore - firstChunk;
    storeFloats(&buffer_[0], src + firstChunk * channels, remaining * channels);
  }

  // Publish the data before advancing the cursor a reader synchronizes on.
  writePosition_.store(writePosBefore + numFrames, std::memory_order_release);
}

void AudioRing::copyFrames(float* dest, std::int64_t fromFrame, std::size_t numFrames) const noexcept {
  const std::uint64_t writePos = writePosition_.load(std::memory_order_acquire);
  const std::size_t channels = static_cast<std::size_t>(numChannels_);

  for (std::size_t i = 0; i < numFrames; ++i) {
    const std::int64_t frame = fromFrame + static_cast<std::int64_t>(i);
    const bool available = frame >= 0 && static_cast<std::uint64_t>(frame) < writePos &&
                            (writePos - static_cast<std::uint64_t>(frame)) <= capacityFrames_;
    if (!available) {
      std::fill_n(dest + i * channels, channels, 0.0f);
      continue;
    }
    const std::size_t index = static_cast<std::size_t>(static_cast<std::uint64_t>(frame) % capacityFrames_);
    loadFloats(dest + i * channels, &buffer_[index * channels], channels);
  }

  // A write() that overlapped the copy may have replaced some of those frames
  // with newer audio. Anything it could have reached is zeroed, as the
  // "already overwritten" frames are.
  std::atomic_thread_fence(std::memory_order_acquire);
  const std::uint64_t reserve = writeReserve_.load(std::memory_order_relaxed);
  if (reserve == writePos) {
    return;
  }
  for (std::size_t i = 0; i < numFrames; ++i) {
    const std::int64_t frame = fromFrame + static_cast<std::int64_t>(i);
    if (frame >= 0 && (reserve - static_cast<std::uint64_t>(frame)) > capacityFrames_) {
      std::fill_n(dest + i * channels, channels, 0.0f);
    }
  }
}

void AudioRing::copyLatest(float* dest, std::size_t numFrames) const noexcept {
  const std::uint64_t writePos = writePosition_.load(std::memory_order_acquire);
  const std::int64_t fromFrame = static_cast<std::int64_t>(writePos) - static_cast<std::int64_t>(numFrames);
  copyFrames(dest, fromFrame, numFrames);
}

bool AudioRing::consumeHop(float* dest, std::size_t numFrames) noexcept {
  const std::uint64_t writePos = writePosition_.load(std::memory_order_acquire);

  // If the reader fell behind by more than the ring holds, the frames in
  // between are unrecoverable; snap forward to the oldest one still buffered.
  if (writePos > readPosition_ && (writePos - readPosition_) > capacityFrames_) {
    readPosition_ = writePos - capacityFrames_;
  }

  if (writePos < readPosition_ + numFrames) {
    return false;
  }

  copyFrames(dest, static_cast<std::int64_t>(readPosition_), numFrames);
  readPosition_ += numFrames;
  return true;
}

} // namespace milkdawp::core
