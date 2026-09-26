// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/PresetLibrary.h"

namespace milkdawp::core {

std::uint32_t PresetLibrary::idFor(const std::string& absolutePath) {
  if (const auto it = ids_.find(absolutePath); it != ids_.end()) {
    return it->second;
  }
  paths_.push_back(absolutePath);
  const auto id = static_cast<std::uint32_t>(paths_.size());
  ids_.emplace(absolutePath, id);
  return id;
}

std::optional<std::string> PresetLibrary::pathFor(std::uint32_t id) const {
  if (id == 0 || id > paths_.size()) {
    return std::nullopt;
  }
  return paths_[id - 1];
}

} // namespace milkdawp::core
