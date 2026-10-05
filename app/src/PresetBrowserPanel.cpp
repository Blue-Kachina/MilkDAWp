// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PresetBrowserPanel.h"

#include <algorithm>
#include <unordered_map>

namespace milkdawp::app {

namespace {
constexpr int kRowHeight = 22;
constexpr int kTopRowHeight = 26;
} // namespace

/// One folder or one preset. Folders build their whole subtree eagerly at
/// construction (a preset library is small enough that this beats the
/// complexity of JUCE's lazy-open callback); the root is hidden and its
/// direct children start the visible tree.
class PresetBrowserPanel::Item final : public juce::TreeViewItem {
public:
  Item(PresetBrowserPanel& owner, juce::String name, int index, std::string path, bool isFolder)
      : owner_(owner), name_(std::move(name)), index_(index), path_(std::move(path)), folder_(isFolder) {}

  [[nodiscard]] bool mightContainSubItems() override { return folder_; }
  [[nodiscard]] int getItemHeight() const override { return kRowHeight; }

  void paintItem(juce::Graphics& g, int width, int height) override {
    const bool current = !folder_ && index_ == owner_.currentIndex_;
    g.setColour(current ? juce::Colours::orange : juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(14.0f, current ? juce::Font::bold : juce::Font::plain)));
    juce::String text = name_;
    if (!folder_) {
      if (owner_.favouriteSet_.count(path_) > 0) {
        text = juce::String(juce::CharPointer_UTF8("\xe2\x98\x85 ")) + text; // U+2605, star
      }
      if (owner_.blacklistSet_.count(path_) > 0) {
        text += " (blacklisted)";
      }
    }
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
  }

  void itemClicked(const juce::MouseEvent& event) override {
    if (folder_) {
      return;
    }
    if (event.mods.isPopupMenu()) {
      owner_.showContextMenu(path_);
    } else {
      owner_.callbacks_.onPick(index_);
    }
  }

private:
  PresetBrowserPanel& owner_;
  juce::String name_;
  int index_;
  std::string path_;
  bool folder_;
};

PresetBrowserPanel::PresetBrowserPanel(Callbacks callbacks) : callbacks_(std::move(callbacks)) {
  search_.setTextToShowWhenEmpty("Search presets...", juce::Colours::grey);
  search_.addListener(this);
  addAndMakeVisible(search_);

  for (auto* button : {&allTab_, &favouritesTab_, &recentTab_}) {
    button->setClickingTogglesState(true);
    button->setRadioGroupId(1, juce::dontSendNotification);
    addAndMakeVisible(*button);
  }
  allTab_.setToggleState(true, juce::dontSendNotification);
  allTab_.onClick = [this] { setTab(Tab::All); };
  favouritesTab_.onClick = [this] { setTab(Tab::Favourites); };
  recentTab_.onClick = [this] { setTab(Tab::Recent); };

  tree_.setRootItemVisible(false);
  tree_.setDefaultOpenness(true);
  tree_.setColour(juce::TreeView::backgroundColourId, juce::Colours::black);
  addAndMakeVisible(tree_);

  setSize(420, 520);
  refresh(true);
  startTimerHz(2);
}

PresetBrowserPanel::~PresetBrowserPanel() {
  stopTimer();
  tree_.setRootItem(nullptr);
}

void PresetBrowserPanel::resized() {
  auto area = getLocalBounds().reduced(8);
  search_.setBounds(area.removeFromTop(kTopRowHeight));
  area.removeFromTop(4);
  auto tabs = area.removeFromTop(kTopRowHeight);
  const int tabWidth = tabs.getWidth() / 3;
  allTab_.setBounds(tabs.removeFromLeft(tabWidth));
  favouritesTab_.setBounds(tabs.removeFromLeft(tabWidth));
  recentTab_.setBounds(tabs);
  area.removeFromTop(4);
  tree_.setBounds(area);
}

void PresetBrowserPanel::setTab(Tab tab) {
  activeTab_ = tab;
  refresh(true);
}

void PresetBrowserPanel::textEditorTextChanged(juce::TextEditor&) { refresh(true); }

void PresetBrowserPanel::timerCallback() { refresh(false); }

