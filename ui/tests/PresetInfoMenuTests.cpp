// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/ui/PresetInfoMenu.h"

using namespace milkdawp;

namespace {

/// The menu's top-level items: text, ticked, has a submenu.
struct ItemInfo {
  juce::String text;
  bool ticked = false;
  const juce::PopupMenu* subMenu = nullptr;
  std::function<void()> action;
};

std::vector<ItemInfo> items(const juce::PopupMenu& menu) {
  std::vector<ItemInfo> result;
  for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
    const auto& item = it.getItem();
    result.push_back({item.text, item.isTicked, item.subMenu.get(), item.action});
  }
  return result;
}

} // namespace

TEST_CASE("Ratings show as stars", "[ui][PresetInfoMenu]") {
  CHECK(ui::ratingText(0) == "Not rated");
  CHECK(ui::ratingText(3) ==
        juce::String(juce::CharPointer_UTF8("\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85"
                                            "\xe2\x98\x86\xe2\x98\x86")));
}

TEST_CASE("The preset info items change one field each", "[ui][PresetInfoMenu]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  const core::PresetInfo info{2, false, {"calm"}};
  core::PresetInfo changed;
  bool editTags = false;
  juce::PopupMenu menu;
  ui::addPresetInfoItems(
      menu,
      info,
      [&changed](const core::PresetInfo& value) { changed = value; },
      [&editTags] { editTags = true; });

  const auto top = items(menu);
  REQUIRE(top.size() == 3);
  CHECK(top[0].text.startsWith("Rating: "));
  REQUIRE(top[0].subMenu != nullptr);
  CHECK(top[1].text == "Never auto-select");
  CHECK_FALSE(top[1].ticked);
  CHECK(top[2].text == "Tags: calm...");

  const auto ratings = items(*top[0].subMenu);
  REQUIRE(ratings.size() == 6); // 5 stars down to "No rating"
  CHECK(ratings[3].ticked);     // 2 stars
  ratings[0].action();          // 5 stars
  CHECK(changed == core::PresetInfo{5, false, {"calm"}});
  ratings[5].action();
  CHECK(changed.rating == 0);

  top[1].action();
  CHECK(changed == core::PresetInfo{2, true, {"calm"}});
  top[2].action();
  CHECK(editTags);
}
