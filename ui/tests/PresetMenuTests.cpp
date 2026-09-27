// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/ui/PresetMenu.h"

using namespace milkdawp::ui;

TEST_CASE("Preset names split into folder and leaf", "[ui][PresetMenu]") {
  CHECK(splitPresetName("Geiss - Cosmic Dust").folder.empty());
  CHECK(splitPresetName("Geiss - Cosmic Dust").leaf == "Geiss - Cosmic Dust");
  const auto nested = splitPresetName("Cream of the Crop/Dancer/Flexi - swing");
  CHECK(nested.folder == "Cream of the Crop/Dancer");
  CHECK(nested.leaf == "Flexi - swing");
  CHECK(splitPresetName("Pack\\Name").folder == "Pack"); // Windows separators too
  CHECK(splitPresetName("Pack\\Sub\\Name").folder == "Pack/Sub"); // shown with forward slashes
}

TEST_CASE("A flat playlist stays flat", "[ui][PresetMenu]") {
  const auto tree = buildPresetTree({"a", "b", "c"});
  CHECK(tree.folders.empty());
  REQUIRE(tree.presets.size() == 3);
  CHECK(tree.presets[2] == std::pair<std::string, int>{"c", 2});
}

TEST_CASE("Subfolders become nested groups that keep playlist indices", "[ui][PresetMenu]") {
  const auto tree = buildPresetTree({"top", "Pack/one", "Pack/Sub/two", "Pack/three", "Other\\four"});
  REQUIRE(tree.presets.size() == 1);
  CHECK(tree.presets[0] == std::pair<std::string, int>{"top", 0});

  REQUIRE(tree.folders.size() == 2);
  const auto& pack = tree.folders[0];
  CHECK(pack.name == "Pack");
  REQUIRE(pack.presets.size() == 2);
  CHECK(pack.presets[0] == std::pair<std::string, int>{"one", 1});
  CHECK(pack.presets[1] == std::pair<std::string, int>{"three", 3});
  REQUIRE(pack.folders.size() == 1);
  CHECK(pack.folders[0].name == "Sub");
  CHECK(pack.folders[0].presets[0] == std::pair<std::string, int>{"two", 2});
  CHECK(tree.folders[1].name == "Other");
  CHECK(tree.folders[1].presets[0].second == 4);

  CHECK(pack.containsIndex(2));
  CHECK_FALSE(pack.containsIndex(4));
  CHECK(tree.containsIndex(4));
  CHECK_FALSE(tree.containsIndex(-1));
}

TEST_CASE("The (name, index) overload keeps caller-supplied indices, for a filtered view", "[ui][PresetMenu]") {
  // §4.5's browser: favourites/search results are a subset of the playlist,
  // so the tree must carry the *real* playlist index, not its position in
  // the filtered vector.
  const std::vector<std::pair<std::string, int>> entries{{"Pack/one", 7}, {"top", 2}};
  const auto tree = buildPresetTree(entries);
  REQUIRE(tree.presets.size() == 1);
  CHECK(tree.presets[0] == std::pair<std::string, int>{"top", 2});
  REQUIRE(tree.folders.size() == 1);
  CHECK(tree.folders[0].presets[0] == std::pair<std::string, int>{"one", 7});
}
