// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/engine/PresetMetadataStore.h"

using namespace milkdawp;

namespace {

/// A metadata file in a fresh folder, deleted at the end of the test.
struct TempMetadataFile {
  juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("milkdawp-metadata-" + juce::Uuid().toString());
  juce::File file = folder.getChildFile("preset-metadata.txt");
  TempMetadataFile() = default;
  ~TempMetadataFile() { folder.deleteRecursively(); }
  TempMetadataFile(const TempMetadataFile&) = delete;
  TempMetadataFile& operator=(const TempMetadataFile&) = delete;
  TempMetadataFile(TempMetadataFile&&) = delete;
  TempMetadataFile& operator=(TempMetadataFile&&) = delete;
};

} // namespace

TEST_CASE("A missing metadata file is an empty library", "[engine][PresetMetadataStore]") {
  const TempMetadataFile temp;
  engine::PresetMetadataStore store(temp.file);
  CHECK(store.snapshot()->size() == 0);
  CHECK(store.get("x.milk").isDefault());
}

TEST_CASE("Setting a preset's info writes the file and bumps the generation",
          "[engine][PresetMetadataStore]") {
  const TempMetadataFile temp;
  engine::PresetMetadataStore store(temp.file);
  const auto before = store.generation();
  REQUIRE(store.set("C:/presets/A.milk", {4, false, {"calm"}}));
  CHECK(store.generation() != before);
  CHECK(temp.file.existsAsFile());
  CHECK(temp.file.loadFileAsString().contains("4\t\tcalm\ta.milk"));

  // Another process starting up reads it back.
  engine::PresetMetadataStore other(temp.file);
  CHECK(other.get("D:/elsewhere/a.milk") == core::PresetInfo{4, false, {"calm"}});
  CHECK(other.allTags() == std::vector<std::string>{"calm"});
}

TEST_CASE("An unchanged write leaves the generation alone", "[engine][PresetMetadataStore]") {
  const TempMetadataFile temp;
  engine::PresetMetadataStore store(temp.file);
  store.set("a.milk", {2, false, {}});
  const auto generation = store.generation();
  store.set("a.milk", {2, false, {}});
  CHECK(store.generation() == generation);
}

TEST_CASE("A write keeps another process's edits", "[engine][PresetMetadataStore]") {
  const TempMetadataFile temp;
  engine::PresetMetadataStore app(temp.file);
  engine::PresetMetadataStore plugin(temp.file);
  app.set("one.milk", {5, false, {}});
  plugin.set("two.milk", {1, false, {}}); // rereads first, so one.milk survives
  engine::PresetMetadataStore fresh(temp.file);
  CHECK(fresh.get("one.milk").rating == 5);
  CHECK(fresh.get("two.milk").rating == 1);
}

TEST_CASE("Another process's change is picked up", "[engine][PresetMetadataStore]") {
  const TempMetadataFile temp;
  engine::PresetMetadataStore reader(temp.file);
  CHECK(reader.get("a.milk").isDefault());
  {
    engine::PresetMetadataStore writer(temp.file);
    writer.set("a.milk", {3, true, {}});
  }
  // Checked at most once a second; a write in the same second could share
  // the old modification time, so make it unambiguous.
  temp.file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
  juce::Thread::sleep(1100);
  CHECK(reader.get("a.milk") == core::PresetInfo{3, true, {}});
}

TEST_CASE("shared() is one store per process", "[engine][PresetMetadataStore]") {
  const auto a = engine::PresetMetadataStore::shared();
  const auto b = engine::PresetMetadataStore::shared();
  CHECK(a == b);
  CHECK(a->file() == engine::PresetMetadataStore::defaultFile());
  CHECK(a->file().getFileName() == "preset-metadata.txt");
}
