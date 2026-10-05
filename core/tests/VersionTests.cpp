// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/Version.h"

TEST_CASE("version() matches the project version", "[core][version]") {
  const auto v = milkdawp::core::version();
  CHECK(v.major == 1);
  CHECK(v.minor >= 0);
  CHECK(v.patch >= 0);
}

TEST_CASE("versionString() starts with version(), then an optional pre-release suffix", "[core][version]") {
  const auto v = milkdawp::core::version();
  const auto numbers = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
  const std::string label = milkdawp::core::versionString();
  REQUIRE(label.rfind(numbers, 0) == 0);
  if (label.size() > numbers.size()) {
    CHECK(label[numbers.size()] == '-');
    CHECK(label.size() > numbers.size() + 1);
  }
}
