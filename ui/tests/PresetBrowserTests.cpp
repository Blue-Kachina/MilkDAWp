// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 6.1b: the search index and the preset browser popover.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/PresetMetadata.h"
#include "milkdawp/ui/PresetBrowser.h"
#include "milkdawp/ui/PresetSearch.h"

using namespace milkdawp;
using ui::PresetBrowser;

namespace {

// A library the browser can change: metadata edits land here and bump the
// generation, like PresetMetadataStore.
struct FakeLibrary {
  std::vector<std::string> names{"Waveform/Wire Tangle/Geiss - 3D - Shockwaves", "Waveform/Wire Flat/Lightspeed-dune2",
                                 "Geometric/Wire Circles/Geiss - Many Colors 1", "Dancer/Blobby/suksma - septop",
                                 "Fractal\\Nested/Rovastar - Voyage"};
  std::vector<std::string> paths;
  core::PresetMetadata metadata;
  std::uint64_t metadataGeneration = 1;
  int current = 2;
  std::vector<int> picked;
  std::vector<std::string> recent;

  FakeLibrary() {
    for (const auto& name : names) {
      paths.push_back("C:/presets/" + name + ".milk");
    }
  }

  PresetBrowser::Source source(bool withRecent = false) {
    PresetBrowser::Source s;
    s.playlistGeneration = [] { return std::uint64_t{1}; };
    s.names = [this] { return names; };
    s.paths = [this] { return paths; };
    s.currentIndex = [this] { return current; };
    s.metadata = [this] { return std::make_shared<const core::PresetMetadata>(metadata); };
    s.metadataGeneration = [this] { return metadataGeneration; };
    s.setInfo = [this](const std::string& path, const core::PresetInfo& info) {
      metadata.set(path, info);
      ++metadataGeneration;
    };
    s.onPick = [this](int index) {
      picked.push_back(index);
      current = index;
    };
    if (withRecent) {
      s.recentPaths = [this] { return recent; };
    }
    return s;
  }
};

const juce::KeyPress kDown(juce::KeyPress::downKey);
const juce::KeyPress kUp(juce::KeyPress::upKey);
const juce::KeyPress kReturn(juce::KeyPress::returnKey);
const juce::KeyPress kEscape(juce::KeyPress::escapeKey);

juce::KeyPress character(char c) { return juce::KeyPress(c, juce::ModifierKeys(), static_cast<juce::juce_wchar>(c)); }

} // namespace

TEST_CASE("PresetSearch: words are lower case and split on whitespace", "[ui][PresetBrowser]") {
  CHECK(ui::presetSearchWords("  Geiss   WAVE\t") == std::vector<std::string>{"geiss", "wave"});
  CHECK(ui::presetSearchWords("   ").empty());
}

TEST_CASE("PresetSearch: every word must match the name, folder or extra text, in any order", "[ui][PresetBrowser]") {
  ui::PresetSearchIndex index;
  index.rebuild({"Waveform/Wire Tangle/Geiss - 3D - Shockwaves", "Geometric/Geiss - Many Colors 1",
                 "Fractal\\Nested\\Rovastar - Voyage"});
  CHECK(index.filter({}) == std::vector<int>{0, 1, 2});
  CHECK(index.filter({"geiss"}) == std::vector<int>{0, 1});
  CHECK(index.filter({"shock", "geiss"}) == std::vector<int>{0}); // order doesn't matter
  CHECK(index.filter({"waveform", "colors"}).empty());             // both words must match
  CHECK(index.filter({"fractal/nested"}) == std::vector<int>{2});  // backslashes searched as slashes

  index.setExtraText({"", "calm, favourite-ish", ""});
  CHECK(index.filter({"calm"}) == std::vector<int>{1});
  CHECK(index.filter({"geiss", "calm"}) == std::vector<int>{1});
}

TEST_CASE("PresetSearch: a keystroke over 10k presets is quick", "[ui][PresetBrowser][scale]") {
  std::vector<std::string> names;
  for (int i = 0; i < 10000; ++i) {
    names.push_back("Folder " + std::to_string(i % 37) + "/Sub " + std::to_string(i % 11) + "/Author " +
                    std::to_string(i % 101) + " - Preset number " + std::to_string(i));
  }
  ui::PresetSearchIndex index;
  auto start = std::chrono::steady_clock::now();
  index.rebuild(names);
  const auto rebuild = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  start = std::chrono::steady_clock::now();
  std::size_t found = 0;
  for (const auto* query : {"a", "au", "aut", "author 5", "author 5 number 1", "zzz"}) {
    found += index.filter(ui::presetSearchWords(query)).size();
  }
  const auto perQuery = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / 6.0;
  CHECK(found > 0);
  // ~1 ms optimized; generous so a Debug build passes. Catches quadratic.
  CHECK(perQuery < 0.1);
  std::printf("6.1b search (10k): rebuild %.1f ms, %.2f ms per query\n", rebuild * 1000.0, perQuery * 1000.0);
}

TEST_CASE("PresetBrowser: lists everything, filters as you type and counts", "[ui][PresetBrowser]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  FakeLibrary library;
  PresetBrowser browser;
  browser.setSource(library.source());
  browser.refresh();

  CHECK(browser.results().size() == 5);
  CHECK(browser.countText() == "5 presets");

  browser.setSearchText("geiss");
  CHECK(browser.results() == std::vector<int>{0, 2});
  CHECK(browser.countText() == "2 of 5 presets");
  CHECK(browser.selectedIndex() == 0); // the best match is ready for Return

  browser.setSearchText("nothing like this");
  CHECK(browser.results().empty());
  CHECK(browser.selectedIndex() == -1);
}

