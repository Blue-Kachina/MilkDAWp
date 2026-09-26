// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace milkdawp::core {

/// Single-writer, multi-reader lock-free snapshot publisher (§4.2: "writes
/// transport info into an atomic snapshot"). Not `std::atomic<T>` directly,
/// because real payloads (e.g. `TransportInfo`) are wider than the
/// platform's lock-free CAS width, so `std::atomic<T>` would silently fall
/// back to an internal lock -- exactly what the audio thread must never touch.
///
/// A seqlock: the writer bumps a sequence counter to odd, stores the payload,
/// then bumps it to even. A reader copies the payload between two loads of
/// the counter and retries if either was odd or they differ, so it never
/// returns a mix of two publishes. The writer is wait-free (it never waits
/// for readers), which is the property the audio thread needs; readers may
/// retry, which is fine for the UI and render threads that call read().
///
/// This replaced a two-buffer + flipped-index design (`DoubleBufferedSnapshot`)
/// that could tear: two publishes during one slow read overwrote the buffer
/// that reader was still copying.
///
/// The payload is stored as relaxed `std::atomic<std::uint64_t>` words
/// rather than a plain `T`, so concurrent read/publish is not a C++ data race
/// (and TSan stays quiet), per Boehm, "Can Seqlocks Get Along With
/// Programming Language Memory Models?" (2012).
///
/// Only one thread may call publish(); any number of threads may call
/// read() concurrently with it and with each other.
template <typename T> class SeqlockSnapshot {
  static_assert(std::is_trivially_copyable_v<T>, "SeqlockSnapshot only carries trivially-copyable payloads");
  static_assert(std::is_default_constructible_v<T>, "SeqlockSnapshot needs a default value before the first publish");
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "SeqlockSnapshot needs lock-free 64-bit atomics");
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "SeqlockSnapshot needs lock-free 32-bit atomics");

  static constexpr std::size_t kWordCount = (sizeof(T) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);
  using Words = std::array<std::uint64_t, kWordCount>;

public:
  /// read() returns a default-constructed T until the first publish().
  SeqlockSnapshot() noexcept { storeWords(toWords(T{})); }

  /// Writer thread only. Never allocates, never blocks, never waits.
  void publish(const T& value) noexcept {
    const Words words = toWords(value);
    const std::uint32_t seq = sequence_.load(std::memory_order_relaxed);
    sequence_.store(seq + 1, std::memory_order_relaxed); // odd: write in progress
    std::atomic_thread_fence(std::memory_order_release);
    storeWords(words);
    sequence_.store(seq + 2, std::memory_order_release); // even: consistent
  }

  /// Any thread. Returns the most recently published value, retrying if a
  /// publish overlapped the copy.
  [[nodiscard]] T read() const noexcept {
    Words words{};
    std::uint32_t before = 0;
    std::uint32_t after = 0;
    do {
      before = sequence_.load(std::memory_order_acquire);
      for (std::size_t i = 0; i < kWordCount; ++i) {
        words[i] = words_[i].load(std::memory_order_relaxed);
      }
      std::atomic_thread_fence(std::memory_order_acquire);
      after = sequence_.load(std::memory_order_relaxed);
    } while ((before & 1U) != 0 || before != after);

    T value;
    std::memcpy(&value, words.data(), sizeof(T));
    return value;
  }

private:
  static Words toWords(const T& value) noexcept {
    Words words{};
    std::memcpy(words.data(), &value, sizeof(T));
    return words;
  }

  void storeWords(const Words& words) noexcept {
    for (std::size_t i = 0; i < kWordCount; ++i) {
      words_[i].store(words[i], std::memory_order_relaxed);
    }
  }

  std::atomic<std::uint32_t> sequence_{0};
  std::array<std::atomic<std::uint64_t>, kWordCount> words_{};
};

} // namespace milkdawp::core
