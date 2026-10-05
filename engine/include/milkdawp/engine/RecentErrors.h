// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "milkdawp/core/Diagnostics.h"

namespace milkdawp::engine {

/// The last few errors one engine ran into (5.9's diagnostics panel): preset
/// files that couldn't be read or that projectM rejected, and projectM's own
/// error log lines (shader compile errors and the like). Written by the
/// director and render threads, read by the UI. Takes a mutex, so never from
/// the audio thread; errors are rare, so it never contends in practice.
class RecentErrors {
public:
  static constexpr std::size_t kCapacity = 20;
  /// Longer messages are cut (projectM can log a whole shader).
  static constexpr std::size_t kMaxMessageLength = 400;

  void add(std::string source, std::string message) {
    if (message.size() > kMaxMessageLength) {
      message.resize(kMaxMessageLength);
      message += "...";
    }
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
      message.pop_back();
    }
    const auto now = juce::Time::currentTimeMillis();
    const std::lock_guard lock(mutex_);
    if (!entries_.empty() && entries_.back().source == source && entries_.back().message == message) {
      ++entries_.back().count;
      entries_.back().lastTimeMs = now;
      return;
    }
    if (entries_.size() == kCapacity) {
      entries_.pop_front();
    }
    entries_.push_back({now, std::move(source), std::move(message), 1});
  }

  /// Oldest first.
  [[nodiscard]] std::vector<core::DiagnosticsError> snapshot() const {
    const std::lock_guard lock(mutex_);
    return {entries_.begin(), entries_.end()};
  }

  void clear() {
    const std::lock_guard lock(mutex_);
    entries_.clear();
  }

private:
  mutable std::mutex mutex_;
  std::deque<core::DiagnosticsError> entries_;
};

} // namespace milkdawp::engine
