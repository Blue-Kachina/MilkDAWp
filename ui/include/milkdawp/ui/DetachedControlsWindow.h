// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// Detached controls (§4.9, Phase 3.13): a small window of our own that
/// hosts the existing `ControlDrawer` -- the same component, moved, not a
/// copy -- for the projector-plus-laptop setup. The window never owns the
/// drawer: the shell hands it over with `setContentNonOwned` semantics and
/// takes it back on dock, so the drawer's wiring and attachments stay put.
///
/// Closing the window docks the controls (`onDockRequested`); keys go to
/// `onKeyPressed` so the shared shortcut table works here too (§4.9: windows
/// we own receive keys unconditionally).
class DetachedControlsWindow final : public juce::DocumentWindow {
public:
  static constexpr const char* kTitle = "MilkDAWp Controls";

  /// `controls` must outlive the window or be taken back with release().
  /// Its current height becomes the window's fixed content height.
  explicit DetachedControlsWindow(juce::Component& controls);
  ~DetachedControlsWindow() override;

  /// Shows the window at `bounds`, or below the centre of the main display
  /// if they are empty or on no connected display.
  void show(juce::Rectangle<int> bounds);
  /// Detaches the controls so the shell can re-parent them.
  void release();

  std::function<void()> onDockRequested;
  std::function<bool(const juce::KeyPress&)> onKeyPressed;
  /// Called after the window moves or resizes.
  std::function<void()> onLayoutChanged;

  void closeButtonPressed() override;
  bool keyPressed(const juce::KeyPress& key) override;
  void moved() override;
  void resized() override;

private:
  const int rowHeight_; // the controls' height when handed over

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DetachedControlsWindow)
};

} // namespace milkdawp::ui
