// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 6.7: the opt-in update check. Never goes online: the checker gets a fake
// fetcher.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

#include <juce_core/juce_core.h>

#include "milkdawp/core/Version.h"
#include "milkdawp/engine/UpdateCheck.h"

using namespace milkdawp::engine;
using namespace std::chrono_literals;

namespace {

// Newest first, like GitHub's API.
const char* const kReleases = R"([
  {"tag_name": "v2.2.0", "draft": true,  "prerelease": false, "html_url": "https://example/draft"},
  {"tag_name": "v2.1.0-beta.1", "draft": false, "prerelease": true, "html_url": "https://example/2.1.0-beta.1"},
  {"tag_name": "v2.0.1", "draft": false, "prerelease": false, "html_url": "https://example/2.0.1"},
  {"tag_name": "v2.0.0", "draft": false, "prerelease": false, "html_url": "https://example/2.0.0"},
  {"tag_name": "v2.0.0-beta.2", "draft": false, "prerelease": true, "html_url": "https://example/2.0.0-beta.2"}
])";

struct TempFile {
  juce::TemporaryFile holder{".json"};
  juce::File file{holder.getFile()};
  ~TempFile() { file.deleteFile(); }
};

UpdateChecker::Status waitForResult(UpdateChecker& checker) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (checker.status().state == UpdateChecker::State::Checking && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  return checker.status();
}

} // namespace

TEST_CASE("Update check: versions order like semver", "[engine][update]") {
  CHECK(compareVersions("2.0.0", "2.0.0") == 0);
  CHECK(compareVersions("v2.0.1", "2.0.0") > 0);
  CHECK(compareVersions("2.0.0", "2.0.1") < 0);
  CHECK(compareVersions("2.1.0", "2.0.9") > 0);
  CHECK(compareVersions("10.0.0", "9.9.9") > 0);
  CHECK(compareVersions("2.0.0-beta.1", "2.0.0") < 0); // a pre-release comes before its release
  CHECK(compareVersions("2.0.0", "2.0.0-rc.1") > 0);
  CHECK(compareVersions("2.0.0-beta.10", "2.0.0-beta.9") > 0); // numerically
  CHECK(compareVersions("2.0.0-rc.1", "2.0.0-beta.5") > 0);    // "rc" > "beta"
  CHECK(compareVersions("2.0.0-beta", "2.0.0-beta.1") < 0);
  CHECK(isPreRelease("2.0.0-beta.1"));
  CHECK_FALSE(isPreRelease("v2.0.0"));
}

TEST_CASE("Update check: a stable user hears only about newer stable releases", "[engine][update]") {
  const auto newer = newerRelease(kReleases, "2.0.0");
  REQUIRE(newer.has_value());
  CHECK(newer->version == "2.0.1");
  CHECK(newer->url == "https://example/2.0.1");

  CHECK_FALSE(newerRelease(kReleases, "2.0.1").has_value()); // the beta and the draft don't count
  CHECK_FALSE(newerRelease(kReleases, "2.3.0").has_value());
}

TEST_CASE("Update check: a beta tester hears about the next pre-release too", "[engine][update]") {
  const auto newer = newerRelease(kReleases, "2.0.0-beta.1");
  REQUIRE(newer.has_value());
  CHECK(newer->version == "2.1.0-beta.1");
  CHECK(newer->preRelease);
}

TEST_CASE("Update check: junk from the network finds nothing", "[engine][update]") {
  CHECK_FALSE(newerRelease("", "2.0.0").has_value());
  CHECK_FALSE(newerRelease("{\"message\": \"API rate limit exceeded\"}", "2.0.0").has_value());
  CHECK_FALSE(newerRelease("[{\"name\": \"no tag\"}]", "2.0.0").has_value());
}

TEST_CASE("Update check: settings round-trip, and a check is due once a day when enabled", "[engine][update]") {
  TempFile temp;
  UpdateSettings settings;
  CHECK_FALSE(settings.isDue(juce::Time::currentTimeMillis())); // off by default
  settings.enabled = true;
  settings.lastCheckMs = 1000;
  settings.latestVersion = "2.0.1";
  settings.latestUrl = "https://example/2.0.1";
  REQUIRE(settings.save(temp.file));

  const auto loaded = UpdateSettings::load(temp.file);
  CHECK(loaded.enabled);
  CHECK(loaded.lastCheckMs == 1000);
  CHECK(loaded.latestVersion == "2.0.1");
  CHECK(loaded.latestUrl == "https://example/2.0.1");

  constexpr juce::int64 day = 24LL * 60 * 60 * 1000;
  CHECK(loaded.isDue(1000 + day));
  CHECK_FALSE(loaded.isDue(1000 + day - 1));

  CHECK_FALSE(UpdateSettings::load(temp.file.getSiblingFile("missing.json")).enabled);
}

TEST_CASE("Update check: the checker is off until enabled, and checks only when due", "[engine][update]") {
  TempFile temp;
  int fetches = 0;
  UpdateChecker checker(temp.file, [&fetches](std::string&) {
    ++fetches;
    return juce::String("[]");
  });
  CHECK(checker.status().state == UpdateChecker::State::Off);
  checker.checkIfDue();
  CHECK(fetches == 0); // off: never goes online by itself

  checker.setEnabled(true); // never checked before, so due now
  CHECK(waitForResult(checker).state == UpdateChecker::State::UpToDate);
  CHECK(fetches == 1);
  checker.checkIfDue(); // checked a moment ago
  CHECK(fetches == 1);
  CHECK(UpdateSettings::load(temp.file).enabled);
}

TEST_CASE("Update check: a newer release is reported and remembered", "[engine][update]") {
  TempFile temp;
  // A release newer than this build, whatever its version is.
  const auto v = milkdawp::core::version();
  const auto next = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch + 1);
  const juce::String json = "[{\"tag_name\": \"v" + juce::String(next) +
                            "\", \"draft\": false, \"prerelease\": false, \"html_url\": \"https://example/next\"}]";
  {
    UpdateChecker checker(temp.file, [json](std::string&) { return json; });
    checker.checkNow(); // works even with automatic checks off
    const auto status = waitForResult(checker);
    REQUIRE(status.state == UpdateChecker::State::Available);
    CHECK(status.version == next);
    CHECK(status.url == "https://example/next");
    CHECK(describeUpdateStatus(status).contains(next));
  }
  // The plugin (and the next app launch) read it from the file.
  const auto saved = UpdateSettings::load(temp.file);
  CHECK(saved.latestVersion == next);
  UpdateChecker reopened(temp.file, [](std::string&) { return juce::String("[]"); });
  CHECK(reopened.status().state == UpdateChecker::State::Available);
}

TEST_CASE("Update check: a failed fetch says why", "[engine][update]") {
  TempFile temp;
  UpdateChecker checker(temp.file, [](std::string& error) {
    error = "couldn't reach GitHub";
    return juce::String();
  });
  checker.checkNow();
  const auto status = waitForResult(checker);
  CHECK(status.state == UpdateChecker::State::Failed);
  CHECK(status.error == "couldn't reach GitHub");
  CHECK(describeUpdateStatus(status).contains("couldn't reach GitHub"));
  CHECK(UpdateSettings::load(temp.file).lastCheckMs == 0); // a failure doesn't count as a check
}
