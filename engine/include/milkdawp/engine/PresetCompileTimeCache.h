// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace milkdawp::engine {

/// Per-preset load time (projectM parse + shader compile, as measured on the
/// render thread and published through RenderStats::lastPresetLoadMs),
/// learned over the session and keyed by absolute path so a folder rescan
/// doesn't lose it. Used to steer hard-cut selection away from expensive
/// presets (§7 Phase 5.4): a hard cut has no crossfade to hide a slow
/// compile, unlike a soft/timed one.
///
/// Director-thread only (no locking): last-measured value per path, which is
/// enough to avoid a preset already known to be slow without the complexity
/// of a running average.
class PresetCompileTimeCache {
public:
  void record(const std::string& absolutePath, float loadTimeMs) noexcept {
    if (!absolutePath.empty() && loadTimeMs >= 0.0f) {
      times_[absolutePath] = loadTimeMs;
    }
  }

  /// The last-measured load time for `absolutePath`, or nullopt if this
  /// session has never measured it.
  [[nodiscard]] std::optional<float> estimateMs(const std::string& absolutePath) const {
    const auto it = times_.find(absolutePath);
    if (it == times_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] std::size_t size() const noexcept { return times_.size(); }

private:
  std::unordered_map<std::string, float> times_;
};

} // namespace milkdawp::engine