TEST_CASE("PresetBrowser: opening selects the playing preset; arrows move, Return plays, Esc closes",
          "[ui][PresetBrowser]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  FakeLibrary library;
  PresetBrowser browser;
  browser.setSource(library.source());
  bool closed = false;
  browser.onCloseRequested = [&closed] { closed = true; };

  browser.opened();
  CHECK(browser.selectedIndex() == 2);

  CHECK(browser.handleKey(kDown, false));
  CHECK(browser.selectedIndex() == 3);
  CHECK(library.picked.empty()); // moving doesn't play
  CHECK(browser.handleKey(kUp, false));
  CHECK(browser.handleKey(kUp, false));
  CHECK(browser.selectedIndex() == 1);
  CHECK(browser.handleKey(kReturn, false));
  CHECK(library.picked == std::vector<int>{1});

  // Letters belong to the search box unless the list has focus.
  CHECK_FALSE(browser.handleKey(character('f'), false));

  CHECK(browser.handleKey(kEscape, false));
  CHECK(closed);
}

TEST_CASE("PresetBrowser: F and 0-5 in the list set favourites and ratings in the shared metadata",
          "[ui][PresetBrowser]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  FakeLibrary library;
  PresetBrowser browser;
  browser.setSource(library.source());
  browser.opened(); // index 2 selected

  CHECK(browser.handleKey(character('f'), true));
  CHECK(library.metadata.get(library.paths[2]).favourite);
  CHECK(browser.handleKey(character('4'), true));
  CHECK(library.metadata.get(library.paths[2]).rating == 4);
  CHECK(library.metadata.get(library.paths[2]).favourite); // the rating kept the heart

  browser.handleKey(kDown, true); // index 3
  browser.handleKey(character('2'), true);
  browser.refresh(); // picks the store's new generation up

  browser.setTab(PresetBrowser::Tab::Favourites);
  CHECK(browser.results() == std::vector<int>{2});
  browser.setTab(PresetBrowser::Tab::Rated);
  CHECK(browser.results() == std::vector<int>{2, 3}); // best first

  browser.handleKey(juce::KeyPress(juce::KeyPress::homeKey), true); // first of Rated: index 2
  CHECK(browser.selectedIndex() == 2);
  browser.handleKey(character('f'), true); // clears index 2's heart
  browser.setTab(PresetBrowser::Tab::Favourites);
  CHECK(browser.results().empty());
}

TEST_CASE("PresetBrowser: favourites made elsewhere show up on the next poll", "[ui][PresetBrowser]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  FakeLibrary library;
  PresetBrowser browser;
  browser.setSource(library.source());
  browser.refresh();
  browser.setTab(PresetBrowser::Tab::Favourites);
  CHECK(browser.results().empty());

  // Another plugin instance or the app hearts a preset.
  core::PresetInfo info;
  info.favourite = true;
  library.metadata.set(library.paths[4], info);
  ++library.metadataGeneration;
  browser.refresh();
  CHECK(browser.results() == std::vector<int>{4});
}

TEST_CASE("PresetBrowser: tags are searchable", "[ui][PresetBrowser]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  FakeLibrary library;
  core::PresetInfo calm;
  calm.tags = {"calm"};
  library.metadata.set(library.paths[1], calm);
  PresetBrowser browser;
  browser.setSource(library.source());
  browser.refresh();
  browser.setSearchText("calm");
  CHECK(browser.results() == std::vector<int>{1});
}

TEST_CASE("PresetBrowser: the Recent tab exists only when the shell keeps a history", "[ui][PresetBrowser]") {
  const juce::ScopedJuceInitialiser_GUI gui;
  FakeLibrary library;
  {
    PresetBrowser browser;
    browser.setSource(library.source(false));
    CHECK_FALSE(browser.recentTab.isVisible());
  }
  library.recent = {library.paths[3], "C:/gone.milk", library.paths[0]};
  PresetBrowser browser;
  browser.setSource(library.source(true));
  CHECK(browser.recentTab.isVisible());
  browser.refresh();
  browser.setTab(PresetBrowser::Tab::Recent);
  CHECK(browser.results() == std::vector<int>{3, 0}); // newest first; presets no longer here skipped
}

TEST_CASE("PresetBrowser: row hit areas for the heart and each star", "[ui][PresetBrowser]") {
  const auto layout = ui::PresetBrowserRowLayout::forRow(440, PresetBrowser::rowHeight);
  CHECK(layout.heart.getX() == 0);
  CHECK(layout.heart.getRight() <= layout.text.getX());
  CHECK(layout.text.getRight() <= layout.stars.getX());
  CHECK(layout.stars.getRight() <= 440);
  CHECK(layout.starAt({layout.stars.getX() + 1, 20}) == 1);
  CHECK(layout.starAt({layout.stars.getRight() - 1, 20}) == 5);
  CHECK(layout.starAt({layout.text.getCentreX(), 20}) == 0);
  // Rows are a comfortable touch target (5.8, 7.6).
  CHECK(PresetBrowser::rowHeight >= 32);
}
