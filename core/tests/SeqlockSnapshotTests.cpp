// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

#include "milkdawp/core/SeqlockSnapshot.h"

using namespace milkdawp::core;

namespace {
struct Payload {
  int a = 0;
  double b = 0.0;
};

// Eight words wide, so a torn read has many more places to land than
// Payload's two, which makes a tearing bug fail reliably rather than
// occasionally.
struct WidePayload {
  std::array<std::int64_t, 8> words{};
};

struct NonZeroDefaults {
  int timeSigNumerator = 4;
  double bpm = 120.0;
};

// Runs a writer publishing `makeValue(i)` for increasing i while this thread
// reads `reads` times; returns true if any read failed `isConsistent`.
template <typename T, typename Make, typename Check>
bool sawTornRead(SeqlockSnapshot<T>& snapshot, Make makeValue, Check isConsistent, int reads) {
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    std::int64_t i = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      snapshot.publish(makeValue(++i));
    }
  });

  bool torn = false;
  for (int i = 0; i < reads && !torn; ++i) {
    torn = !isConsistent(snapshot.read());
  }

  stop.store(true, std::memory_order_relaxed);
  writer.join();
  return torn;
}
} // namespace

TEST_CASE("SeqlockSnapshot::read returns a default value before any publish", "[core][SeqlockSnapshot]") {
  SeqlockSnapshot<Payload> snapshot;
  const auto value = snapshot.read();
  CHECK(value.a == 0);
  CHECK(value.b == 0.0);
}

TEST_CASE("SeqlockSnapshot::read keeps non-zero member defaults before any publish", "[core][SeqlockSnapshot]") {
  // TransportInfo defaults timeSigNumerator to 4; zero-filled storage would
  // hand out 0 instead.
  SeqlockSnapshot<NonZeroDefaults> snapshot;
  const auto value = snapshot.read();
  CHECK(value.timeSigNumerator == 4);
  CHECK(value.bpm == 120.0);
}

TEST_CASE("SeqlockSnapshot::read reflects the most recent publish", "[core][SeqlockSnapshot]") {
  SeqlockSnapshot<Payload> snapshot;
  snapshot.publish({1, 1.5});
  CHECK(snapshot.read().a == 1);

  snapshot.publish({2, 2.5});
  CHECK(snapshot.read().a == 2);
  CHECK(snapshot.read().b == 2.5);
}

TEST_CASE("SeqlockSnapshot never hands a reader a torn value under concurrent publish", "[core][SeqlockSnapshot]") {
  SeqlockSnapshot<Payload> snapshot;
  // The two fields are derivable from each other, so a mix of two publishes
  // is detectable.
  const bool torn = sawTornRead(
      snapshot, [](std::int64_t v) { return Payload{static_cast<int>(v), static_cast<double>(v) * 2.0}; },
      [](const Payload& p) { return static_cast<double>(p.a) * 2.0 == p.b; }, 200000);
  CHECK_FALSE(torn);
}

TEST_CASE("SeqlockSnapshot never tears a multi-word payload under concurrent publish", "[core][SeqlockSnapshot]") {
  SeqlockSnapshot<WidePayload> snapshot;
  const bool torn = sawTornRead(
      snapshot,
      [](std::int64_t v) {
        WidePayload p;
        p.words.fill(v);
        return p;
      },
      [](const WidePayload& p) {
        for (const auto w : p.words) {
          if (w != p.words[0]) {
            return false;
          }
        }
        return true;
      },
      200000);
  CHECK_FALSE(torn);
}
