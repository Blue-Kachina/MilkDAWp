// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 6.1: finding the bundled content, and the library code staying fast with
// the real ~10k-preset pack. The scale tests run wherever the build fetched
// the pack (MILKDAWP_BUNDLE_CONTENT, on by default, so in CI too) and skip
// otherwise. Their bounds are generous: they catch something going quadratic,
// not a slow runner. The timings are printed for the roadmap.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <thread>

#include <juce_core/juce_core.h>

#include "milkdawp/core/Playlist.h"
#include "milkdawp/core/PresetMetadata.h"
#include "milkdawp/engine/BundledContent.h"
#include "milkdawp/engine/PresetMetadataStore.h"
#include "milkdawp/engine/Visualizer.h"

using namespace milkdawp;
using namespace std::chrono_literals;

namespace {

void setEnvironmentVariable(const char* name, const juce::String& value) {
#if JUCE_WINDOWS
  _putenv_s(name, value.toRawUTF8()); // an empty value removes it
#else
  if (value.isEmpty()) {
    unsetenv(name);
  } else {
    setenv(name, value.toRawUTF8(), 1);
  }
#endif
}

/// Points MILKDAWP_CONTENT_DIR somewhere for one test, then restores it.
struct ScopedContentDir {
  juce::String previous = juce::SystemStats::getEnvironmentVariable("MILKDAWP_CONTENT_DIR", {});
  explicit ScopedContentDir(const juce::File& dir) { setEnvironmentVariable("MILKDAWP_CONTENT_DIR", dir.getFullPathName()); }
  ~ScopedContentDir() { setEnvironmentVariable("MILKDAWP_CONTENT_DIR", previous); }
};

struct TempDir {
  juce::TemporaryFile holder;
  juce::File dir{holder.getFile()};
  TempDir() { dir.createDirectory(); }
  ~TempDir() { dir.deleteRecursively(); }
};

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

/// The pack the build fetched, if any (MILKDAWP_TEST_CONTENT_DIR).
std::optional<engine::BundledContent> fetchedPack() {
#ifdef MILKDAWP_TEST_CONTENT_DIR
  const juce::File root(MILKDAWP_TEST_CONTENT_DIR);
  if (engine::BundledContent::isContentRoot(root)) {
    return engine::BundledContent{root};
  }
#endif
  return std::nullopt;
}

} // namespace

TEST_CASE("BundledContent: a folder is a content root only with its preset folder", "[engine][content]") {
  TempDir temp;
  CHECK_FALSE(engine::BundledContent::isContentRoot(temp.dir));
  CHECK_FALSE(engine::BundledContent::isContentRoot(juce::File()));
  temp.dir.getChildFile("Presets/Cream of the Crop").createDirectory();
  CHECK(engine::BundledContent::isContentRoot(temp.dir));
}

TEST_CASE("BundledContent: MILKDAWP_CONTENT_DIR comes first and is found", "[engine][content]") {
  TempDir temp;
  const engine::BundledContent content{temp.dir};
  content.presetFolder().createDirectory();
  const ScopedContentDir scoped(temp.dir);

  const auto roots = engine::BundledContent::candidateRoots();
  REQUIRE_FALSE(roots.empty());
  CHECK(roots.front() == temp.dir);

  const auto found = engine::BundledContent::find();
  REQUIRE(found.has_value());
  CHECK(found->root == temp.dir);
}

TEST_CASE("BundledContent: default preset and textures only when present", "[engine][content]") {
  TempDir temp;
  const engine::BundledContent content{temp.dir};
  content.presetFolder().createDirectory();
  CHECK(content.defaultPreset() == juce::File());
  CHECK(content.textureSearchPaths().empty());

  const auto preset = content.presetFolder().getChildFile(engine::BundledContent::kDefaultPresetRelativePath);
  preset.create();
  content.texturesFolder().createDirectory();
  CHECK(content.defaultPreset() == preset);
  REQUIRE(content.textureSearchPaths().size() == 1);
  CHECK(juce::File(content.textureSearchPaths().front()) == content.texturesFolder());
}

