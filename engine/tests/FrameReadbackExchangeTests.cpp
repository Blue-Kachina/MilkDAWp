// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "milkdawp/engine/FrameReadbackExchange.h"

using milkdawp::engine::FrameReadbackExchange;

namespace {
// Writes a frame whose every byte is `number & 0xFF`, so a reader can tell a
// torn frame from a whole one.
bool writeFrame(FrameReadbackExchange& exchange, int width, int height, std::uint64_t number) {
  auto* pixels = exchange.beginWrite(width, height);
  if (pixels == nullptr) {
    return false;
  }
  std::memset(pixels, static_cast<int>(number & 0xFFU), static_cast<std::size_t>(width) * height * 4);
  exchange.commitWrite(number);
  return true;
}
} // namespace

TEST_CASE("FrameReadbackExchange hands out nothing before the first frame", "[engine][readback]") {
  FrameReadbackExchange exchange;
  CHECK_FALSE(exchange.acquireLatest());
}

TEST_CASE("FrameReadbackExchange publishes the latest committed frame", "[engine][readback]") {
  FrameReadbackExchange exchange;
  REQUIRE(writeFrame(exchange, 4, 2, 1));
  REQUIRE(writeFrame(exchange, 8, 4, 2));

  const auto frame = exchange.acquireLatest();
  REQUIRE(frame);
  CHECK(frame->number == 2);
  CHECK(frame->width == 8);
  CHECK(frame->height == 4);
  REQUIRE(frame->rgba.size() == 8U * 4U * 4U);
  CHECK(frame->rgba.front() == 2);
}

TEST_CASE("FrameReadbackExchange never overwrites a pinned frame", "[engine][readback]") {
  FrameReadbackExchange exchange;
  REQUIRE(writeFrame(exchange, 2, 2, 1));
  const auto pinnedFirst = exchange.acquireLatest();
  REQUIRE(pinnedFirst);

  REQUIRE(writeFrame(exchange, 2, 2, 2));
  const auto pinnedSecond = exchange.acquireLatest();
  REQUIRE(pinnedSecond);

  // Three buffers: one pinned (1), one pinned and published (2), one free.
  REQUIRE(writeFrame(exchange, 2, 2, 3));
  // Now 1 and 2 are pinned and 3 is published: nothing left to write into.
  CHECK_FALSE(writeFrame(exchange, 2, 2, 4));
  CHECK(pinnedFirst->number == 1);
  CHECK(pinnedFirst->rgba.front() == 1);
  CHECK(pinnedSecond->number == 2);
  CHECK(exchange.acquireLatest()->number == 3);
}

TEST_CASE("FrameReadbackExchange reuses a buffer once its reader lets go", "[engine][readback]") {
  FrameReadbackExchange exchange;
  REQUIRE(writeFrame(exchange, 2, 2, 1));
  {
    const auto pinned = exchange.acquireLatest();
    REQUIRE(writeFrame(exchange, 2, 2, 2));
    const auto alsoPinned = exchange.acquireLatest();
    REQUIRE(writeFrame(exchange, 2, 2, 3));
    CHECK_FALSE(writeFrame(exchange, 2, 2, 4));
  }
  CHECK(writeFrame(exchange, 2, 2, 4));
  CHECK(exchange.acquireLatest()->number == 4);
}

TEST_CASE("FrameReadbackExchange::clear hides the stale frame until the next commit", "[engine][readback]") {
  FrameReadbackExchange exchange;
  REQUIRE(writeFrame(exchange, 2, 2, 1));
  exchange.clear();
  CHECK_FALSE(exchange.acquireLatest());
  REQUIRE(writeFrame(exchange, 2, 2, 2));
  CHECK(exchange.acquireLatest()->number == 2);
}

TEST_CASE("FrameReadbackExchange readers never see a torn frame under contention", "[engine][readback]") {
  FrameReadbackExchange exchange;
  std::atomic<bool> stop{false};
  std::atomic<int> tornFrames{0};
  std::atomic<int> framesRead{0};

  std::vector<std::thread> readers;
  for (int r = 0; r < 3; ++r) {
    readers.emplace_back([&] {
      std::uint64_t lastNumber = 0;
      while (!stop.load()) {
        const auto frame = exchange.acquireLatest();
        if (!frame || frame->number == lastNumber) {
          std::this_thread::yield();
          continue;
        }
        lastNumber = frame->number;
        const auto expected = static_cast<std::uint8_t>(frame->number & 0xFFU);
        const bool whole = std::all_of(frame->rgba.begin(), frame->rgba.end(),
                                       [expected](std::uint8_t byte) { return byte == expected; });
        if (!whole) {
          tornFrames.fetch_add(1);
        }
        framesRead.fetch_add(1);
      }
    });
  }

  // At least 2000 frames, and on until the readers have really contended
  // (under a sanitizer the writer can finish before a reader is scheduled).
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (std::uint64_t n = 1; n <= 2000 || (framesRead.load() < 200 && std::chrono::steady_clock::now() < deadline);
       ++n) {
    const int size = 16 + static_cast<int>(n % 3) * 8; // sizes change too
    (void)writeFrame(exchange, size, size, n);
  }
  stop.store(true);
  for (auto& reader : readers) {
    reader.join();
  }
  CHECK(tornFrames.load() == 0);
  CHECK(framesRead.load() > 0);
}
