// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/Playlist.h"

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <stdexcept>

namespace milkdawp::core {

namespace {
constexpr std::size_t kMaxHistoryLength = 256;

bool isMilkFile(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".milk";
}
} // namespace

std::vector<PlaylistEntry> Playlist::scanFolder(const std::string& rootPath) {
  std::vector<PlaylistEntry> entries;
  const std::filesystem::path root(rootPath);

  if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
    return entries;
  }

  for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(
           root, std::filesystem::directory_options::skip_permission_denied)) {
    if (!dirEntry.is_regular_file() || !isMilkFile(dirEntry.path())) {
      continue;
    }
    PlaylistEntry entry;
    entry.absolutePath = std::filesystem::absolute(dirEntry.path()).string();
    entry.relativePath = std::filesystem::relative(dirEntry.path(), root).string();
    entries.push_back(std::move(entry));
  }

  // Filesystem enumeration order is not guaranteed stable across platforms
  // or rescans; impose our own so `presetIndex` automation means the same
  // preset from one scan to the next (§7 Phase 1.11).
  std::sort(entries.begin(), entries.end(),
            [](const PlaylistEntry& a, const PlaylistEntry& b) { return a.relativePath < b.relativePath; });

  return entries;
}

Playlist::Playlist(std::vector<PlaylistEntry> entries) : entries_(std::move(entries)) {
  if (!entries_.empty()) {
    history_.push_back(0);
  }
}

const PlaylistEntry& Playlist::at(std::size_t index) const {
  if (index >= entries_.size()) {
    throw std::out_of_range("Playlist::at: index out of range");
  }
  return entries_[index];
}

void Playlist::setCurrentIndex(std::size_t index) {
  if (entries_.empty()) {
    return;
  }
  currentIndex_ = std::min(index, entries_.size() - 1);
  recordHistory(currentIndex_);
}

void Playlist::recordHistory(std::size_t index) {
  history_.push_back(index);
  while (history_.size() > kMaxHistoryLength) {
    history_.pop_front();
  }
}

bool Playlist::isInNoRepeatWindow(std::size_t index) const noexcept {
  const std::size_t window = std::min(historyWindowSize_, history_.size());
  auto it = history_.rbegin();
  for (std::size_t i = 0; i < window; ++i, ++it) {
    if (*it == index) {
      return true;
    }
  }
  return false;
}

void Playlist::setSelectionInfo(std::size_t index, float weight, bool autoSelect) {
  if (index < entries_.size()) {
    entries_[index].weight = weight;
    entries_[index].autoSelect = autoSelect;
  }
}

std::vector<bool> Playlist::eligibility() const {
  std::vector<bool> eligible(entries_.size());
  bool any = false;
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    eligible[i] = entries_[i].autoSelect;
    any = any || eligible[i];
  }
  if (!any) {
    std::fill(eligible.begin(), eligible.end(), true); // nothing matches: ignore the filter
  }
  return eligible;
}

std::size_t Playlist::advanceNext(std::mt19937& rng) {
  if (locked_ || entries_.empty()) {
    return currentIndex_;
  }

  const auto eligible = eligibility();
  std::size_t newIndex = currentIndex_;

  switch (policy_) {
  case PlaylistPolicy::Sequential:
    // The next eligible entry in order (the current one again when it is
    // the only one).
    for (std::size_t step = 1; step <= entries_.size(); ++step) {
      const auto index = (currentIndex_ + step) % entries_.size();
      if (eligible[index]) {
        newIndex = index;
        break;
      }
    }
    break;

  case PlaylistPolicy::ShuffleNoRepeat:
  case PlaylistPolicy::Weighted: {
    const bool weighted = policy_ == PlaylistPolicy::Weighted;
    // The no-repeat window as a mask, built once: asking isInNoRepeatWindow()
    // per entry walks the history for each of ~10k presets (6.1).
    std::vector<char> recent(entries_.size(), 0);
    {
      const std::size_t window = std::min(historyWindowSize_, history_.size());
      auto it = history_.rbegin();
      for (std::size_t i = 0; i < window; ++i, ++it) {
        if (*it < recent.size()) {
          recent[*it] = 1;
        }
      }
    }
    std::vector<std::size_t> candidates;
    std::vector<double> weights;
    candidates.reserve(entries_.size());
    weights.reserve(entries_.size());
    const auto collect = [&](bool skipHistory) {
      for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (eligible[i] && (skipHistory ? recent[i] == 0 : i != currentIndex_)) {
          candidates.push_back(i);
          weights.push_back(std::max(0.0f, entries_[i].weight));
        }
      }
    };
    collect(true);
    if (candidates.empty()) {
      // The window covers every eligible entry; the only sensible fallback
      // is to avoid immediately repeating the current one.
      collect(false);
    }
    const double totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (candidates.empty()) {
      newIndex = currentIndex_; // only one eligible entry exists
    } else if (!weighted || totalWeight <= 0.0) {
      // Shuffle, or all candidate weights are zero: a uniform choice rather
      // than a degenerate discrete_distribution.
      std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
      newIndex = candidates[dist(rng)];
    } else {
      std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
      newIndex = candidates[dist(rng)];
    }
    break;
  }
  }

  currentIndex_ = newIndex;
  recordHistory(currentIndex_);
  return currentIndex_;
}

std::size_t Playlist::advancePrevious() {
  if (locked_ || entries_.empty()) {
    return currentIndex_;
  }

  if (history_.size() > 1) {
    history_.pop_back();
    currentIndex_ = history_.back();
    return currentIndex_;
  }

  const auto eligible = eligibility();
  for (std::size_t step = 1; step <= entries_.size(); ++step) {
    const auto index = (currentIndex_ + entries_.size() - step) % entries_.size();
    if (eligible[index]) {
      currentIndex_ = index;
      break;
    }
  }
  recordHistory(currentIndex_);
  return currentIndex_;
}

} // namespace milkdawp::core
