// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/DisplayLayout.h"

using namespace milkdawp::core;

namespace {
// A 1080p primary with a 1440p display to its right, top-aligned one row up.
constexpr WindowBounds kPrimary{0, 0, 1920, 1080};
constexpr WindowBounds kRight{1920, -360, 2560, 1440};
} // namespace

TEST_CASE("findDisplay matches a display by its exact bounds", "[core][DisplayLayout]") {
  const std::array displays{kPrimary, kRight};
  CHECK(findDisplay(displays, kPrimary) == 0);
  CHECK(findDisplay(displays, kRight) == 1);
}

TEST_CASE("findDisplay treats an empty or disconnected target as automatic", "[core][DisplayLayout]") {
  const std::array displays{kPrimary, kRight};
  CHECK(findDisplay(displays, WindowBounds{}) == -1);
  CHECK(findDisplay(displays, WindowBounds{-1920, 0, 1920, 1080}) == -1); // unplugged since it was saved
}

TEST_CASE("placeOnDisplay keeps a window the user already put on that display", "[core][DisplayLayout]") {
  const WindowBounds saved{2100, 0, 1280, 720};
  CHECK(placeOnDisplay(saved, kRight, 1280, 720) == saved);
}

TEST_CASE("placeOnDisplay centres a default-sized window when the saved one is elsewhere", "[core][DisplayLayout]") {
  const WindowBounds saved{100, 100, 640, 360}; // on the primary
  const auto placed = placeOnDisplay(saved, kRight, 1280, 720);
  CHECK(placed == WindowBounds{1920 + (2560 - 1280) / 2, -360 + (1440 - 720) / 2, 1280, 720});
}

TEST_CASE("placeOnDisplay centres a default-sized window when nothing was saved", "[core][DisplayLayout]") {
  const auto placed = placeOnDisplay(WindowBounds{}, kPrimary, 1280, 720);
  CHECK(placed == WindowBounds{320, 180, 1280, 720});
}

TEST_CASE("placeOnDisplay never exceeds the display", "[core][DisplayLayout]") {
  const WindowBounds small{0, 0, 800, 600};
  const auto placed = placeOnDisplay(WindowBounds{}, small, 1280, 720);
  CHECK(placed == WindowBounds{0, 0, 800, 600});
}

TEST_CASE("layoutDisplayMap keeps relative positions and fits the box", "[core][DisplayLayout]") {
  const std::array displays{kPrimary, kRight};
  const auto tiles = layoutDisplayMap(displays, 400, 150, 0);
  REQUIRE(tiles.size() == 2);
  for (const auto& tile : tiles) {
    CHECK(tile.x >= 0);
    CHECK(tile.y >= 0);
    CHECK(tile.x + tile.width <= 400);
    CHECK(tile.y + tile.height <= 150);
  }
  CHECK(tiles[1].x >= tiles[0].x + tiles[0].width - 1); // right of the primary
  CHECK(tiles[1].y < tiles[0].y);                       // and one row up, as on the desktop
  CHECK(tiles[1].width > tiles[0].width);               // the 1440p display is bigger
}

TEST_CASE("layoutDisplayMap insets tiles so neighbours do not touch", "[core][DisplayLayout]") {
  const std::array displays{WindowBounds{0, 0, 1000, 500}, WindowBounds{1000, 0, 1000, 500}};
  const auto tiles = layoutDisplayMap(displays, 200, 100, 3);
  REQUIRE(tiles.size() == 2);
  CHECK(tiles[1].x - (tiles[0].x + tiles[0].width) >= 6);
}

TEST_CASE("layoutDisplayMap copes with nothing to lay out", "[core][DisplayLayout]") {
  CHECK(layoutDisplayMap({}, 200, 100).empty());
  const std::array displays{kPrimary};
  CHECK(layoutDisplayMap(displays, 0, 100).empty());
}
