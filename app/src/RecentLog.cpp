// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "RecentLog.h"

#include <algorithm>
#include <utility>

namespace milkdawp::app {

RecentLog::RecentLog(int capacity) : capacity_(std::max(1, capacity)) {}

void RecentLog::setFileLogger(std::unique_ptr<juce::FileLogger> logger) {
  std::unique_ptr<juce::FileLogger> old;
  {
    const std::lock_guard lock(mutex_);
    old = std::exchange(file_, std::move(logger));
  }
  // `old` closes its file here, outside the lock.
}

bool RecentLog::isWritingToFile() const {
  const std::lock_guard lock(mutex_);
  return file_ != nullptr;
}

juce::StringArray RecentLog::lines() const {
  const std::lock_guard lock(mutex_);
  juce::StringArray result;
  for (const auto& line : lines_) {
    result.add(line);
  }
  return result;
}

juce::StringArray RecentLog::tryGetLines() const {
  const std::unique_lock lock(mutex_, std::try_to_lock);
  juce::StringArray result;
  if (lock.owns_lock()) {
    for (const auto& line : lines_) {
      result.add(line);
    }
  }
  return result;
}

juce::String RecentLog::formatLine(juce::Time time, const juce::String& message) {
  return time.formatted("%H:%M:%S.") + juce::String(time.getMilliseconds()).paddedLeft('0', 3) +
         "  " + message;
}

void RecentLog::logMessage(const juce::String& message) {
  const auto line = formatLine(juce::Time::getCurrentTime(), message);
  const std::lock_guard lock(mutex_);
  lines_.push_back(line);
  while (static_cast<int>(lines_.size()) > capacity_) {
    lines_.pop_front();
  }
  if (file_ != nullptr) {
    file_->logMessage(line);
  }
}

} // namespace milkdawp::app
