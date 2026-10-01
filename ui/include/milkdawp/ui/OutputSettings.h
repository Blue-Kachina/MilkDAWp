// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/StateSchema.h"

namespace milkdawp::ui {

/// One connected display, as the Output settings and the shell see it.
struct DisplayEntry {
  /// Identity saved in the state: the display's own bounds (JUCE has no
  /// stable monitor id). The same value `core::findDisplay` matches against.
  core::WindowBounds id;
  /// The whole monitor, in logical pixels: what fullscreen covers and what
  /// the highlight frame is drawn around.
  juce::Rectangle<int> frame;
  bool primary = false;
};

/// The displays JUCE reports right now, primary first-class but kept in
/// JUCE's order.
[[nodiscard]] std::vector<DisplayEntry> currentDisplays();

/// A thin, click-through frame drawn around a whole monitor, so the user can
/// tell which physical screen a picker tile stands for. A borderless
/// transparent desktop window of its own: it never takes focus or mouse input.
class DisplayHighlightFrame final : public juce::Component {
public:
  DisplayHighlightFrame();
  ~DisplayHighlightFrame() override;

  void showAround(juce::Rectangle<int> screenBounds);
  void hide();

  void paint(juce::Graphics& g) override;

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DisplayHighlightFrame)
};

/// Settings -> Output (M0 in layers_like_shrek.md): whether the Output window
/// opens fullscreen, and which screen it opens on. The screen picker only
/// appears when more than one monitor is connected; hovering a monitor tile
/// draws a frame around the real monitor, clicking it chooses it.
///
/// Like `TransitionSettingsPanel` it is a child of the editor's output
/// surface, never its own desktop window, and holds no processor state: the
/// shell calls `refresh()` with the saved layout and acts on the callbacks.
class OutputSettingsPanel : public juce::Component, private juce::Timer {
public:
  static constexpr int preferredWidth = 320;

  OutputSettingsPanel();
  ~OutputSettingsPanel() override;

  /// Height for the displays connected right now: taller when there is a
  /// screen picker to show.
  [[nodiscard]] int preferredHeight() const;

  /// Shows the saved settings and re-reads the connected displays.
  void refresh(bool defaultFullscreen, const core::WindowBounds& targetDisplay);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseMove(const juce::MouseEvent& event) override;
  void mouseExit(const juce::MouseEvent& event) override;
  void mouseUp(const juce::MouseEvent& event) override;
  void visibilityChanged() override;

  std::function<void()> onCloseRequested;
  /// A monitor was plugged in or out, so `preferredHeight()` changed: the
  /// shell re-lays the panel out.
  std::function<void()> onPreferredSizeChanged;
  std::function<void(bool)> onDefaultFullscreenChanged;
  /// An empty `WindowBounds` means automatic.
  std::function<void(const core::WindowBounds&)> onTargetDisplayChanged;

private:
  void timerCallback() override;
  /// Index of the map tile under `position`, or -1.
  [[nodiscard]] int tileAt(juce::Point<int> position) const;
  void setHovered(int tile);
  void rebuildDisplays();
  [[nodiscard]] bool hasPicker() const noexcept { return displays_.size() > 1; }
  /// The selected display's index, or -1 for automatic (or not connected).
  [[nodiscard]] int selectedIndex() const;

  juce::Label titleLabel;
  juce::TextButton closeButton{"Close"};
  juce::ToggleButton fullscreenToggle{"Open fullscreen"};
  juce::Label screenLabel;
  juce::ToggleButton automaticToggle{"Automatic"};
  juce::Label noteLabel;

  std::vector<DisplayEntry> displays_;
  std::vector<juce::Rectangle<int>> tiles_; // map tiles, panel coordinates, same order as displays_
  juce::Rectangle<int> mapArea_;
  core::WindowBounds target_;
  int hovered_ = -1;
  DisplayHighlightFrame highlight_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputSettingsPanel)
};

} // namespace milkdawp::ui
