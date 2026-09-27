// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "milkdawp/engine/PresetLoader.h"

using namespace milkdawp::engine;

namespace {

/// RAII scratch file under the OS temp directory, cleaned up on scope exit
/// regardless of test outcome.
class ScratchFile {
public:
  explicit ScratchFile(const juce::String& content, const juce::String& suffix = ".milk") {
    file_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("mdw_preset_loader_test_" + juce::String(juce::Random::getSystemRandom().nextInt64()) +
                              suffix);
    file_.replaceWithText(content);
  }
  ~ScratchFile() { file_.deleteFile(); }
  ScratchFile(const ScratchFile&) = delete;
  ScratchFile& operator=(const ScratchFile&) = delete;

  [[nodiscard]] const juce::File& file() const { return file_; }

private:
  juce::File file_;
};

} // namespace

TEST_CASE("PresetLoader::validate rejects a nonexistent file", "[engine][PresetLoader]") {
  const juce::File missing = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("mdw_preset_loader_definitely_missing.milk");
  const auto result = PresetLoader::validate(missing);
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.reason.empty());
}

TEST_CASE("PresetLoader::validate rejects an empty file", "[engine][PresetLoader]") {
  const ScratchFile scratch("");
  const auto result = PresetLoader::validate(scratch.file());
  CHECK_FALSE(result.ok);
  CHECK(result.reason.find("empty") != std::string::npos);
}

TEST_CASE("PresetLoader::validate rejects content with no assignment", "[engine][PresetLoader]") {
  const ScratchFile scratch("this is just some prose with no key value pairs at all");
  const auto result = PresetLoader::validate(scratch.file());
  CHECK_FALSE(result.ok);
}

TEST_CASE("PresetLoader::validate accepts plausible .milk-shaped content", "[engine][PresetLoader]") {
  const ScratchFile scratch("fRating=3\nfGammaAdj=2.0\nper_frame_1=wave_r = wave_r + 0.1;\n");
  const auto result = PresetLoader::validate(scratch.file());
  CHECK(result.ok);
  CHECK(result.reason.empty());
}

TEST_CASE("PresetLoader::prefetch reads content and measures time on success", "[engine][PresetLoader]") {
  const juce::String content = "fRating=5\nfGammaAdj=1.0\n";
  const ScratchFile scratch(content);

  // Compare against what the file actually reads back as, not the literal:
  // File::replaceWithText may translate line endings to the platform's
  // native convention on write, and prefetch() reads the file back the same
  // way this does.
  const auto expected = scratch.file().loadFileAsString().toStdString();

  PresetLoader loader;
  const auto result = loader.prefetch(scratch.file());

  CHECK(result.success);
  CHECK(result.reason.empty());
  CHECK(result.contents == expected);
  CHECK_FALSE(loader.isBlacklisted(scratch.file().getFullPathName().toStdString()));
}

TEST_CASE("PresetLoader::prefetch blacklists a file that fails validation", "[engine][PresetLoader]") {
  const ScratchFile scratch("");
  PresetLoader loader;

  const auto path = scratch.file().getFullPathName().toStdString();
  CHECK_FALSE(loader.isBlacklisted(path));

  const auto result = loader.prefetch(scratch.file());
  CHECK_FALSE(result.success);
  CHECK(loader.isBlacklisted(path));
  REQUIRE(loader.blacklistReason(path).has_value());
  CHECK(*loader.blacklistReason(path) == result.reason);
}

TEST_CASE("PresetLoader blacklist can be queried, cleared, and re-cleared entirely", "[engine][PresetLoader]") {
  PresetLoader loader;
  loader.blacklist("a.milk", "reason a");
  loader.blacklist("b.milk", "reason b");
  CHECK(loader.blacklistSize() == 2);
  CHECK(loader.isBlacklisted("a.milk"));

  loader.clearBlacklistEntry("a.milk");
  CHECK_FALSE(loader.isBlacklisted("a.milk"));
  CHECK(loader.isBlacklisted("b.milk"));
  CHECK(loader.blacklistSize() == 1);

  loader.clearBlacklist();
  CHECK(loader.blacklistSize() == 0);
  CHECK_FALSE(loader.isBlacklisted("b.milk"));
}

TEST_CASE("PresetLoader::blacklistedPaths lists every blacklisted path", "[engine][PresetLoader]") {
  PresetLoader loader;
  CHECK(loader.blacklistedPaths().empty());
  loader.blacklist("a.milk", "reason a");
  loader.blacklist("b.milk", "reason b");
  auto paths = loader.blacklistedPaths();
  std::sort(paths.begin(), paths.end());
  CHECK(paths == std::vector<std::string>{"a.milk", "b.milk"});
  loader.clearBlacklistEntry("a.milk");
  CHECK(loader.blacklistedPaths() == std::vector<std::string>{"b.milk"});
}
