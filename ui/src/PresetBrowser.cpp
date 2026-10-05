// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/PresetBrowser.h"

#include <algorithm>

#include "milkdawp/ui/DrawerLookAndFeel.h"
#include "milkdawp/ui/PresetInfoMenu.h"
#include "milkdawp/ui/PresetMenu.h"

namespace milkdawp::ui {

namespace {

constexpr int kHeaderHeight = 26;
constexpr int kGap = 6;
constexpr int kStarCell = 16;

const juce::Colour kFavourite{0xffff5a7a};
const juce::Colour kStar{0xffffc94d};

juce::Path heartPath(juce::Rectangle<float> r) {
  const float x = r.getX();
  const float y = r.getY();
  const float w = r.getWidth();
  const float h = r.getHeight();
  juce::Path p;
  p.startNewSubPath(x + w * 0.5f, y + h * 0.92f);
  p.cubicTo(x + w * 0.1f, y + h * 0.62f, x - w * 0.02f, y + h * 0.3f, x + w * 0.2f, y + h * 0.12f);
  p.cubicTo(x + w * 0.34f, y + h * 0.02f, x + w * 0.46f, y + h * 0.12f, x + w * 0.5f, y + h * 0.26f);
  p.cubicTo(x + w * 0.54f, y + h * 0.12f, x + w * 0.66f, y + h * 0.02f, x + w * 0.8f, y + h * 0.12f);
  p.cubicTo(x + w * 1.02f, y + h * 0.3f, x + w * 0.9f, y + h * 0.62f, x + w * 0.5f, y + h * 0.92f);
  p.closeSubPath();
  return p;
}

juce::Path starPath(juce::Rectangle<float> r) {
  juce::Path p;
  const float outer = std::min(r.getWidth(), r.getHeight()) * 0.5f;
  p.addStar(r.getCentre(), 5, outer * 0.45f, outer, 0.0f);
  return p;
}

juce::String withThousands(std::size_t n) {
  auto digits = juce::String(static_cast<juce::int64>(n));
  for (int i = digits.length() - 3; i > 0; i -= 3) {
    digits = digits.substring(0, i) + "," + digits.substring(i);
  }
  return digits;
}

} // namespace

PresetBrowserRowLayout PresetBrowserRowLayout::forRow(int width, int height) {
  PresetBrowserRowLayout layout;
  auto area = juce::Rectangle<int>(0, 0, width, height);
  layout.heart = area.removeFromLeft(32);
  layout.stars = area.removeFromRight(kStarCell * 5 + 10).withTrimmedRight(10);
  layout.text = area.withTrimmedRight(6);
  return layout;
}

int PresetBrowserRowLayout::starAt(juce::Point<int> point) const {
  if (!stars.contains(point)) {
    return 0;
  }
  return std::clamp((point.x - stars.getX()) / kStarCell + 1, 1, 5);
}

PresetBrowser::PresetBrowser() {
  titleLabel.setText("Presets", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(titleLabel);

  searchBox.setTextToShowWhenEmpty("Search names, folders and tags", juce::Colours::grey);
  searchBox.setTitle("Search presets");
  searchBox.addListener(this);
  searchBox.addKeyListener(this);
  addAndMakeVisible(searchBox);

  const auto tabs = {std::pair{&allTab, Tab::All}, std::pair{&favouritesTab, Tab::Favourites},
                     std::pair{&ratedTab, Tab::Rated}, std::pair{&recentTab, Tab::Recent}};
  for (auto [button, which] : tabs) {
    button->setClickingTogglesState(true);
    button->setRadioGroupId(0x6b1b, juce::dontSendNotification);
    button->setWantsKeyboardFocus(false); // Tab moves search box <-> list only
    button->onClick = [this, which = which] { setTab(which); };
    addAndMakeVisible(*button);
  }
  allTab.setToggleState(true, juce::dontSendNotification);
  allTab.setTooltip("Every preset in the folder");
  favouritesTab.setTooltip("Presets you marked with a heart (F)");
  ratedTab.setTooltip("Presets you rated, best first");
  recentTab.setTooltip("Recently played, newest first");

  folderButton.setTooltip("Choose the preset folder");
  folderButton.onClick = [this] {
    if (source_.onChooseFolder) {
      source_.onChooseFolder();
    }
  };
  rescanButton.setTooltip("Rescan the preset folder");
  rescanButton.onClick = [this] {
    if (source_.onRescan) {
      source_.onRescan();
    }
  };
  closeButton.setTooltip("Close (Esc)");
  closeButton.onClick = [this] {
    if (onCloseRequested) {
      onCloseRequested();
    }
  };
  for (auto* button : {&folderButton, &rescanButton, &closeButton}) {
    button->setWantsKeyboardFocus(false);
    addAndMakeVisible(*button);
  }

  list.setModel(this);
  list.setRowHeight(rowHeight);
  list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
  list.setTitle("Preset list");
  list.addKeyListener(this);
  addAndMakeVisible(list);

  setFocusContainerType(FocusContainerType::keyboardFocusContainer);
  setSize(preferredWidth, preferredHeight);
}

PresetBrowser::~PresetBrowser() {
  stopTimer();
  list.setModel(nullptr);
}

void PresetBrowser::setSource(Source source) {
  source_ = std::move(source);
  loaded_ = false;
  recentTab.setVisible(static_cast<bool>(source_.recentPaths));
  resized();
}

void PresetBrowser::opened() {
  refresh();
  const int row = static_cast<int>(std::find(results_.begin(), results_.end(), current_) - results_.begin());
  if (row < static_cast<int>(results_.size())) {
    selectRow(row);
  }
  searchBox.grabKeyboardFocus();
  searchBox.selectAll(); // typing replaces the last search
}

void PresetBrowser::visibilityChanged() {
  if (isVisible()) {
    startTimerHz(4);
  } else {
    stopTimer();
  }
}

void PresetBrowser::refresh() {
  bool changed = false;

  const auto generation = source_.playlistGeneration ? source_.playlistGeneration() : 0;
  if (!loaded_ || generation != playlistGeneration_ || !source_.playlistGeneration) {
    auto names = source_.names ? source_.names() : std::vector<std::string>{};
    auto paths = source_.paths ? source_.paths() : std::vector<std::string>{};
    // Without a generation counter, compare instead (tests, simple shells).
    if (!loaded_ || source_.playlistGeneration || names != names_ || paths != paths_) {
      names_ = std::move(names);
      paths_ = std::move(paths);
      paths_.resize(names_.size());
      indexForPath_.clear();
      for (std::size_t i = 0; i < paths_.size(); ++i) {
        indexForPath_.emplace(paths_[i], static_cast<int>(i));
      }
      index_.rebuild(names_);
      metadataGeneration_ = ~std::uint64_t{0}; // force the metadata pass below
      changed = true;
    }
    playlistGeneration_ = generation;
    loaded_ = true;
  }

  const auto metadataGeneration = source_.metadataGeneration ? source_.metadataGeneration() : 0;
  if (metadataGeneration != metadataGeneration_) {
    metadataGeneration_ = metadataGeneration;
    const auto metadata = source_.metadata ? source_.metadata() : nullptr;
    infos_.assign(names_.size(), core::PresetInfo{});
    std::vector<std::string> tags(names_.size());
    if (metadata != nullptr && metadata->size() > 0) {
      for (std::size_t i = 0; i < paths_.size(); ++i) {
        infos_[i] = metadata->get(paths_[i]);
        tags[i] = core::PresetMetadata::joinTags(infos_[i].tags);
      }
    }
    index_.setExtraText(std::move(tags));
    changed = true;
  }

  if (source_.blacklistedPaths) {
    const auto blacklisted = source_.blacklistedPaths();
    std::unordered_set<std::string> set(blacklisted.begin(), blacklisted.end());
    if (set != blacklist_) {
      blacklist_ = std::move(set);
      list.repaint();
    }
  }
  if (source_.recentPaths) {
    auto recent = source_.recentPaths();
    if (recent != recent_) {
      recent_ = std::move(recent);
      changed = changed || tab_ == Tab::Recent;
    }
  }

  const int current = source_.currentIndex ? source_.currentIndex() : -1;
  if (current != current_) {
    current_ = current;
    list.repaint();
  }

  if (changed) {
    applyFilter();
  }
}

void PresetBrowser::setTab(Tab tab) {
  tab_ = tab;
  for (auto [button, which] : {std::pair{&allTab, Tab::All}, std::pair{&favouritesTab, Tab::Favourites},
                               std::pair{&ratedTab, Tab::Rated}, std::pair{&recentTab, Tab::Recent}}) {
    button->setToggleState(which == tab, juce::dontSendNotification);
  }
  applyFilter();
  list.scrollToEnsureRowIsOnscreen(std::max(list.getSelectedRow(), 0));
}

void PresetBrowser::textEditorTextChanged(juce::TextEditor&) { applyFilter(); }

void PresetBrowser::setSearchText(const juce::String& text) {
  searchBox.setText(text, juce::dontSendNotification);
  applyFilter();
}

void PresetBrowser::applyFilter() {
  const int keep = selectedIndex();
  const auto words = presetSearchWords(searchBox.getText());

  switch (tab_) {
  case Tab::All:
    results_ = index_.filter(words);
    break;
  case Tab::Favourites:
  case Tab::Rated:
    results_.clear();
    for (std::size_t i = 0; i < infos_.size(); ++i) {
      const auto& info = infos_[i];
      if ((tab_ == Tab::Favourites ? info.favourite : info.rating > 0) && index_.matches(static_cast<int>(i), words)) {
        results_.push_back(static_cast<int>(i));
      }
    }
    if (tab_ == Tab::Rated) {
      std::stable_sort(results_.begin(), results_.end(), [this](int a, int b) {
        return infos_[static_cast<std::size_t>(a)].rating > infos_[static_cast<std::size_t>(b)].rating;
      });
    }
    break;
  case Tab::Recent:
    results_.clear();
    for (const auto& path : recent_) {
      const auto it = indexForPath_.find(path);
      if (it != indexForPath_.end() && index_.matches(it->second, words)) {
        results_.push_back(it->second);
      }
    }
    break;
  }

  list.updateContent();
  const auto found = std::find(results_.begin(), results_.end(), keep);
  if (found != results_.end()) {
    selectRow(static_cast<int>(found - results_.begin()));
  } else if (!results_.empty() && !words.empty()) {
    selectRow(0); // Return plays the best match
  } else {
    list.deselectAllRows();
  }
  list.repaint();
  repaint(); // the count in the header
}

int PresetBrowser::selectedIndex() const {
  const int row = list.getSelectedRow();
  return row >= 0 && row < static_cast<int>(results_.size()) ? results_[static_cast<std::size_t>(row)] : -1;
}

juce::String PresetBrowser::countText() const {
  const auto total = names_.size();
  if (total == 0) {
    return "No presets";
  }
  if (tab_ == Tab::All && results_.size() == total) {
    return withThousands(total) + (total == 1 ? " preset" : " presets");
  }
  return withThousands(results_.size()) + " of " + withThousands(total) + " presets";
}

void PresetBrowser::selectRow(int row) {
  list.selectRow(row, /*dontScrollToShowThisRow=*/false, /*deselectOthersFirst=*/true);
}

void PresetBrowser::moveSelection(int delta) {
  if (results_.empty()) {
    return;
  }
  const int last = static_cast<int>(results_.size()) - 1;
  const int row = list.getSelectedRow();
  selectRow(row < 0 ? (delta > 0 ? 0 : last) : std::clamp(row + delta, 0, last));
}

void PresetBrowser::play(int index) {
  if (index >= 0 && source_.onPick) {
    source_.onPick(index);
    current_ = index; // shown at once; the next poll confirms it
    list.repaint();
  }
}

const std::string& PresetBrowser::pathFor(int index) const {
  static const std::string none;
  return index >= 0 && static_cast<std::size_t>(index) < paths_.size() ? paths_[static_cast<std::size_t>(index)] : none;
}

core::PresetInfo PresetBrowser::infoFor(int index) const {
  return index >= 0 && static_cast<std::size_t>(index) < infos_.size() ? infos_[static_cast<std::size_t>(index)]
                                                                      : core::PresetInfo{};
}

void PresetBrowser::updateInfo(int index, const core::PresetInfo& info) {
  if (index < 0 || static_cast<std::size_t>(index) >= infos_.size() || !source_.setInfo) {
    return;
  }
  infos_[static_cast<std::size_t>(index)] = info; // shown at once; the store's generation confirms it
  source_.setInfo(pathFor(index), info);
  list.repaint();
}

void PresetBrowser::toggleFavourite(int index) {
  auto info = infoFor(index);
  info.favourite = !info.favourite;
  updateInfo(index, info);
}

void PresetBrowser::setRating(int index, int rating) {
  auto info = infoFor(index);
  info.rating = std::clamp(rating, 0, 5);
  updateInfo(index, info);
}

int PresetBrowser::getNumRows() { return static_cast<int>(results_.size()); }

juce::String PresetBrowser::getNameForRow(int row) {
  if (row < 0 || row >= static_cast<int>(results_.size())) {
    return {};
  }
  const int index = results_[static_cast<std::size_t>(row)];
  const auto info = infoFor(index);
  auto name = juce::String(splitPresetName(names_[static_cast<std::size_t>(index)]).leaf);
  if (info.favourite) {
    name << ", favourite";
  }
  if (info.rating > 0) {
    name << ", " << info.rating << (info.rating == 1 ? " star" : " stars");
  }
  if (index == current_) {
    name << ", playing";
  }
  return name;
}

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
  if (row < 0 || row >= static_cast<int>(results_.size())) {
    return;
  }
  const int index = results_[static_cast<std::size_t>(row)];
  const auto& path = pathFor(index);
  const auto info = infoFor(index);
  const auto parts = splitPresetName(names_[static_cast<std::size_t>(index)]);
  const bool current = index == current_;
  const auto layout = PresetBrowserRowLayout::forRow(width, height);

  if (selected) {
    g.setColour(drawerTheme::pressFill);
    g.fillRect(0, 0, width, height);
  }
  if (current) {
    g.setColour(drawerTheme::accent);
    g.fillRect(0, 4, 3, height - 8);
  }

  const auto heart = heartPath(layout.heart.toFloat().withSizeKeepingCentre(16.0f, 15.0f));
  if (info.favourite) {
    g.setColour(kFavourite);
    g.fillPath(heart);
  } else {
    g.setColour(drawerTheme::hairline);
    g.strokePath(heart, juce::PathStrokeType(1.2f));
  }

  for (int star = 0; star < 5; ++star) {
    const auto cell = juce::Rectangle<int>(layout.stars.getX() + star * kStarCell, layout.stars.getY(), kStarCell,
                                           layout.stars.getHeight())
                          .toFloat()
                          .withSizeKeepingCentre(13.0f, 13.0f);
    const auto shape = starPath(cell);
    if (star < info.rating) {
      g.setColour(kStar);
      g.fillPath(shape);
    } else {
      g.setColour(drawerTheme::hairline);
      g.strokePath(shape, juce::PathStrokeType(1.0f));
    }
  }

  auto text = layout.text;
  auto top = text.removeFromTop(height / 2 + 2).withTrimmedTop(3);
  g.setColour(current ? drawerTheme::accent : drawerTheme::text);
  g.setFont(juce::FontOptions(14.0f, current ? juce::Font::bold : juce::Font::plain));
  g.drawText(juce::String::fromUTF8(parts.leaf.c_str()), top, juce::Justification::bottomLeft, true);

  juce::String detail = juce::String::fromUTF8(parts.folder.c_str());
  if (blacklist_.count(path) > 0) {
    detail = "blacklisted" + (detail.isEmpty() ? juce::String() : " - " + detail);
  } else if (info.neverAutoSelect) {
    detail = "never auto-selected" + (detail.isEmpty() ? juce::String() : " - " + detail);
  }
  if (!info.tags.empty()) {
    detail << "  #" << juce::String(core::PresetMetadata::joinTags(info.tags)).replace(", ", " #");
  }
  g.setColour(drawerTheme::textSecondary);
  g.setFont(juce::FontOptions(11.5f));
  g.drawText(detail, text.withTrimmedBottom(3), juce::Justification::topLeft, true);
}

void PresetBrowser::listBoxItemClicked(int row, const juce::MouseEvent& event) {
  if (row < 0 || row >= static_cast<int>(results_.size())) {
    return;
  }
  const int index = results_[static_cast<std::size_t>(row)];
  if (event.mods.isPopupMenu()) {
    showContextMenu(index);
    return;
  }
  const auto layout = PresetBrowserRowLayout::forRow(list.getVisibleRowWidth(), rowHeight);
  const auto point = event.getPosition();
  if (layout.heart.contains(point)) {
    toggleFavourite(index);
  } else if (const int star = layout.starAt(point); star > 0) {
    setRating(index, infoFor(index).rating == star ? 0 : star); // clicking the same star clears it
  } else {
    play(index);
  }
}

void PresetBrowser::returnKeyPressed(int row) {
  if (row >= 0 && row < static_cast<int>(results_.size())) {
    play(results_[static_cast<std::size_t>(row)]);
  }
}

bool PresetBrowser::keyPressed(const juce::KeyPress& key, juce::Component* origin) {
  return handleKey(key, origin == &list || (origin != nullptr && list.isParentOf(origin)));
}

bool PresetBrowser::handleKey(const juce::KeyPress& key, bool inList) {
  const int page = std::max(1, list.getHeight() / rowHeight - 1);

  if (key == juce::KeyPress::escapeKey) {
    if (onCloseRequested) {
      onCloseRequested();
    }
    return true;
  }
  if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
    moveSelection(key == juce::KeyPress::upKey ? -1 : 1);
    return true;
  }
  if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey) {
    moveSelection(key == juce::KeyPress::pageUpKey ? -page : page);
    return true;
  }
  if (key == juce::KeyPress::returnKey) {
    int index = selectedIndex();
    if (index < 0 && !results_.empty()) {
      index = results_.front();
    }
    play(index);
    return true;
  }
  if (!inList) {
    return false; // the search box takes everything else
  }
  if (key == juce::KeyPress::homeKey || key == juce::KeyPress::endKey) {
    if (!results_.empty()) {
      selectRow(key == juce::KeyPress::homeKey ? 0 : static_cast<int>(results_.size()) - 1);
    }
    return true;
  }
  const auto character = key.getTextCharacter();
  if (!key.getModifiers().isCommandDown() && !key.getModifiers().isAltDown()) {
    if (character == 'f' || character == 'F') {
      toggleFavourite(selectedIndex());
      return true;
    }
    if (character >= '0' && character <= '5') {
      setRating(selectedIndex(), character - '0');
      return true;
    }
    if (character >= ' ') {
      // Typing in the list searches.
      searchBox.grabKeyboardFocus();
      searchBox.moveCaretToEnd();
      searchBox.insertTextAtCaret(juce::String::charToString(character));
      return true;
    }
  }
  return false;
}

