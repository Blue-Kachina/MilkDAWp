// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/PresetMetadata.h"
#include "milkdawp/ui/KeyboardNavigation.h"
#include "milkdawp/ui/PresetSearch.h"

namespace milkdawp::ui {

/// 6.1b: the preset browser both shells open from the preset title (or `B`),
/// replacing the popup menu of nested folders, which can't cope with the
/// ~10k presets of the bundled pack (D12).
///
/// A popover over the picture, like Transitions and Output: a search box
/// that filters as you type (every word must match the name, folder or
/// tags), All / Favourites / Rated / Recent tabs, and a virtualised list
/// (only the visible rows are painted, so 10k rows cost nothing). Each row
/// shows the preset's name and folder, a heart for favourites and its star
/// rating; click to play, click the heart or a star to change them,
/// right-click for the rest (blacklist, never auto-select, tags).
///
/// Keyboard (5.8): opening it focuses the search box. Up/Down/Page Up/Page
/// Down move through the results without playing, Return plays the selected
/// one, Esc closes. With the list focused, F toggles the favourite, 0-5 set
/// the rating, and typing goes back to the search box.
///
/// Favourites and ratings are the shared `core::PresetMetadata` (5.2), so
/// the app and every plugin instance see the same ones. Pull-based: while
/// visible it polls its `Source` a few times a second and only re-filters
/// when the playlist, the metadata or the blacklist actually changed.
/// Message thread only.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::TextEditor::Listener,
                      private juce::KeyListener,
                      private juce::Timer {
public:
  /// What the browser reads and changes; the shell wires it to its director
  /// and metadata store. Everything but `names`/`paths`/`onPick` may be left
  /// empty.
  struct Source {
    /// Changes whenever names/paths change (DirectorStatus::playlistGeneration).
    std::function<std::uint64_t()> playlistGeneration;
    std::function<std::vector<std::string>()> names; // display names, playlist order
    std::function<std::vector<std::string>()> paths; // parallel absolute paths
    std::function<int()> currentIndex;
    std::function<std::string()> folder; // the library root, for the header

    std::function<std::shared_ptr<const core::PresetMetadata>()> metadata;
    std::function<std::uint64_t()> metadataGeneration;
    std::function<void(const std::string& path, const core::PresetInfo& info)> setInfo;
    std::function<std::vector<std::string>()> allTags;

    std::function<std::vector<std::string>()> blacklistedPaths;
    std::function<void(const std::string& path, bool blacklisted)> setBlacklisted;
    /// Most recent first. Without it there is no Recent tab.
    std::function<std::vector<std::string>()> recentPaths;

    std::function<void(int index)> onPick;
    std::function<void()> onChooseFolder;
    std::function<void()> onRescan;
  };

  enum class Tab { All, Favourites, Rated, Recent };

  static constexpr int preferredWidth = 460;
  static constexpr int preferredHeight = 560;
  static constexpr int rowHeight = 40; // two lines; also a comfortable touch target (7.6)

  PresetBrowser();
  ~PresetBrowser() override;

  void setSource(Source source);

  /// Call right after showing it: reloads, selects and scrolls to the
  /// playing preset, and focuses the search box.
  void opened();
  /// Polls the source; the timer calls this while the browser is visible.
  void refresh();

  void setTab(Tab tab);
  [[nodiscard]] Tab tab() const noexcept { return tab_; }

  /// Replaces the search text and filters at once (the search box's own
  /// change notification is asynchronous).
  void setSearchText(const juce::String& text);
  /// The browser's keys (see the class comment). `inList`: the list has
  /// focus rather than the search box. True if the key was used. The key
  /// listener on the search box and the list calls this.
  bool handleKey(const juce::KeyPress& key, bool inList);

  /// The current results, as playlist indices in display order.
  [[nodiscard]] const std::vector<int>& results() const noexcept { return results_; }
  /// The selected result's playlist index, or -1.
  [[nodiscard]] int selectedIndex() const;
  /// "123 of 9,795 presets" (or "9,795 presets").
  [[nodiscard]] juce::String countText() const;

  std::function<void()> onCloseRequested;

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override { focusRing_.paint(g); }
  void resized() override;
  void visibilityChanged() override;

  juce::Label titleLabel;
  juce::TextEditor searchBox;
  juce::TextButton allTab{"All"};
  juce::TextButton favouritesTab{"Favourites"};
  juce::TextButton ratedTab{"Rated"};
  juce::TextButton recentTab{"Recent"};
  juce::TextButton folderButton{"Folder..."};
  juce::TextButton rescanButton{"Rescan"};
  juce::TextButton closeButton{"Close"};
  juce::ListBox list{"Presets"};

private:
  // ListBoxModel
  int getNumRows() override;
  void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
  void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
  void returnKeyPressed(int row) override;
  juce::String getNameForRow(int row) override;

  void textEditorTextChanged(juce::TextEditor&) override;
  bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;
  using juce::Component::keyPressed;
  void timerCallback() override { refresh(); }

  void applyFilter();
  void selectRow(int row);
  void moveSelection(int delta);
  void play(int index);
  void toggleFavourite(int index);
  void setRating(int index, int rating);
  void showContextMenu(int index);
  [[nodiscard]] core::PresetInfo infoFor(int index) const;
  void updateInfo(int index, const core::PresetInfo& info);
  [[nodiscard]] const std::string& pathFor(int index) const;

  Source source_;
  Tab tab_ = Tab::All;
  bool loaded_ = false;
  std::uint64_t playlistGeneration_ = 0;
  std::uint64_t metadataGeneration_ = 0;
  int current_ = -1;

  std::vector<std::string> names_;
  std::vector<std::string> paths_;
  std::unordered_map<std::string, int> indexForPath_;
  std::vector<core::PresetInfo> infos_; // parallel to names_
  std::unordered_set<std::string> blacklist_;
  std::vector<std::string> recent_;

  PresetSearchIndex index_;
  std::vector<int> results_;

  KeyboardFocusRing focusRing_{*this};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};

/// Row geometry, shared by painting and hit-testing (and the tests).
struct PresetBrowserRowLayout {
  juce::Rectangle<int> heart;
  juce::Rectangle<int> text;
  juce::Rectangle<int> stars; // five equal cells, left to right
  [[nodiscard]] static PresetBrowserRowLayout forRow(int width, int height);
  /// 1-5 for a point on a star, else 0.
  [[nodiscard]] int starAt(juce::Point<int> point) const;
};

} // namespace milkdawp::ui
