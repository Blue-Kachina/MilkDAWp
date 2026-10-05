// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "milkdawp/core/PresetMetadata.h"

namespace milkdawp::engine {

/// The user's preset ratings, tags and "never auto-select" (5.2) on disk:
/// one file shared by the app and every plugin instance, and one store per
/// process (`shared()`), so a rating made in one instance reaches the
/// others at once.
///
/// Other processes (the app while a DAW runs the plugin) write the same
/// file: the store rereads it when its modification time changes (checked
/// at most once a second), and rereads it before every write, so one
/// process's edits don't erase another's. Last writer wins for the same
/// preset.
///
/// Thread-safe. `snapshot()` is what the Director thread reads; it never
/// waits on disk except during that once-a-second check.
class PresetMetadataStore {
public:
  explicit PresetMetadataStore(juce::File file);

  /// "<user data>/MilkDAWp/preset-metadata.txt" (the dev identity's own
  /// folder in dev builds, ADR-0007).
  [[nodiscard]] static juce::File defaultFile();
  /// This process's store for `defaultFile()`.
  [[nodiscard]] static std::shared_ptr<PresetMetadataStore> shared();

  [[nodiscard]] std::shared_ptr<const core::PresetMetadata> snapshot();
  /// Changes whenever the metadata does (a write, or another process's).
  [[nodiscard]] std::uint64_t generation();

  [[nodiscard]] core::PresetInfo get(const std::string& path);
  [[nodiscard]] std::vector<std::string> allTags();
  /// Sets one preset's info and writes the file. False if it couldn't be written.
  bool set(const std::string& path, const core::PresetInfo& info);

  [[nodiscard]] const juce::File& file() const noexcept { return file_; }

private:
  void refreshLocked(bool force);

  const juce::File file_;
  std::mutex mutex_;
  std::shared_ptr<const core::PresetMetadata> metadata_;
  std::uint64_t generation_ = 0;
  juce::Time loadedModificationTime_;
  juce::uint32 lastCheckMs_ = 0;
  bool checkedOnce_ = false;
};

} // namespace milkdawp::engine
