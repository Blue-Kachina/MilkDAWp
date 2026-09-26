// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/PresetLibrary.h"

using namespace milkdawp::core;

TEST_CASE("PresetLibrary interns each path once, with ids starting at 1", "[core][PresetLibrary]") {
  PresetLibrary library;
  const auto a = library.idFor("/presets/a.milk");
  const auto b = library.idFor("/presets/b.milk");
  CHECK(a == 1);
  CHECK(b == 2);
  CHECK(library.idFor("/presets/a.milk") == a);
  CHECK(library.size() == 2);
}

TEST_CASE("PresetLibrary maps ids back to paths, and rejects unknown ids", "[core][PresetLibrary]") {
  PresetLibrary library;
  const auto id = library.idFor("C:/presets/x.milk");
  CHECK(library.pathFor(id) == "C:/presets/x.milk");
  CHECK_FALSE(library.pathFor(0).has_value());
  CHECK_FALSE(library.pathFor(id + 1).has_value());
}
