// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace milkdawp::core {

/// One preset reference in a Playlist. `contentHash`/`libraryRoot` support
/// relinking a moved preset folder (§4.8); left empty until Phase 1.14 wires
/// up PresetLibrary -- Playlist itself only needs the paths.
struct PlaylistEntry {
  std::string absolutePath;
  std::string relativePath; // relative to the scanned root; used for stable ordering
  float weight = 1.0f;      // Weighted policy: from the rating (5.2, PresetMetadata::weightFor)
  /// 5.2: false for "never auto-select" and for presets outside the tag
  /// filter. advanceNext/advancePrevious skip it; setCurrentIndex (a pick
  /// by hand) doesn't. When no entry is eligible, all of them are.
  bool autoSelect = true;
};

enum class PlaylistPolicy { Sequential, ShuffleNoRepeat, Weighted };

/// Own playlist implementation (D7): folder scan, selection policies, lock,
/// and index mapping, independent of libprojectM-4-playlist (which doesn't
/// offer ratings, tags, history windows, or sample-accurate scheduling).
///
/// Pure logic, no threading of its own -- TransitionScheduler (Phase 1.12)
/// owns calling into this from the analysis thread (§4.2).
class Playlist {
public:
  /// Recursively scans `rootPath` for `.milk` files. Returns entries sorted
  /// by relative path, so rescanning an unchanged folder always produces the
  /// same order (§7 Phase 1.11: "stable ordering across rescans") --
  /// filesystem enumeration order is not guaranteed stable on every platform,
  /// so this function imposes its own.
  [[nodiscard]] static std::vector<PlaylistEntry> scanFolder(const std::string& rootPath);

  explicit Playlist(std::vector<PlaylistEntry> entries);

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] const PlaylistEntry& at(std::size_t index) const;
  /// Updates an entry's `weight` and `autoSelect` (5.2). Out-of-range: no-op.
  void setSelectionInfo(std::size_t index, float weight, bool autoSelect);

  [[nodiscard]] std::size_t currentIndex() const noexcept { return currentIndex_; }
  /// Explicit selection (e.g. from the `presetIndex` parameter or the UI
  /// combo box). Clamped to a valid index; recorded in history like any
  /// other selection so `previous` can return to whatever played before it.
  void setCurrentIndex(std::size_t index);

  void setLocked(bool locked) noexcept { locked_ = locked; }
  [[nodiscard]] bool isLocked() const noexcept { return locked_; }

  void setPolicy(PlaylistPolicy policy) noexcept { policy_ = policy; }
  [[nodiscard]] PlaylistPolicy policy() const noexcept { return policy_; }

  /// How many recently-played entries ShuffleNoRepeat/Weighted must avoid
  /// re-selecting. Clamped internally to size()-1.
  void setHistoryWindowSize(std::size_t n) noexcept { historyWindowSize_ = n; }
  [[nodiscard]] std::size_t historyWindowSize() const noexcept { return historyWindowSize_; }

  /// Advances to the next entry per the current policy; a no-op while
  /// locked. `rng` makes Shuffle/Weighted selection deterministic and
  /// testable. Returns the new currentIndex().
  std::size_t advanceNext(std::mt19937& rng);

  /// Returns to whatever entry played immediately before the current one
  /// (from history), or decrements sequentially if there's no history left;
  /// a no-op while locked. Returns the new currentIndex().
  std::size_t advancePrevious();

private:
  [[nodiscard]] bool isInNoRepeatWindow(std::size_t index) const noexcept;
  // autoSelect, or every entry when none has it.
  [[nodiscard]] std::vector<bool> eligibility() const;
  void recordHistory(std::size_t index);

  std::vector<PlaylistEntry> entries_;
  std::size_t currentIndex_ = 0;
  bool locked_ = false;
  PlaylistPolicy policy_ = PlaylistPolicy::Sequential;
  std::size_t historyWindowSize_ = 10;
  std::deque<std::size_t> history_; // most-recently-played at the back; includes currentIndex_
};

} // namespace milkdawp::core
