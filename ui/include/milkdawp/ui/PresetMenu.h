// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// A preset display name ("Pack/Sub/Name", either slash) split into its
/// folder part ("Pack/Sub" -- always forward slashes, for display -- empty
/// at the top level) and leaf ("Name").
struct PresetNameParts {
  std::string folder;
  std::string leaf;
};
[[nodiscard]] PresetNameParts splitPresetName(const std::string& name);

/// The playlist grouped by subfolder, for the preset picker. Folders and
/// presets keep playlist order (the playlist is already sorted); each
/// preset remembers its playlist index.
struct PresetTreeNode {
  std::string name; // this folder's own name; empty for the root
  std::vector<PresetTreeNode> folders;
  std::vector<std::pair<std::string, int>> presets; // leaf name, playlist index

  [[nodiscard]] bool containsIndex(int index) const;
};
[[nodiscard]] PresetTreeNode buildPresetTree(const std::vector<std::string>& names);

/// Appends `tree` to `menu`: a submenu per folder, an item per preset, the
/// preset at `currentIndex` (and the folders holding it) ticked. Choosing a
/// preset calls `onPick` with its playlist index.
void addPresetTree(juce::PopupMenu& menu, const PresetTreeNode& tree, int currentIndex,
                   const std::function<void(int)>& onPick);

} // namespace milkdawp::ui
