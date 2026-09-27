// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "milkdawp/ui/PresetMenu.h"

namespace milkdawp::app {

/// The preset library browser (4.5): a tree of the library root, a search
/// box, Favourites and Recently-played tabs (both flat, most-relevant
/// first, rather than grouped by folder -- that's the point of them), and a
/// right-click menu per preset for favourites and the playback blacklist
/// (§4.5's blacklist is `PresetLoader`'s, via `Director::blacklistPreset`,
/// so it actually keeps playback off a preset, not just a browser flag).
///
/// Pull-based like `AudioSettingsPanel`: polls `callbacks` on a timer and
/// only rebuilds the `TreeView` when the underlying data actually changed,
/// so an open folder or the scroll position survives ticks where nothing
/// did.
class PresetBrowserPanel final : public juce::Component, private juce::Timer, private juce::TextEditor::Listener {
public:
  struct Callbacks {
    std::function<std::vector<std::string>()> names; // director.presetNames(), playlist order
    std::function<std::vector<std::string>()> paths;  // parallel absolute paths
    std::function<int()> currentIndex;
    std::function<std::vector<std::string>()> blacklistedPaths;
    std::function<std::vector<std::string>()> favouritePaths;
    std::function<std::vector<std::string>()> recentPaths; // most-recently-played first
    std::function<void(int)> onPick;
    std::function<void(const std::string&, bool)> onSetFavourite;
    std::function<void(const std::string&, bool)> onSetBlacklisted;
  };

  explicit PresetBrowserPanel(Callbacks callbacks);
  ~PresetBrowserPanel() override;

  void resized() override;

private:
  enum class Tab { All, Favourites, Recent };
  class Item;

  void timerCallback() override;
  void textEditorTextChanged(juce::TextEditor&) override;
  void setTab(Tab tab);
  void refresh(bool force);
  void rebuildTree();
  [[nodiscard]] std::unique_ptr<Item> buildItem(const ui::PresetTreeNode& node, bool isRoot);
  void showContextMenu(const std::string& path);

  Callbacks callbacks_;
  juce::TextEditor search_;
  juce::TextButton allTab_{"All"};
  juce::TextButton favouritesTab_{"Favourites"};
  juce::TextButton recentTab_{"Recent"};
  juce::TreeView tree_;
  std::unique_ptr<Item> root_;

  Tab activeTab_ = Tab::All;
  int currentIndex_ = -1;
  std::vector<std::string> names_;
  std::vector<std::string> paths_;
  std::unordered_set<std::string> favouriteSet_;
  std::unordered_set<std::string> blacklistSet_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowserPanel)
};

} // namespace milkdawp::app
