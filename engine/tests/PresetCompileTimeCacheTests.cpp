// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/engine/PresetCompileTimeCache.h"

using namespace milkdawp::engine;

TEST_CASE("PresetCompileTimeCache has no estimate until a path is recorded", "[engine][PresetCompileTimeCache]") {
  PresetCompileTimeCache cache;
  CHECK_FALSE(cache.estimateMs("/presets/a.milk").has_value());
  CHECK(cache.size() == 0);
}

TEST_CASE("PresetCompileTimeCache recalls the last measurement per path", "[engine][PresetCompileTimeCache]") {
  PresetCompileTimeCache cache;
  cache.record("/presets/cheap.milk", 4.0f);
  cache.record("/presets/expensive.milk", 180.0f);

  REQUIRE(cache.estimateMs("/presets/cheap.milk").has_value());
  CHECK(*cache.estimateMs("/presets/cheap.milk") == 4.0f);
  REQUIRE(cache.estimateMs("/presets/expensive.milk").has_value());
  CHECK(*cache.estimateMs("/presets/expensive.milk") == 180.0f);
  CHECK_FALSE(cache.estimateMs("/presets/unknown.milk").has_value());
  CHECK(cache.size() == 2);
}

TEST_CASE("PresetCompileTimeCache keeps only the latest measurement for a path", "[engine][PresetCompileTimeCache]") {
  PresetCompileTimeCache cache;
  cache.record("/presets/a.milk", 10.0f);
  cache.record("/presets/a.milk", 2.0f);
  CHECK(*cache.estimateMs("/presets/a.milk") == 2.0f);
  CHECK(cache.size() == 1);
}

TEST_CASE("PresetCompileTimeCache ignores empty paths and negative timings", "[engine][PresetCompileTimeCache]") {
  PresetCompileTimeCache cache;
  cache.record("", 5.0f);
  cache.record("/presets/a.milk", -1.0f);
  CHECK(cache.size() == 0);
}