void PresetBrowser::showContextMenu(int index) {
  const auto path = pathFor(index);
  const auto info = infoFor(index);
  const bool blacklisted = blacklist_.count(path) > 0;
  const auto name = juce::String::fromUTF8(splitPresetName(names_[static_cast<std::size_t>(index)]).leaf.c_str());

  juce::PopupMenu menu;
  menu.setLookAndFeel(&getLookAndFeel());
  menu.addItem("Play", [this, index] { play(index); });
  menu.addItem(info.favourite ? "Remove from favourites" : "Add to favourites",
               [this, index] { toggleFavourite(index); });
  if (source_.setBlacklisted) {
    menu.addItem(blacklisted ? "Remove from blacklist" : "Add to blacklist", [this, path, blacklisted] {
      source_.setBlacklisted(path, !blacklisted);
    });
  }
  if (source_.setInfo) {
    const juce::Component::SafePointer<PresetBrowser> safe(this);
    PresetInfoAccess access{[safe, index] { return safe != nullptr ? safe->infoFor(index) : core::PresetInfo{}; },
                            [safe, index](const core::PresetInfo& changed) {
                              if (safe != nullptr) {
                                safe->updateInfo(index, changed);
                              }
                            },
                            source_.allTags ? source_.allTags : std::function<std::vector<std::string>()>{}};
    addPresetInfoSection(menu, name, access, this);
  }
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&list).withMousePosition());
}

