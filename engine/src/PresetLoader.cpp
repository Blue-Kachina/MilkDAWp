// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/PresetLoader.h"

#include <array>
#include <chrono>

namespace milkdawp::engine {

namespace {
constexpr int kSniffBytes = 512;
}

PresetLoader::ValidationResult PresetLoader::validate(const juce::File& presetFile) {
  if (!presetFile.existsAsFile()) {
    return {false, "file does not exist"};
  }

  const auto size = presetFile.getSize();
  if (size <= 0) {
    return {false, "file is empty"};
  }

  juce::FileInputStream stream(presetFile);
  if (!stream.openedOk()) {
    return {false, "could not open file: " + stream.getStatus().getErrorMessage().toStdString()};
  }

  std::array<char, kSniffBytes> buffer{};
  const auto bytesRead = stream.read(buffer.data(), static_cast<int>(buffer.size()));
  if (bytesRead <= 0) {
    return {false, "file could not be read"};
  }

  bool sawEquals = false;
  for (int i = 0; i < bytesRead; ++i) {
    const auto byte = static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
    if (byte == 0) {
      return {false, "file contains a null byte in its first " + std::to_string(bytesRead) +
                          " bytes (not a text-based .milk preset)"};
    }
    if (buffer[static_cast<std::size_t>(i)] == '=') {
      sawEquals = true;
    }
  }

  if (!sawEquals) {
    return {false, "no '=' assignment found in the first " + std::to_string(bytesRead) +
                        " bytes (does not look like a .milk preset)"};
  }

  return {true, ""};
}

PresetLoader::PrefetchResult PresetLoader::prefetch(const juce::File& presetFile) {
  const auto path = presetFile.getFullPathName().toStdString();

  const auto validation = validate(presetFile);
  if (!validation.ok) {
    blacklist(path, validation.reason);
    return {false, validation.reason, "", 0};
  }

  const auto start = std::chrono::steady_clock::now();
  const auto contents = presetFile.loadFileAsString();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const auto micros = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());

  if (contents.isEmpty()) {
    const std::string reason = "file read returned no content";
    blacklist(path, reason);
    return {false, reason, "", micros};
  }

  return {true, "", contents.toStdString(), micros};
}

void PresetLoader::blacklist(const std::string& presetPath, std::string reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  blacklist_[presetPath] = std::move(reason);
}

bool PresetLoader::isBlacklisted(const std::string& presetPath) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return blacklist_.find(presetPath) != blacklist_.end();
}

std::optional<std::string> PresetLoader::blacklistReason(const std::string& presetPath) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = blacklist_.find(presetPath);
  if (it == blacklist_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void PresetLoader::clearBlacklistEntry(const std::string& presetPath) {
  std::lock_guard<std::mutex> lock(mutex_);
  blacklist_.erase(presetPath);
}

void PresetLoader::clearBlacklist() {
  std::lock_guard<std::mutex> lock(mutex_);
  blacklist_.clear();
}

std::size_t PresetLoader::blacklistSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return blacklist_.size();
}

std::vector<std::string> PresetLoader::blacklistedPaths() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> paths;
  paths.reserve(blacklist_.size());
  for (const auto& [path, reason] : blacklist_) {
    paths.push_back(path);
  }
  return paths;
}

} // namespace milkdawp::engine
