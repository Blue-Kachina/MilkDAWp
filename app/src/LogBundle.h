// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

namespace milkdawp::app {

/// File > Collect logs... (4.10): one zip with everything a bug report
/// needs, written where the user chooses. Nothing is sent anywhere.
struct LogBundle {
  struct Entry {
    juce::File file;
    juce::String path; // inside the zip; empty: the file's own name
  };

  /// Copied as they are. Missing ones are skipped, so an entry can name a
  /// file that may not exist (the log when logging was never on).
  juce::Array<Entry> files;
  /// Written as "diagnostics.txt": versions, system, engine state, recent log.
  juce::String diagnostics;

  /// Adds the newest `count` crash reports (text and minidump) under "crashes/".
  void addCrashReports(const juce::Array<juce::File>& reportsNewestFirst, int count);
};

/// Writes `bundle` to `zipFile`, replacing it. The zip is written to a
/// temporary file first, so a failure leaves any existing file untouched.
[[nodiscard]] juce::Result writeLogBundle(const LogBundle& bundle, const juce::File& zipFile);

/// "MilkDAWp logs 2026-10-03 14-05.zip".
[[nodiscard]] juce::String defaultLogBundleName(const juce::String& appName, juce::Time time);

} // namespace milkdawp::app