void PresetBrowser::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  // Nearly solid: rows of small text over a busy picture, or the
  // diagnostics panel, must stay legible.
  g.setColour(juce::Colours::black.withAlpha(0.96f));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  // Folder and count, under the tabs.
  auto info = getLocalBounds().reduced(10, 0);
  info = info.withTop(allTab.getBottom() + 2).withHeight(18);
  g.setFont(juce::FontOptions(11.5f));
  g.setColour(drawerTheme::textSecondary);
  const auto count = countText();
  const int countWidth = juce::GlyphArrangement::getStringWidthInt(juce::Font(juce::FontOptions(11.5f)), count) + 4;
  g.drawText(count, info.removeFromRight(countWidth), juce::Justification::centredRight, false);
  const auto folder = source_.folder ? juce::String(source_.folder()) : juce::String();
  g.drawText(folder.isEmpty() ? juce::String("No preset folder") : folder, info.withTrimmedRight(8),
             juce::Justification::centredLeft, true);

  if (results_.empty()) {
    g.setFont(juce::FontOptions(13.0f));
    const auto message = names_.empty()                  ? juce::String("Choose a preset folder to start.")
                         : searchBox.getText().isNotEmpty() ? juce::String("No preset matches.")
                         : tab_ == Tab::Favourites         ? juce::String("No favourites yet: click a heart, or press F.")
                         : tab_ == Tab::Rated              ? juce::String("Nothing rated yet: click the stars, or press 1-5.")
                                                           : juce::String("Nothing played yet.");
    g.drawText(message, list.getBounds().reduced(12), juce::Justification::centredTop, true);
  }
}

