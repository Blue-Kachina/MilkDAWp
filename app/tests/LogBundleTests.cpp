// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "LogBundle.h"

using namespace milkdawp;

namespace {

juce::String entryText(juce::ZipFile& zip, const juce::String& path) {
  const auto index = zip.getIndexOfFileName(path);
  if (index < 0) {
    return {};
  }
  const std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
  return stream != nullptr ? stream->readEntireStreamAsString() : juce::String();
}

} // namespace

TEST_CASE("The default bundle name carries the date", "[app][logbundle]") {
  CHECK(app::defaultLogBundleName("MilkDAWp", juce::Time(2026, 9, 3, 14, 5, 9)) ==
        "MilkDAWp logs 2026-10-03 14-05.zip");
}

TEST_CASE("A log bundle zips the files that exist and the diagnostics", "[app][logbundle]") {
  const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("milkdawp-bundle-" + juce::Uuid().toString());
  folder.createDirectory();
  const auto log = folder.getChildFile("MilkDAWp.log");
  log.replaceWithText("log text");
  const auto report = folder.getChildFile("crash-2026-10-03_14-05-09.txt");
  report.replaceWithText("report text"); // no .dmp beside it: skipped

  app::LogBundle bundle;
  bundle.files.add({log, {}});
  bundle.files.add({log, {}}); // twice: stored once
  bundle.files.add({folder.getChildFile("missing.settings"), {}});
  bundle.addCrashReports({report}, 3);
  bundle.diagnostics = "diagnostics text";

  const auto zipFile = folder.getChildFile("out/bundle.zip");
  REQUIRE(app::writeLogBundle(bundle, zipFile).wasOk());

  juce::ZipFile zip(zipFile);
  CHECK(zip.getNumEntries() == 3);
  CHECK(entryText(zip, "MilkDAWp.log") == "log text");
  CHECK(entryText(zip, "crashes/crash-2026-10-03_14-05-09.txt") == "report text");
  CHECK(entryText(zip, "diagnostics.txt") == "diagnostics text");
  folder.deleteRecursively();
}