void PresetBrowserPanel::refresh(bool force) {
  auto names = callbacks_.names ? callbacks_.names() : std::vector<std::string>{};
  auto paths = callbacks_.paths ? callbacks_.paths() : std::vector<std::string>{};
  auto blacklisted = callbacks_.blacklistedPaths ? callbacks_.blacklistedPaths() : std::vector<std::string>{};
  auto favourites = callbacks_.favouritePaths ? callbacks_.favouritePaths() : std::vector<std::string>{};
  auto recent = callbacks_.recentPaths ? callbacks_.recentPaths() : std::vector<std::string>{};
  const int current = callbacks_.currentIndex ? callbacks_.currentIndex() : -1;

  const std::unordered_set<std::string> blacklistSet(blacklisted.begin(), blacklisted.end());
  const std::unordered_set<std::string> favouriteSet(favourites.begin(), favourites.end());

  if (!force && names == names_ && paths == paths_ && blacklistSet == blacklistSet_ &&
      favouriteSet == favouriteSet_ && current == currentIndex_) {
    return; // nothing an open tree needs to reflect changed
  }

  names_ = std::move(names);
  paths_ = std::move(paths);
  blacklistSet_ = blacklistSet;
  favouriteSet_ = favouriteSet;
  currentIndex_ = current;

  ui::PresetTreeNode treeData;
  const auto query = search_.getText().trim().toLowerCase();
  auto matches = [&query](const std::string& name) {
    return query.isEmpty() || juce::String(name).toLowerCase().contains(query);
  };

  if (activeTab_ == Tab::All) {
    std::vector<std::pair<std::string, int>> entries;
    for (std::size_t i = 0; i < names_.size(); ++i) {
      if (matches(names_[i])) {
        entries.emplace_back(names_[i], static_cast<int>(i));
      }
    }
    treeData = ui::buildPresetTree(entries);
  } else {
    // Favourites and Recent are flat (recency/favourite-ness is the point,
    // not folder position), most-relevant first.
    std::unordered_map<std::string, int> indexForPath;
    for (std::size_t i = 0; i < paths_.size(); ++i) {
      indexForPath.emplace(paths_[i], static_cast<int>(i));
    }
    const auto& orderedPaths = activeTab_ == Tab::Favourites ? favourites : recent;
    for (const auto& path : orderedPaths) {
      const auto it = indexForPath.find(path);
      if (it == indexForPath.end()) {
        continue; // no longer in the current playlist (rescanned away)
      }
      const auto& name = names_[static_cast<std::size_t>(it->second)];
      if (matches(name)) {
        treeData.presets.emplace_back(name, it->second);
      }
    }
  }

  root_ = buildItem(treeData, true);
  tree_.setRootItem(root_.get());
}

std::unique_ptr<PresetBrowserPanel::Item> PresetBrowserPanel::buildItem(const ui::PresetTreeNode& node, bool isRoot) {
  auto item =
      std::make_unique<Item>(*this, isRoot ? juce::String("Presets") : juce::String(node.name), -1, std::string{}, true);
  item->setLinesDrawnForSubItems(false);
  for (const auto& folder : node.folders) {
    item->addSubItem(buildItem(folder, false).release());
  }
  for (const auto& [leaf, index] : node.presets) {
    const auto path =
        index >= 0 && static_cast<std::size_t>(index) < paths_.size() ? paths_[static_cast<std::size_t>(index)] : std::string{};
    item->addSubItem(new Item(*this, juce::String(leaf), index, path, false)); // NOLINT: owned by the TreeView
  }
  return item;
}

void PresetBrowserPanel::showContextMenu(const std::string& path) {
  const bool fav = favouriteSet_.count(path) > 0;
  const bool blacklisted = blacklistSet_.count(path) > 0;
  juce::PopupMenu menu;
  menu.addItem(fav ? "Remove from favourites" : "Add to favourites", [this, path, fav] {
    if (callbacks_.onSetFavourite) {
      callbacks_.onSetFavourite(path, !fav);
    }
  });
  menu.addItem(blacklisted ? "Remove from blacklist" : "Add to blacklist", [this, path, blacklisted] {
    if (callbacks_.onSetBlacklisted) {
      callbacks_.onSetBlacklisted(path, !blacklisted);
    }
  });
  if (callbacks_.presetInfo) {
    ui::addPresetInfoSection(menu, juce::File(juce::String(path)).getFileNameWithoutExtension(),
                             callbacks_.presetInfo(path), this);
  }
  menu.showMenuAsync(juce::PopupMenu::Options());
}

} // namespace milkdawp::app
