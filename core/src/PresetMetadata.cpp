// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/PresetMetadata.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace milkdawp::core {

namespace {

constexpr std::string_view kHeader = "# MilkDAWp preset metadata v1";

std::string lowerAscii(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return result;
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string_view> split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (true) {
    const auto end = text.find(separator, start);
    parts.push_back(
        text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
    if (end == std::string_view::npos) {
      return parts;
    }
    start = end + 1;
  }
}

} // namespace

std::string PresetMetadata::keyFor(std::string_view pathOrName) {
  const auto slash = pathOrName.find_last_of("/\\");
  return lowerAscii(slash == std::string_view::npos ? pathOrName : pathOrName.substr(slash + 1));
}

PresetInfo PresetMetadata::get(std::string_view path) const {
  const auto it = entries_.find(keyFor(path));
  return it != entries_.end() ? it->second : PresetInfo{};
}

void PresetMetadata::set(std::string_view path, PresetInfo info) {
  info.rating = std::clamp(info.rating, 0, 5);
  info.tags = parseTags(joinTags(info.tags));
  const auto key = keyFor(path);
  if (key.empty()) {
    return;
  }
  if (info.isDefault()) {
    entries_.erase(key);
  } else {
    entries_[key] = std::move(info);
  }
}

std::vector<std::string> PresetMetadata::allTags() const {
  std::set<std::string> tags;
  for (const auto& [key, info] : entries_) {
    tags.insert(info.tags.begin(), info.tags.end());
  }
  return {tags.begin(), tags.end()};
}

std::string PresetMetadata::serialize() const {
  std::string text(kHeader);
  text += '\n';
  for (const auto& [key, info] : entries_) {
    text += std::to_string(info.rating);
    text += '\t';
    text += info.neverAutoSelect ? "n" : "";
    text += '\t';
    text += joinTags(info.tags);
    text += '\t';
    text += key;
    text += '\n';
  }
  return text;
}

PresetMetadata PresetMetadata::parse(std::string_view text) {
  PresetMetadata metadata;
  for (auto line : split(text, '\n')) {
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto fields = split(line, '\t');
    if (fields.size() != 4 || fields[3].empty() || fields[0].size() != 1 || fields[0][0] < '0' ||
        fields[0][0] > '5') {
      continue; // malformed: skipped
    }
    PresetInfo info;
    info.rating = fields[0][0] - '0';
    info.neverAutoSelect = fields[1].find('n') != std::string_view::npos;
    info.tags = parseTags(fields[2]);
    metadata.set(fields[3], std::move(info));
  }
  return metadata;
}

float PresetMetadata::weightFor(const PresetInfo& info) noexcept {
  return info.rating <= 0 ? 1.0f : std::pow(2.0f, static_cast<float>(std::min(info.rating, 5) - 3));
}

std::vector<std::string> PresetMetadata::parseTags(std::string_view text) {
  std::set<std::string> tags;
  for (const auto part : split(text, ',')) {
    auto tag = lowerAscii(trim(part));
    // Tabs and newlines would break the file format.
    tag.erase(std::remove_if(tag.begin(),
                             tag.end(),
                             [](char c) { return c == '\t' || c == '\n' || c == '\r'; }),
              tag.end());
    if (!tag.empty()) {
      tags.insert(std::move(tag));
    }
  }
  return {tags.begin(), tags.end()};
}

std::string PresetMetadata::joinTags(const std::vector<std::string>& tags) {
  std::string text;
  for (const auto& tag : tags) {
    if (!text.empty()) {
      text += ',';
    }
    text += tag;
  }
  return text;
}

bool PresetMetadata::matchesFilter(const PresetInfo& info, const std::vector<std::string>& filter) {
  if (filter.empty()) {
    return true;
  }
  return std::any_of(filter.begin(), filter.end(), [&info](const std::string& tag) {
    return std::find(info.tags.begin(), info.tags.end(), tag) != info.tags.end();
  });
}

} // namespace milkdawp::core
