// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>

#include <juce_gui_extra/juce_gui_extra.h>

#include "AppPreferences.h"
#include "AudioInput.h"
#include "ParameterBinding.h"
#include "SignalMonitor.h"
#include "milkdawp/engine/OutputSurface.h"
#include "milkdawp/engine/OutputWindow.h"
#include "milkdawp/engine/Visualizer.h"
#include "milkdawp/ui/ControlDrawer.h"
#include "milkdawp/ui/DetachedControlsWindow.h"
#include "milkdawp/ui/TransitionSettings.h"

namespace milkdawp::app {

/// The app's main window content (4.1, §4.9): the whole area is an
/// `engine::OutputSurface`, with the shared `ControlDrawer`, the transition
/// settings popover, the diagnostics overlay and the input hint composited as
/// its children. It offers everything the plugin editor does, bound to the
/// app's own parameter values (`ParameterBinding`) instead of a host's.
///
/// Every action is a public method, so the drawer, the keyboard shortcuts
/// (§4.9, with the app-only Space), the settings popup and the desktop
/// shell's menu bar all call the same code. Window-level concerns (the menu
/// bar, fullscreen of the main window, geometry) belong to `MainWindow`
/// (ADR-0010's shell boundary); this component only asks for them through
/// the `on...` callbacks.
///
/// Remembers what it owns in the shared `AppState` and calls
/// `onStateChanged` whenever something worth saving changed.
class MainComponent final : public juce::Component, private juce::Timer {
public:
  MainComponent(engine::Visualizer& visualizer, AudioInput& input, AppState& state);
  ~MainComponent() override;

  // ---- actions ----
  void previousPreset();
  void nextPreset();
  void choosePresetFolder();
  void rescanPresets();
  void showPresetPicker();
  void showAudioSettings();
  void setTransitionSettingsVisible(bool visible);
  void toggleOutputWindow(bool fullscreen);
  void setControlsFloating(bool floating);
  void setDiagnosticsVisible(bool visible);
  void setLoggingEnabled(bool enabled);
  [[nodiscard]] ParameterBinding& parameters() noexcept { return binding_; }
  [[nodiscard]] ui::ControlDrawer& drawer() noexcept { return drawer_; }

  [[nodiscard]] bool isTransitionSettingsVisible() const { return transitionSettings_.isVisible(); }
  [[nodiscard]] bool isOutputWindowOpen() const noexcept { return outputWindow_ != nullptr; }
  [[nodiscard]] bool areControlsFloating() const noexcept { return controlsWindow_ != nullptr; }
  [[nodiscard]] bool isDiagnosticsVisible() const { return diagnosticsLabel_.isVisible(); }

  /// The actions as menus: the menu bar's "File", "Playback" and "View"
  /// (`menuNames()`), and the drawer's settings popup (all of them at once).
  [[nodiscard]] static juce::StringArray menuNames();
  [[nodiscard]] juce::PopupMenu createMenu(int menuIndex);

  /// The main window tells this component when it enters or leaves
  /// fullscreen: the drawer auto-hides in fullscreen and returns to the
  /// user's pin choice outside it (§4.9).
  void mainFullscreenChanged(bool fullscreen);
  /// Reopens the Output window and floating controls as they were saved.
  /// Called once the main window is on screen.
  void restoreSecondaryWindows();

  /// F11 on the main window; Esc while it is fullscreen.
  std::function<void()> onToggleMainFullscreen;
  std::function<bool()> isMainFullscreen;
  /// Something in `AppState` changed (the owner saves it).
  std::function<void()> onStateChanged;
  /// The logging preference changed (the owner starts or stops the log file).
  std::function<void(bool)> onLoggingChanged;

  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;

private:
  void timerCallback() override;
  void publishControls();
  void layoutTransitionSettings();
  void updateInputHint();
  void updateStatusText();
  void notifyStateChanged();

  engine::Visualizer& visualizer_;
  AudioInput& input_;
  AppState& state_;

  engine::OutputSurface surface_;
  juce::Label diagnosticsLabel_;
  juce::TextButton inputHint_;
  ui::ControlDrawer drawer_;
  ui::TransitionSettingsPanel transitionSettings_;
  juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow_;
  // After every widget it binds: destroyed first.
  ParameterBinding binding_;

  SignalMonitor signalMonitor_;
  bool pinnedBeforeFullscreen_ = false;
  bool fullscreen_ = false;
  std::unique_ptr<juce::FileChooser> folderChooser_;
  std::unique_ptr<engine::OutputWindow> outputWindow_;
  // Declared after drawer_ so it is destroyed first: it only borrows the
  // drawer, and hands it back in its destructor.
  std::unique_ptr<ui::DetachedControlsWindow> controlsWindow_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace milkdawp::app