TEST_CASE("BundledContent: the fetched pack has the default preset and textures", "[engine][content]") {
  const auto pack = fetchedPack();
  if (!pack) {
    SKIP("built without the bundled content (MILKDAWP_BUNDLE_CONTENT=OFF)");
  }
  CHECK(pack->defaultPreset().existsAsFile());
  CHECK(pack->texturesFolder().getNumberOfChildFiles(juce::File::findFiles, "*.jpg;*.png") > 50);
  CHECK(pack->presetFolder().getChildFile("LICENSE.md").existsAsFile());
}

TEST_CASE("BundledContent: scan, Weighted shuffle and metadata stay fast at ~10k presets", "[engine][content][scale]") {
  const auto pack = fetchedPack();
  if (!pack) {
    SKIP("built without the bundled content (MILKDAWP_BUNDLE_CONTENT=OFF)");
  }
  const auto folder = pack->presetFolder().getFullPathName().toStdString();

  auto start = std::chrono::steady_clock::now();
  auto entries = core::Playlist::scanFolder(folder);
  const auto scanSeconds = secondsSince(start);
  REQUIRE(entries.size() > 9000);
  CHECK(scanSeconds < 10.0);

  // Rate every fifth preset (1-5 stars) and keep every seventh out of
  // shuffle, then build the weights the way the director does.
  core::PresetMetadata metadata;
  for (std::size_t i = 0; i < entries.size(); i += 5) {
    core::PresetInfo info;
    info.rating = static_cast<int>(i / 5 % 5) + 1;
    info.neverAutoSelect = i % 7 == 0;
    metadata.set(entries[i].absolutePath, info);
  }
  start = std::chrono::steady_clock::now();
  core::Playlist playlist(std::move(entries));
  for (std::size_t i = 0; i < playlist.size(); ++i) {
    const auto info = metadata.get(playlist.at(i).absolutePath);
    playlist.setSelectionInfo(i, core::PresetMetadata::weightFor(info), !info.neverAutoSelect);
  }
  const auto selectionSeconds = secondsSince(start);
  CHECK(selectionSeconds < 2.0);

  playlist.setPolicy(core::PlaylistPolicy::Weighted);
  std::mt19937 rng(1234);
  start = std::chrono::steady_clock::now();
  constexpr int kAdvances = 500;
  for (int i = 0; i < kAdvances; ++i) {
    const auto index = playlist.advanceNext(rng);
    REQUIRE(playlist.at(index).autoSelect);
  }
  const auto advanceSeconds = secondsSince(start);
  // Linear per pick (one pass and a discrete_distribution over the
  // candidates): ~0.1 ms optimized, a few ms in a Debug build. Quadratic
  // would be seconds.
  CHECK(advanceSeconds / kAdvances < 0.02);

  // The metadata file round trip a launch does with this many entries.
  start = std::chrono::steady_clock::now();
  const auto parsed = core::PresetMetadata::parse(metadata.serialize());
  const auto metadataSeconds = secondsSince(start);
  CHECK(parsed.size() == metadata.size());
  CHECK(metadataSeconds < 2.0);

  std::printf("6.1 scale (%zu presets): scan %.3f s, selection pass %.3f s, %d Weighted picks %.3f s "
              "(%.1f us each), metadata round trip (%zu entries) %.3f s\n",
              playlist.size(), scanSeconds, selectionSeconds, kAdvances, advanceSeconds,
              advanceSeconds * 1e6 / kAdvances, metadata.size(), metadataSeconds);
}

TEST_CASE("BundledContent: the director takes the whole pack as its library", "[engine][content][scale]") {
  const auto pack = fetchedPack();
  if (!pack) {
    SKIP("built without the bundled content (MILKDAWP_BUNDLE_CONTENT=OFF)");
  }
  engine::Visualizer visualizer(engine::Visualizer::Config{});
  auto& director = visualizer.director();

  const auto start = std::chrono::steady_clock::now();
  director.setPresetFolder(pack->presetFolder().getFullPathName().toStdString(),
                           pack->defaultPreset().getFullPathName().toStdString());
  const auto deadline = start + 30s;
  while ((director.status().playlistSize == 0 || director.status().currentIndex < 0) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  const auto seconds = secondsSince(start);
  REQUIRE(director.status().playlistSize > 9000);
  CHECK(seconds < 10.0);
  // It starts on the default preset, not the first file in the pack.
  CHECK(juce::File(director.currentPresetPath()) == pack->defaultPreset());
  std::printf("6.1 scale: director library ready in %.3f s (%u presets)\n", seconds, director.status().playlistSize);
}
