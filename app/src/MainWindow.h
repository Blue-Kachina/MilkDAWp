// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>

#include <juce_gui_extra/juce_gui_extra.h>

#include "AppPreferences.h"
#include "MainComponent.h"

namespace milkdawp::app {

/// The desktop app's main window (4.1): a native-titled, resizable window
/// holding `MainComponent`, with a menu bar built from the component's own
/// actions (the macOS menu bar on a Mac, a window menu bar elsewhere).
/// Desktop-shell code by ADR-0010's boundary rules: an Android shell hosts
/// `MainComponent` without any of this.
///
/// Fullscreen (F11, View > Fullscreen) makes this window the kiosk-mode
/// component on its display, with the menu bar hidden; unlike the plugin,
/// the app owns the whole process, so kiosk mode is fine here (§4.9).
/// Geometry and fullscreen go into `AppState`, and the window reopens where
/// it was, falling back to a centred default if that display is gone.
class MainWindow final : public juce::DocumentWindow, private juce::MenuBarModel {
public:
  MainWindow(const juce::String& title, std::unique_ptr<MainComponent> content, AppState& state);
  ~MainWindow() override;

  void setFullscreen(bool shouldBeFullscreen);
  [[nodiscard]] bool isFullscreen() const noexcept { return fullscreen_; }

  /// Something in `AppState` changed (geometry, fullscreen).
  std::function<void()> onStateChanged;

  void closeButtonPressed() override;
  void moved() override;
  void resized() override;

private:
  juce::StringArray getMenuBarNames() override { return MainComponent::menuNames(); }
  juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
  void menuItemSelected(int, int) override {}

  void rememberBounds();

  AppState& state_;
  MainComponent* content_ = nullptr; // owned by the window
  bool fullscreen_ = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace milkdawp::app
