// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <deque>
#include <memory>
#include <mutex>

#include <juce_core/juce_core.h>

namespace milkdawp::app {

/// The app's logger (4.10): every line is timestamped and kept in memory
/// (the last `capacity` lines), and also written to the log file while one
/// is set. File logging stays optional (File > Write a log file), but a crash
/// report or a "Collect logs" bundle always has the recent lines.
///
/// Thread-safe: JUCE code logs from any thread.
class RecentLog final : public juce::Logger {
public:
  explicit RecentLog(int capacity = 500);

  /// Starts (non-null) or stops (null) writing to a log file.
  void setFileLogger(std::unique_ptr<juce::FileLogger> logger);
  [[nodiscard]] bool isWritingToFile() const;

  /// The kept lines, oldest first.
  [[nodiscard]] juce::StringArray lines() const;
  /// For the crash handler: the same as `lines()`, but returns nothing
  /// instead of waiting when another thread holds the lock. A crash on a
  /// thread that was logging can't deadlock the report.
  [[nodiscard]] juce::StringArray tryGetLines() const;

  /// "HH:MM:SS.mmm  message", the form both the memory and the file get.
  [[nodiscard]] static juce::String formatLine(juce::Time time, const juce::String& message);

protected:
  void logMessage(const juce::String& message) override;

private:
  mutable std::mutex mutex_;
  std::deque<juce::String> lines_;
  int capacity_;
  std::unique_ptr<juce::FileLogger> file_;
};

} // namespace milkdawp::app
