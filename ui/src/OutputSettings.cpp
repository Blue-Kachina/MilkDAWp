// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/OutputSettings.h"

#include <algorithm>
#include <span>

#include "milkdawp/core/DisplayLayout.h"

namespace milkdawp::ui {

namespace {
constexpr int kRowHeight = 24;
constexpr int kGap = 4;
constexpr int kMapHeight = 100;
constexpr int kNoteHeight = 16;
constexpr int kScreenLabelWidth = 56;
constexpr int kFrameThickness = 6;
const juce::Colour kAccent{0xff3fa9f5};

core::WindowBounds toWindowBounds(juce::Rectangle<int> r) noexcept {
  return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}

std::vector<core::WindowBounds> idsOf(const std::vector<DisplayEntry>& displays) {
  std::vector<core::WindowBounds> ids;
  ids.reserve(displays.size());
  for (const auto& d : displays) {
    ids.push_back(d.id);
  }
  return ids;
}
} // namespace

std::vector<DisplayEntry> currentDisplays() {
  std::vector<DisplayEntry> result;
  for (const auto& display : juce::Desktop::getInstance().getDisplays().displays) {
    result.push_back({toWindowBounds(display.userBounds.toNearestInt()), display.logicalBounds.toNearestInt(),
                      display.isMain});
  }
  return result;
}

DisplayHighlightFrame::DisplayHighlightFrame() {
  setOpaque(false);
  setInterceptsMouseClicks(false, false);
}

DisplayHighlightFrame::~DisplayHighlightFrame() { hide(); }

void DisplayHighlightFrame::showAround(juce::Rectangle<int> screenBounds) {
  if (!isOnDesktop()) {
    addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresMouseClicks |
                 juce::ComponentPeer::windowIgnoresKeyPresses);
  }
  setBounds(screenBounds);
  setVisible(true);
  toFront(false);
}

void DisplayHighlightFrame::hide() {
  setVisible(false);
  if (isOnDesktop()) {
    removeFromDesktop();
  }
}

void DisplayHighlightFrame::paint(juce::Graphics& g) {
  g.setColour(kAccent);
  g.drawRect(getLocalBounds(), kFrameThickness);
  g.setColour(juce::Colours::white.withAlpha(0.7f));
  g.drawRect(getLocalBounds().reduced(kFrameThickness), 1);
}

