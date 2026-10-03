// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "CrashReporter.h"

using namespace milkdawp;

namespace {

/// A fresh folder, deleted at the end of the test.
struct TempFolder {
  juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("milkdawp-crash-" + juce::Uuid().toString());
  TempFolder() = default;
  ~TempFolder() { folder.deleteRecursively(); }
  TempFolder(const TempFolder&) = delete;
  TempFolder& operator=(const TempFolder&) = delete;
  TempFolder(TempFolder&&) = delete;
  TempFolder& operator=(TempFolder&&) = delete;
};

} // namespace

TEST_CASE("Crash report names sort by time", "[app][crash]") {
  const juce::Time time(2026, 9, 3, 14, 5, 9); // months are 0-based: October
  CHECK(app::CrashReporter::reportStem(time) == "crash-2026-10-03_14-05-09");
}

TEST_CASE("A report has the reason, system, stack and recent log", "[app][crash]") {
  const TempFolder temp;
  const app::CrashReporter reporter(temp.folder, "MilkDAWp 9.9.9");
  const juce::Time time(2026, 9, 3, 14, 5, 9);
  const auto report =
      reporter.writeReport(time, "test crash", {"first line", "last line"}, nullptr);

  CHECK(report.getFileName() == "crash-2026-10-03_14-05-09.txt");
  const auto text = report.loadFileAsString();
  CHECK(text.startsWith("MilkDAWp 9.9.9 crashed"));
  CHECK(text.contains("Reason: test crash"));
  CHECK(text.contains("OS: "));
  CHECK(text.contains("Stack:"));
  CHECK(text.contains("Recent log (2 lines):\nfirst line\nlast line"));
#if JUCE_WINDOWS
  // A minidump of this process, even without exception information.
  CHECK(report.withFileExtension("dmp").getSize() > 0);
  CHECK(text.contains("Minidump: crash-2026-10-03_14-05-09.dmp"));
#endif
  CHECK(reporter.reports() == juce::Array<juce::File>{report});
}

TEST_CASE("A clean shutdown is not reported as a crash", "[app][crash]") {
  const TempFolder temp;
  {
    app::CrashReporter reporter(temp.folder, "MilkDAWp");
    CHECK(reporter.beginSession().endedCleanly);
    CHECK(temp.folder.getChildFile(app::CrashReporter::kMarkerFileName).existsAsFile());
    reporter.endSession();
  }
  app::CrashReporter next(temp.folder, "MilkDAWp");
  const auto previous = next.beginSession();
  CHECK(previous.endedCleanly);
  CHECK(previous.crashReport == juce::File());
}

namespace {

/// A report from some earlier session, an hour before this one started.
juce::File writeOldReport(const juce::File& folder) {
  const app::CrashReporter earlier(folder, "MilkDAWp");
  auto old = earlier.writeReport(juce::Time(2026, 0, 1, 0, 0, 0), "old", {}, nullptr);
  old.setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::hours(1));
  return old;
}

} // namespace

TEST_CASE("The next session finds the report the crashed one left", "[app][crash]") {
  const TempFolder temp;
  writeOldReport(temp.folder);
  juce::File report;
  {
    app::CrashReporter crashed(temp.folder, "MilkDAWp");
    crashed.beginSession();
    report = crashed.writeReport(juce::Time::getCurrentTime(), "boom", {}, nullptr);
    // No endSession(): the process died.
  }
  app::CrashReporter next(temp.folder, "MilkDAWp");
  const auto previous = next.beginSession();
  CHECK_FALSE(previous.endedCleanly);
  CHECK(previous.crashReport == report);
}

TEST_CASE("A session that died without a report finds no crash report", "[app][crash]") {
  const TempFolder temp;
  writeOldReport(temp.folder); // older than the session: not its report
  {
    app::CrashReporter killed(temp.folder, "MilkDAWp");
    killed.beginSession();
  }
  app::CrashReporter next(temp.folder, "MilkDAWp");
  const auto previous = next.beginSession();
  CHECK_FALSE(previous.endedCleanly);
  CHECK(previous.crashReport == juce::File());
}

TEST_CASE("Starting a session keeps only the newest reports", "[app][crash]") {
  const TempFolder temp;
  app::CrashReporter reporter(temp.folder, "MilkDAWp");
  for (int day = 1; day <= 5; ++day) {
    reporter.writeReport(juce::Time(2026, 0, day, 12, 0, 0), "crash", {}, nullptr);
  }
  reporter.beginSession(2);
  const auto kept = reporter.reports();
  REQUIRE(kept.size() == 2);
  CHECK(kept[0].getFileName() == "crash-2026-01-05_12-00-00.txt");
  CHECK(kept[1].getFileName() == "crash-2026-01-04_12-00-00.txt");
  CHECK_FALSE(temp.folder.getChildFile("crash-2026-01-03_12-00-00.dmp").exists());
}
