// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/PresetMenu.h"

#include <algorithm>
#include <iterator>

namespace milkdawp::ui {

PresetNameParts splitPresetName(const std::string& name) {
  const auto slash = name.find_last_of("/\\");
  if (slash == std::string::npos) {
    return {{}, name};
  }
  auto folder = name.substr(0, slash);
  std::replace(folder.begin(), folder.end(), '\\', '/'); // shown the same on every platform
  return {folder, name.substr(slash + 1)};
}

bool PresetTreeNode::containsIndex(int index) const {
  return std::any_of(presets.begin(), presets.end(), [index](const auto& p) { return p.second == index; }) ||
         std::any_of(folders.begin(), folders.end(), [index](const auto& f) { return f.containsIndex(index); });
}

PresetTreeNode buildPresetTree(const std::vector<std::pair<std::string, int>>& entries) {
  PresetTreeNode root;
  for (const auto& [name, index] : entries) {
    auto* node = &root;
    std::size_t start = 0;
    for (auto slash = name.find_first_of("/\\"); slash != std::string::npos;
         slash = name.find_first_of("/\\", start)) {
      const auto folder = name.substr(start, slash - start);
      start = slash + 1;
      if (folder.empty()) {
        continue;
      }
      auto it = std::find_if(node->folders.begin(), node->folders.end(),
                             [&folder](const PresetTreeNode& f) { return f.name == folder; });
      if (it == node->folders.end()) {
        node->folders.push_back(PresetTreeNode{folder, {}, {}});
        it = std::prev(node->folders.end());
      }
      node = &*it;
    }
    node->presets.emplace_back(name.substr(start), index);
  }
  return root;
}

PresetTreeNode buildPresetTree(const std::vector<std::string>& names) {
  std::vector<std::pair<std::string, int>> entries;
  entries.reserve(names.size());
  for (std::size_t i = 0; i < names.size(); ++i) {
    entries.emplace_back(names[i], static_cast<int>(i));
  }
  return buildPresetTree(entries);
}

void addPresetTree(juce::PopupMenu& menu, const PresetTreeNode& tree, int currentIndex,
                   const std::function<void(int)>& onPick) {
  for (const auto& folder : tree.folders) {
    juce::PopupMenu sub;
    addPresetTree(sub, folder, currentIndex, onPick);
    menu.addSubMenu(juce::String(folder.name), sub, true, nullptr, folder.containsIndex(currentIndex));
  }
  for (const auto& [leaf, index] : tree.presets) {
    menu.addItem(juce::String(leaf), true, index == currentIndex, [onPick, i = index] { onPick(i); });
  }
}

} // namespace milkdawp::ui
