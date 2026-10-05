// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "milkdawp/engine/RecentErrors.h"

using milkdawp::engine::RecentErrors;

TEST_CASE("RecentErrors keeps errors oldest first and folds back-to-back repeats (5.9)",
          "[engine][RecentErrors]") {
  RecentErrors errors;
  CHECK(errors.snapshot().empty());

  errors.add("preset", "a.milk skipped: empty file");
  errors.add("projectM", "shader failed to compile\n");
  errors.add("projectM", "shader failed to compile");
  errors.add("preset", "a.milk skipped: empty file");

  const auto entries = errors.snapshot();
  REQUIRE(entries.size() == 3);
  CHECK(entries[0].source == "preset");
  CHECK(entries[1].message == "shader failed to compile"); // trailing newline dropped
  CHECK(entries[1].count == 2);
  CHECK(entries[2].count == 1); // a repeat, but not back to back
  CHECK(entries[2].lastTimeMs >= entries[0].lastTimeMs);

  errors.clear();
  CHECK(errors.snapshot().empty());
}

TEST_CASE("RecentErrors keeps only the newest entries and cuts long messages", "[engine][RecentErrors]") {
  RecentErrors errors;
  for (std::size_t i = 0; i < RecentErrors::kCapacity + 5; ++i) {
    errors.add("preset", "error " + std::to_string(i));
  }
  const auto entries = errors.snapshot();
  REQUIRE(entries.size() == RecentErrors::kCapacity);
  CHECK(entries.front().message == "error 5");
  CHECK(entries.back().message == "error " + std::to_string(RecentErrors::kCapacity + 4));

  errors.add("projectM", std::string(5000, 'x'));
  CHECK(errors.snapshot().back().message.size() == RecentErrors::kMaxMessageLength + 3);
}