void PresetBrowser::resized() {
  auto area = getLocalBounds().reduced(8, 6);
  auto header = area.removeFromTop(kHeaderHeight);
  closeButton.setBounds(header.removeFromRight(56));
  header.removeFromRight(4);
  rescanButton.setBounds(header.removeFromRight(64));
  header.removeFromRight(4);
  folderButton.setBounds(header.removeFromRight(70));
  titleLabel.setBounds(header);
  area.removeFromTop(kGap);

  searchBox.setBounds(area.removeFromTop(kHeaderHeight + 2));
  area.removeFromTop(kGap);

  auto tabs = area.removeFromTop(kHeaderHeight);
  std::vector<juce::TextButton*> visibleTabs{&allTab, &favouritesTab, &ratedTab};
  if (recentTab.isVisible()) {
    visibleTabs.push_back(&recentTab);
  }
  const int tabWidth = tabs.getWidth() / static_cast<int>(visibleTabs.size());
  for (auto* button : visibleTabs) {
    button->setBounds(tabs.removeFromLeft(tabWidth));
  }
  if (!visibleTabs.empty()) {
    visibleTabs.back()->setBounds(visibleTabs.back()->getBounds().withRight(tabs.getRight()));
  }

  area.removeFromTop(22); // folder and count line (painted)
  list.setBounds(area);
}

} // namespace milkdawp::ui
