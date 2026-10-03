// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "RecentLog.h"

using namespace milkdawp;

namespace {

/// Installs `log` as JUCE's logger for one test.
struct ScopedLogger {
  explicit ScopedLogger(juce::Logger& log) { juce::Logger::setCurrentLogger(&log); }
  ~ScopedLogger() { juce::Logger::setCurrentLogger(nullptr); }
  ScopedLogger(const ScopedLogger&) = delete;
  ScopedLogger& operator=(const ScopedLogger&) = delete;
  ScopedLogger(ScopedLogger&&) = delete;
  ScopedLogger& operator=(ScopedLogger&&) = delete;
};

juce::File tempFolder() {
  return juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getChildFile("milkdawp-recentlog-" + juce::Uuid().toString());
}

} // namespace

TEST_CASE("Log lines are timestamped", "[app][log]") {
  const juce::Time time(2026, 9, 3, 14, 5, 9, 42); // months are 0-based: October
  CHECK(app::RecentLog::formatLine(time, "hello") == "14:05:09.042  hello");
}

TEST_CASE("The recent log keeps only the newest lines", "[app][log]") {
  app::RecentLog log(3);
  const ScopedLogger scoped(log);
  for (int i = 1; i <= 5; ++i) {
    juce::Logger::writeToLog("line " + juce::String(i));
  }
  const auto lines = log.lines();
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].endsWith("  line 3"));
  CHECK(lines[2].endsWith("  line 5"));
  CHECK(log.tryGetLines() == lines);
}

TEST_CASE("The recent log writes to a file only while one is set", "[app][log]") {
  const auto folder = tempFolder();
  const auto file = folder.getChildFile("test.log");
  {
    app::RecentLog log;
    const ScopedLogger scoped(log);
    juce::Logger::writeToLog("before");
    CHECK_FALSE(log.isWritingToFile());
    log.setFileLogger(std::make_unique<juce::FileLogger>(file, "welcome"));
    CHECK(log.isWritingToFile());
    juce::Logger::writeToLog("during");
    log.setFileLogger(nullptr);
    juce::Logger::writeToLog("after");
    CHECK(log.lines().size() == 3);
  }
  const auto text = file.loadFileAsString();
  CHECK(text.contains("  during"));
  CHECK_FALSE(text.contains("before"));
  CHECK_FALSE(text.contains("after"));
  folder.deleteRecursively();
}