OutputSettingsPanel::OutputSettingsPanel() {
  titleLabel.setText("Output", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(titleLabel);

  closeButton.setTooltip("Close (Esc)");
  closeButton.onClick = [this] {
    if (onCloseRequested) {
      onCloseRequested();
    }
  };
  addAndMakeVisible(closeButton);

  fullscreenToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  fullscreenToggle.setTooltip("The pop-out button opens the Output window fullscreen");
  fullscreenToggle.onClick = [this] {
    if (onDefaultFullscreenChanged) {
      onDefaultFullscreenChanged(fullscreenToggle.getToggleState());
    }
  };
  addAndMakeVisible(fullscreenToggle);

  screenLabel.setText("Screen", juce::dontSendNotification);
  screenLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  screenLabel.setJustificationType(juce::Justification::centredLeft);
  addChildComponent(screenLabel);

  automaticToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  automaticToggle.setTooltip("Open where the window was last, or on the main screen");
  automaticToggle.setClickingTogglesState(false);
  automaticToggle.onClick = [this] {
    target_ = {};
    if (onTargetDisplayChanged) {
      onTargetDisplayChanged(target_);
    }
    automaticToggle.setToggleState(true, juce::dontSendNotification);
    noteLabel.setVisible(false);
    repaint();
  };
  addChildComponent(automaticToggle);

  noteLabel.setText("The saved screen isn't connected; using automatic.", juce::dontSendNotification);
  noteLabel.setFont(juce::FontOptions(12.0f));
  noteLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
  addChildComponent(noteLabel);

  rebuildDisplays();
  setSize(preferredWidth, preferredHeight());
}

OutputSettingsPanel::~OutputSettingsPanel() { highlight_.hide(); }

int OutputSettingsPanel::preferredHeight() const {
  int height = 6 + kRowHeight + kGap + kRowHeight + 6; // header + fullscreen toggle
  if (hasPicker()) {
    height += kGap + kRowHeight + kGap + kMapHeight + kNoteHeight;
  }
  return height;
}

int OutputSettingsPanel::selectedIndex() const { return core::findDisplay(idsOf(displays_), target_); }

void OutputSettingsPanel::rebuildDisplays() {
  displays_ = currentDisplays();
  const bool picker = hasPicker();
  screenLabel.setVisible(picker);
  automaticToggle.setVisible(picker);
  automaticToggle.setToggleState(selectedIndex() < 0, juce::dontSendNotification);
  noteLabel.setVisible(picker && !target_.isEmpty() && selectedIndex() < 0);
  if (hovered_ >= static_cast<int>(displays_.size())) {
    setHovered(-1);
  }
}

void OutputSettingsPanel::refresh(bool defaultFullscreen, const core::WindowBounds& targetDisplay) {
  fullscreenToggle.setToggleState(defaultFullscreen, juce::dontSendNotification);
  target_ = targetDisplay;
  rebuildDisplays();
  resized();
  repaint();
}

void OutputSettingsPanel::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(juce::Colours::black.withAlpha(0.85f));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  const auto selected = selectedIndex();
  for (std::size_t i = 0; i < tiles_.size(); ++i) {
    const auto tile = tiles_[i].toFloat();
    const bool isSelected = static_cast<int>(i) == selected;
    const bool isHovered = static_cast<int>(i) == hovered_;
    g.setColour(isSelected ? kAccent.withAlpha(0.55f) : juce::Colours::white.withAlpha(isHovered ? 0.28f : 0.12f));
    g.fillRoundedRectangle(tile, 3.0f);
    g.setColour(isSelected || isHovered ? kAccent : juce::Colours::white.withAlpha(0.35f));
    g.drawRoundedRectangle(tile, 3.0f, isSelected || isHovered ? 2.0f : 1.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText(juce::String(i + 1) + (displays_[i].primary ? " (main)" : ""), tiles_[i], juce::Justification::centred);
  }
}

void OutputSettingsPanel::resized() {
  auto area = getLocalBounds().reduced(8, 6);
  auto header = area.removeFromTop(kRowHeight);
  closeButton.setBounds(header.removeFromRight(60));
  titleLabel.setBounds(header);
  area.removeFromTop(kGap);
  fullscreenToggle.setBounds(area.removeFromTop(kRowHeight));

  tiles_.clear();
  mapArea_ = {};
  if (!hasPicker()) {
    return;
  }
  area.removeFromTop(kGap);
  auto row = area.removeFromTop(kRowHeight);
  screenLabel.setBounds(row.removeFromLeft(kScreenLabelWidth));
  automaticToggle.setBounds(row);
  area.removeFromTop(kGap);
  mapArea_ = area.removeFromTop(kMapHeight);
  noteLabel.setBounds(area.removeFromTop(kNoteHeight));

  const auto ids = idsOf(displays_);
  for (const auto& tile : core::layoutDisplayMap(ids, mapArea_.getWidth(), mapArea_.getHeight())) {
    tiles_.emplace_back(mapArea_.getX() + tile.x, mapArea_.getY() + tile.y, tile.width, tile.height);
  }
}

int OutputSettingsPanel::tileAt(juce::Point<int> position) const {
  for (std::size_t i = 0; i < tiles_.size(); ++i) {
    if (tiles_[i].contains(position)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void OutputSettingsPanel::setHovered(int tile) {
  if (tile == hovered_) {
    return;
  }
  hovered_ = tile;
  if (tile >= 0 && tile < static_cast<int>(displays_.size())) {
    highlight_.showAround(displays_[static_cast<std::size_t>(tile)].frame);
  } else {
    highlight_.hide();
  }
  repaint();
}

void OutputSettingsPanel::mouseMove(const juce::MouseEvent& event) { setHovered(tileAt(event.getPosition())); }

void OutputSettingsPanel::mouseExit(const juce::MouseEvent&) { setHovered(-1); }

void OutputSettingsPanel::mouseUp(const juce::MouseEvent& event) {
  const auto tile = tileAt(event.getPosition());
  if (tile < 0 || tile >= static_cast<int>(displays_.size())) {
    return;
  }
  target_ = displays_[static_cast<std::size_t>(tile)].id;
  automaticToggle.setToggleState(false, juce::dontSendNotification);
  noteLabel.setVisible(false);
  repaint();
  if (onTargetDisplayChanged) {
    onTargetDisplayChanged(target_);
  }
}

void OutputSettingsPanel::visibilityChanged() {
  if (isVisible()) {
    startTimer(1000); // notice a monitor being plugged in or out while open
  } else {
    stopTimer();
    setHovered(-1);
  }
}

void OutputSettingsPanel::timerCallback() {
  const auto before = idsOf(displays_);
  const auto current = idsOf(currentDisplays());
  if (before == current) {
    return;
  }
  const bool pickerChanged = (before.size() > 1) != (current.size() > 1);
  setHovered(-1);
  rebuildDisplays();
  if (pickerChanged && onPreferredSizeChanged) {
    onPreferredSizeChanged();
  }
  resized();
  repaint();
}

} // namespace milkdawp::ui
