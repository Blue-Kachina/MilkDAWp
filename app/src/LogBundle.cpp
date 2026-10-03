// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "LogBundle.h"

#include <algorithm>

namespace milkdawp::app {

void LogBundle::addCrashReports(const juce::Array<juce::File>& reportsNewestFirst, int count) {
  for (int i = 0; i < std::min(count, reportsNewestFirst.size()); ++i) {
    const auto& report = reportsNewestFirst.getReference(i);
    files.add({report, "crashes/" + report.getFileName()});
    const auto dump = report.withFileExtension("dmp");
    files.add({dump, "crashes/" + dump.getFileName()});
  }
}

juce::Result writeLogBundle(const LogBundle& bundle, const juce::File& zipFile) {
  constexpr int kCompression = 9;
  juce::ZipFile::Builder builder;
  juce::StringArray used;
  for (const auto& entry : bundle.files) {
    if (!entry.file.existsAsFile()) {
      continue;
    }
    const auto path = entry.path.isNotEmpty() ? entry.path : entry.file.getFileName();
    if (used.contains(path)) {
      continue; // the same file named twice
    }
    used.add(path);
    builder.addFile(entry.file, kCompression, path);
  }
  builder.addEntry(std::make_unique<juce::MemoryInputStream>(bundle.diagnostics.toRawUTF8(),
                                                             bundle.diagnostics.getNumBytesAsUTF8(),
                                                             true),
                   kCompression,
                   "diagnostics.txt",
                   juce::Time::getCurrentTime());

  if (!zipFile.getParentDirectory().createDirectory()) {
    return juce::Result::fail("Can't create " + zipFile.getParentDirectory().getFullPathName());
  }
  const juce::TemporaryFile temp(zipFile);
  {
    juce::FileOutputStream out(temp.getFile());
    if (!out.openedOk()) {
      return juce::Result::fail("Can't write " + temp.getFile().getFullPathName());
    }
    if (!builder.writeToStream(out, nullptr)) {
      return juce::Result::fail("Writing the zip failed");
    }
    out.flush();
    if (out.getStatus().failed()) {
      return out.getStatus();
    }
  }
  if (!temp.overwriteTargetFileWithTemporary()) {
    return juce::Result::fail("Can't replace " + zipFile.getFullPathName());
  }
  return juce::Result::ok();
}

juce::String defaultLogBundleName(const juce::String& appName, juce::Time time) {
  return appName + " logs " + time.formatted("%Y-%m-%d %H-%M") + ".zip";
}

} // namespace milkdawp::app
