// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

namespace milkdawp::app {

class RecentLog;

/// Local crash reports (4.10). Nothing is uploaded: a crash leaves a report
/// in `folder`, and File > Collect logs... (`LogBundle`) zips it with the log
/// for the user to attach to an issue.
///
/// A report is "crash-<date>_<time>.txt" (reason, system, stack trace, the
/// recent log lines) plus, on Windows, "crash-<date>_<time>.dmp", a
/// minidump. On macOS/Linux the text report is written from a signal
/// handler, so it is best effort: the process is already broken.
///
/// A marker file is kept in `folder` while the app runs. The next launch
/// finding it means the last session didn't shut down cleanly, and a report
/// newer than it means that session crashed.
class CrashReporter {
public:
  /// `appDescription`: name and version, the report's first line.
  CrashReporter(juce::File folder, juce::String appDescription);
  ~CrashReporter();
  CrashReporter(const CrashReporter&) = delete;
  CrashReporter& operator=(const CrashReporter&) = delete;
  CrashReporter(CrashReporter&&) = delete;
  CrashReporter& operator=(CrashReporter&&) = delete;

  struct PreviousSession {
    bool endedCleanly = true; // false: the marker was still there
    juce::File crashReport;   // the report that session left, if it crashed
  };

  /// Call once at startup. Reports on the previous session, then writes this
  /// session's marker and deletes all but the newest `keepReports` reports.
  PreviousSession beginSession(int keepReports = 10);
  /// A clean shutdown: removes the marker.
  void endSession();

  /// Makes this the process's crash handler: unhandled exceptions (Windows),
  /// fatal signals (macOS/Linux) and `std::terminate`. Reports include the
  /// lines `log` holds. One reporter per process; it and `log` must outlive
  /// the handler, which is uninstalled when this reporter is destroyed.
  void install(const RecentLog& log);

  /// What the handler does. Public for tests. `crashInfo` is what JUCE's
  /// crash handler gets (`EXCEPTION_POINTERS*` on Windows, the signal
  /// number otherwise), or null. Returns the text report.
  juce::File writeReport(juce::Time time,
                         const juce::String& reason,
                         const juce::StringArray& recentLines,
                         void* crashInfo) const;

  [[nodiscard]] const juce::File& folder() const noexcept { return folder_; }
  /// The text reports in `folder`, newest first.
  [[nodiscard]] juce::Array<juce::File> reports() const;

  /// "crash-2026-10-03_14-05-09": a report's file name without extension.
  [[nodiscard]] static juce::String reportStem(juce::Time time);
  /// OS, CPU and memory, one item per line.
  [[nodiscard]] static juce::String describeSystem();

  static constexpr const char* kMarkerFileName = "session.running";

private:
  juce::File folder_;
  juce::String appDescription_;
};

} // namespace milkdawp::app
