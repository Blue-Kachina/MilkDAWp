// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/PresetSearch.h"

#include <algorithm>

namespace milkdawp::ui {

namespace {
std::string lowerUtf8(const std::string& text) { return juce::String::fromUTF8(text.c_str()).toLowerCase().toStdString(); }
} // namespace

std::vector<std::string> presetSearchWords(const juce::String& query) {
  std::vector<std::string> words;
  for (const auto& token : juce::StringArray::fromTokens(query.toLowerCase(), " \t\r\n", "")) {
    if (token.isNotEmpty()) {
      words.push_back(token.toStdString());
    }
  }
  return words;
}

void PresetSearchIndex::rebuild(const std::vector<std::string>& names) {
  names_.clear();
  names_.reserve(names.size());
  for (const auto& name : names) {
    auto lower = lowerUtf8(name);
    std::replace(lower.begin(), lower.end(), '\\', '/');
    names_.push_back(std::move(lower));
  }
  extra_.clear();
}

void PresetSearchIndex::setExtraText(std::vector<std::string> extra) {
  for (auto& text : extra) {
    text = lowerUtf8(text);
  }
  extra_ = std::move(extra);
}

bool PresetSearchIndex::matches(int index, const std::vector<std::string>& words) const {
  if (index < 0 || static_cast<std::size_t>(index) >= names_.size()) {
    return false;
  }
  const auto& name = names_[static_cast<std::size_t>(index)];
  const auto* extra = static_cast<std::size_t>(index) < extra_.size() ? &extra_[static_cast<std::size_t>(index)] : nullptr;
  return std::all_of(words.begin(), words.end(), [&](const std::string& word) {
    return name.find(word) != std::string::npos || (extra != nullptr && extra->find(word) != std::string::npos);
  });
}

std::vector<int> PresetSearchIndex::filter(const std::vector<std::string>& words) const {
  std::vector<int> result;
  result.reserve(words.empty() ? names_.size() : names_.size() / 8);
  for (std::size_t i = 0; i < names_.size(); ++i) {
    if (words.empty() || matches(static_cast<int>(i), words)) {
      result.push_back(static_cast<int>(i));
    }
  }
  return result;
}

} // namespace milkdawp::ui
