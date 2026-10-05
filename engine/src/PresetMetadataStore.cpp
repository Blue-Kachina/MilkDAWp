// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/PresetMetadataStore.h"

#include <utility>

namespace milkdawp::engine {

namespace {
constexpr juce::uint32 kCheckIntervalMs = 1000;
} // namespace

PresetMetadataStore::PresetMetadataStore(juce::File file)
    : file_(std::move(file)), metadata_(std::make_shared<const core::PresetMetadata>()) {
  const std::lock_guard lock(mutex_);
  refreshLocked(true);
}

juce::File PresetMetadataStore::defaultFile() {
  auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  folder = folder.getChildFile("Application Support");
#endif
  return folder.getChildFile(MILKDAWP_USER_DATA_FOLDER).getChildFile("preset-metadata.txt");
}

std::shared_ptr<PresetMetadataStore> PresetMetadataStore::shared() {
  // Weak, so the last plugin instance closing releases it.
  static std::mutex mutex;
  static std::weak_ptr<PresetMetadataStore> instance;
  const std::lock_guard lock(mutex);
  auto store = instance.lock();
  if (store == nullptr) {
    store = std::make_shared<PresetMetadataStore>(defaultFile());
    instance = store;
  }
  return store;
}

void PresetMetadataStore::refreshLocked(bool force) {
  const auto now = juce::Time::getMillisecondCounter();
  if (!force && checkedOnce_ && now - lastCheckMs_ < kCheckIntervalMs) {
    return;
  }
  checkedOnce_ = true;
  lastCheckMs_ = now;

  const auto modified = file_.existsAsFile() ? file_.getLastModificationTime() : juce::Time();
  if (!force && modified == loadedModificationTime_) {
    return;
  }
  loadedModificationTime_ = modified;
  auto loaded = file_.existsAsFile()
                    ? core::PresetMetadata::parse(file_.loadFileAsString().toStdString())
                    : core::PresetMetadata();
  if (loaded != *metadata_) {
    metadata_ = std::make_shared<const core::PresetMetadata>(std::move(loaded));
    ++generation_;
  }
}

std::shared_ptr<const core::PresetMetadata> PresetMetadataStore::snapshot() {
  const std::lock_guard lock(mutex_);
  refreshLocked(false);
  return metadata_;
}

std::uint64_t PresetMetadataStore::generation() {
  const std::lock_guard lock(mutex_);
  refreshLocked(false);
  return generation_;
}

core::PresetInfo PresetMetadataStore::get(const std::string& path) {
  return snapshot()->get(path);
}

std::vector<std::string> PresetMetadataStore::allTags() {
  return snapshot()->allTags();
}

bool PresetMetadataStore::set(const std::string& path, const core::PresetInfo& info) {
  const std::lock_guard lock(mutex_);
  refreshLocked(true); // another process's edits first
  auto updated = *metadata_;
  updated.set(path, info);
  if (updated == *metadata_) {
    return true;
  }

  file_.getParentDirectory().createDirectory();
  const juce::TemporaryFile temp(file_);
  const bool written =
      temp.getFile().replaceWithText(juce::String(updated.serialize()), false, false, "\n") &&
      temp.overwriteTargetFileWithTemporary();
  metadata_ = std::make_shared<const core::PresetMetadata>(std::move(updated));
  ++generation_;
  if (written) {
    loadedModificationTime_ = file_.getLastModificationTime();
  }
  return written;
}

} // namespace milkdawp::engine
