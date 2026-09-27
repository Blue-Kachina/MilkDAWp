// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/ui/ControlDrawer.h"

using namespace milkdawp::ui;

TEST_CASE("A wide docked drawer shows every control", "[ui][ControlDrawer]") {
  for (const int width : {compactBelowWidth, 960, 1920}) {
    const auto layout = drawerLayoutFor(width, false);
    CHECK_FALSE(layout.compact);
    CHECK(layout.showModeChip);
    CHECK(layout.showBpm);
    CHECK(layout.showUtilities);
    CHECK(layout.showPin);
    CHECK_FALSE(layout.showMore);
  }
}

TEST_CASE("A narrow drawer folds the chip, badge and utilities into the more menu", "[ui][ControlDrawer]") {
  for (const int width : {360, 480, compactBelowWidth - 1}) {
    for (const bool floating : {false, true}) {
      const auto layout = drawerLayoutFor(width, floating);
      CHECK(layout.compact);
      CHECK_FALSE(layout.showModeChip);
      CHECK_FALSE(layout.showBpm);
      CHECK_FALSE(layout.showUtilities);
      CHECK_FALSE(layout.showPin);
      CHECK(layout.showMore);
    }
  }
}

TEST_CASE("Floating controls never show the pin", "[ui][ControlDrawer]") {
  for (const int width : {360, 720, 1920}) {
    CHECK_FALSE(drawerLayoutFor(width, true).showPin);
  }
  CHECK(drawerLayoutFor(1920, true).showUtilities);
}
