// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/PresetMetadata.h"

using namespace milkdawp::core;

TEST_CASE("Preset metadata is keyed by file name, case-insensitively", "[core][PresetMetadata]") {
  CHECK(PresetMetadata::keyFor("C:\\Presets\\Pack\\Geiss - Dust.milk") == "geiss - dust.milk");
  CHECK(PresetMetadata::keyFor("/home/me/presets/Geiss - Dust.milk") == "geiss - dust.milk");
  CHECK(PresetMetadata::keyFor("Geiss - Dust.milk") == "geiss - dust.milk");
  // 8.10: a .milkdawp shares the ratings of the .milk it was made from.
  CHECK(PresetMetadata::keyFor("C:\\Presets\\Geiss - Dust.MILKDAWP") == "geiss - dust.milk");

  PresetMetadata metadata;
  metadata.set("C:/old/place/Geiss - Dust.milk", {4, false, {"calm"}});
  // The same preset after moving the folder, or in another pack.
  CHECK(metadata.get("D:/new/place/GEISS - DUST.milk").rating == 4);
  CHECK(metadata.get("D:/new/place/Other.milk").isDefault());
}

TEST_CASE("Setting default info removes the entry", "[core][PresetMetadata]") {
  PresetMetadata metadata;
  metadata.set("a.milk", {3, false, {}});
  CHECK(metadata.size() == 1);
  metadata.set("a.milk", {});
  CHECK(metadata.size() == 0);
}

TEST_CASE("Tags are normalised", "[core][PresetMetadata]") {
  CHECK(PresetMetadata::parseTags(" Calm, dark ,calm,,  ") ==
        std::vector<std::string>{"calm", "dark"});
  CHECK(PresetMetadata::parseTags("").empty());
  PresetMetadata metadata;
  metadata.set("a.milk", {0, false, {"Dark", " calm", "dark"}});
  CHECK(metadata.get("a.milk").tags == std::vector<std::string>{"calm", "dark"});
  metadata.set("b.milk", {0, false, {"Abstract"}});
  CHECK(metadata.allTags() == std::vector<std::string>{"abstract", "calm", "dark"});
}

TEST_CASE("Ratings set the weighted shuffle weight", "[core][PresetMetadata]") {
  CHECK(PresetMetadata::weightFor({0, false, {}}) == 1.0f);
  CHECK(PresetMetadata::weightFor({1, false, {}}) == 0.25f);
  CHECK(PresetMetadata::weightFor({3, false, {}}) == 1.0f);
  CHECK(PresetMetadata::weightFor({5, false, {}}) == 4.0f);
}

TEST_CASE("A tag filter matches any of its tags", "[core][PresetMetadata]") {
  const PresetInfo info{0, false, {"calm", "dark"}};
  CHECK(PresetMetadata::matchesFilter(info, {}));
  CHECK(PresetMetadata::matchesFilter(info, {"dark"}));
  CHECK(PresetMetadata::matchesFilter(info, {"bright", "calm"}));
  CHECK_FALSE(PresetMetadata::matchesFilter(info, {"bright"}));
  CHECK_FALSE(PresetMetadata::matchesFilter({}, {"bright"}));
}

TEST_CASE("Preset metadata round-trips through text", "[core][PresetMetadata]") {
  PresetMetadata metadata;
  metadata.set("Pack/One.milk", {5, false, {"calm", "dark"}});
  metadata.set("Two.milk", {0, true, {}});
  metadata.set("Three, with comma.milk", {2, true, {"x"}});
  const auto text = metadata.serialize();
  CHECK(text.rfind("# MilkDAWp preset metadata v1\n", 0) == 0);
  CHECK(PresetMetadata::parse(text) == metadata);
  // Windows line endings, as an editor might leave them.
  std::string crlf;
  for (const char c : text) {
    if (c == '\n') {
      crlf += '\r';
    }
    crlf += c;
  }
  CHECK(PresetMetadata::parse(crlf) == metadata);
}

TEST_CASE("Malformed metadata lines are skipped", "[core][PresetMetadata]") {
  const auto metadata = PresetMetadata::parse("# header\n"
                                              "garbage\n"
                                              "9\t\t\tbad rating.milk\n"
                                              "3\t\tok\n"
                                              "4\tn\tcalm\tgood.milk\n"
                                              "\n");
  CHECK(metadata.size() == 1);
  CHECK(metadata.get("good.milk") == PresetInfo{4, true, {"calm"}});
}

TEST_CASE("Favourites round-trip, and older files and readers ignore the new flag", "[core][PresetMetadata]") {
  PresetMetadata metadata;
  PresetInfo info;
  info.favourite = true;
  info.neverAutoSelect = true;
  info.rating = 2;
  metadata.set("C:/a/fav.milk", info);
  CHECK_FALSE(info.isDefault());

  const auto text = metadata.serialize();
  CHECK(text.find("2\tnf\t\tfav.milk") != std::string::npos);
  CHECK(PresetMetadata::parse(text).get("fav.milk") == info);

  // A favourite with nothing else is still kept (not "default").
  PresetMetadata onlyFavourite;
  PresetInfo heart;
  heart.favourite = true;
  onlyFavourite.set("x.milk", heart);
  CHECK(onlyFavourite.size() == 1);

  // A file written before 6.1b has no 'f': nothing is a favourite.
  const auto old = PresetMetadata::parse("3\tn\tcalm\told.milk\n");
  CHECK_FALSE(old.get("old.milk").favourite);
  CHECK(old.get("old.milk").neverAutoSelect);
}
