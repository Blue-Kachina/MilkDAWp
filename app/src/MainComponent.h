// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>

#include <juce_gui_extra/juce_gui_extra.h>

#include "AppPreferences.h"
#include "AudioSourceRouter.h"
#include "MidiLearn.h"
#include "ParameterBinding.h"
#include "SignalMonitor.h"
#include "milkdawp/engine/OutputSurface.h"
#include "milkdawp/engine/OscRemote.h"
#include "milkdawp/engine/OutputWindow.h"
#include "milkdawp/engine/UpdateCheck.h"
#include "milkdawp/engine/Visualizer.h"
#include "milkdawp/ui/AboutPanel.h"
#include "milkdawp/ui/ControlDrawer.h"
#include "milkdawp/ui/DiagnosticsPanel.h"
#include "milkdawp/ui/DetachedControlsWindow.h"
#include "milkdawp/ui/OutputSettings.h"
#include "milkdawp/ui/PresetBrowser.h"
#include "milkdawp/ui/PresetInfoMenu.h"
#include "milkdawp/ui/TransitionSettings.h"
#include "milkdawp/ui/VisualSettings.h"

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
class MainComponent final : public juce::Component, private juce::Timer, private juce::FileDragAndDropTarget {
public:
  MainComponent(engine::Visualizer& visualizer, AudioSourceRouter& input, AppState& state);
  ~MainComponent() override;

  // ---- actions ----
  void previousPreset();
  void nextPreset();
  void choosePresetFolder();
  void rescanPresets();
  /// 6.1b: the preset browser popover (preset title, `B`, File > Browse
  /// presets). Replaces the popup picker and the separate library window.
  void setPresetBrowserVisible(bool visible);
  [[nodiscard]] bool isPresetBrowserVisible() const { return presetBrowser_.isVisible(); }
  void showAudioSettings();
  /// A `.milk` file (loads its folder, selects the file) or a folder
  /// (loads it) from a command-line argument, another instance's, or a
  /// drop (§4.6). Ignored if `path` is neither.
  void openPath(const juce::String& path);
  void setTransitionSettingsVisible(bool visible);
  /// Phase 8.1: the Visual popover (globals, Macros, gate).
  void setVisualSettingsVisible(bool visible);
  [[nodiscard]] bool isVisualSettingsVisible() const { return visualSettings_.isVisible(); }
  /// 8.6: the image Media Mix shows (empty: none), and a chooser for one.
  void setMediaSource(const juce::String& path);
  void chooseMediaSource();
  /// Settings -> Output: fullscreen default and which screen the window opens on.
  void setOutputSettingsVisible(bool visible);
  /// The drawer's Output button: closes the window if open, else opens it
  /// (fullscreen if Settings -> Output says to).
  void toggleOutputWindowFromDrawer();
  void toggleOutputWindow(bool fullscreen);
  void setControlsFloating(bool floating);
  void setDiagnosticsVisible(bool visible);
  /// 6.7: Help > About, with the opt-in update check.
  void setAboutVisible(bool visible);
  /// Main.cpp calls this at start-up: a check if the user turned them on and
  /// one is due. Not from the constructor, so tests never go online.
  void checkForUpdatesIfDue() { updateChecker_.checkIfDue(); }
  void setLoggingEnabled(bool enabled);
  [[nodiscard]] ParameterBinding& parameters() noexcept { return binding_; }
  [[nodiscard]] ui::ControlDrawer& drawer() noexcept { return drawer_; }

  [[nodiscard]] bool isTransitionSettingsVisible() const { return transitionSettings_.isVisible(); }
  [[nodiscard]] bool isOutputSettingsVisible() const { return outputSettings_.isVisible(); }
  [[nodiscard]] bool isOutputWindowOpen() const noexcept { return outputWindow_ != nullptr; }
  [[nodiscard]] bool areControlsFloating() const noexcept { return controlsWindow_ != nullptr; }
  [[nodiscard]] bool isDiagnosticsVisible() const { return diagnosticsPanel_.isVisible(); }

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
  /// File > Show log file / Collect logs... (4.10): the owner has the log
  /// file, the crash reports and the settings file.
  std::function<void()> onShowLogFile;
  std::function<void()> onCollectLogs;

  /// What the diagnostics panel shows (5.9), with the app's own fields.
  [[nodiscard]] core::DiagnosticsInfo diagnosticsInfo() const;
  /// The diagnostics panel's text with every recent error, also put
  /// in a log bundle.
  [[nodiscard]] juce::String diagnosticsText() const;

  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;

private:
  void timerCallback() override;
  void publishControls();
  void layoutTransitionSettings();
  void layoutVisualSettings();
  void layoutOutputSettings();
  void layoutPresetBrowser();
  void layoutAbout();
  void refreshAboutUpdateStatus();
  /// Favourites used to be app-only (4.5); since 6.1b they are in the shared
  /// preset metadata. Moves any saved ones there once.
  void migrateFavourites();
  void updateInputHint();
  void updateStatusText();
  void notifyStateChanged();
  void recordRecentlyPlayed(const juce::String& absolutePath);
  /// Phase 8.1: on a preset change, un-locked Macros move to its defaults.
  void applyMacroLock();

  // juce::FileDragAndDropTarget (§4.6): a .milk file or a preset folder
  // dropped on the main window.
  bool isInterestedInFileDrag(const juce::StringArray& files) override;
  void filesDropped(const juce::StringArray& files, int, int) override;

  engine::Visualizer& visualizer_;
  AudioSourceRouter& input_;
  AppState& state_;

  engine::OutputSurface surface_;
  ui::DiagnosticsPanel diagnosticsPanel_; // 5.9
  juce::TextButton inputHint_;
  ui::ControlDrawer drawer_;
  ui::TransitionSettingsPanel transitionSettings_;
  ui::VisualSettingsPanel visualSettings_; // Phase 8.1
  ui::OutputSettingsPanel outputSettings_;
  ui::PresetBrowser presetBrowser_; // 6.1b
  ui::AboutPanel aboutPanel_;       // 6.7
  engine::UpdateChecker updateChecker_; // 6.7, opt-in
  juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow_;
  // After every widget it binds: destroyed first.
  ParameterBinding binding_;
  // After binding_, which it calls into; before it, which is what it binds.
  MidiLearn midiLearn_;

  SignalMonitor signalMonitor_;
  bool pinnedBeforeFullscreen_ = false;
  bool fullscreen_ = false;
  bool presetSeen_ = false; // a preset has played this session (Lock Macros)
  // 8.4: the process's OSC remote; this component is its one endpoint.
  juce::SharedResourcePointer<engine::OscRemote> osc_;
  int oscHandle_ = 0;
  std::unique_ptr<juce::FileChooser> folderChooser_;
  std::unique_ptr<juce::FileChooser> mediaChooser_; // 8.6
  std::unique_ptr<engine::OutputWindow> outputWindow_;
  // Declared after drawer_ so it is destroyed first: it only borrows the
  // drawer, and hands it back in its destructor.
  std::unique_ptr<ui::DetachedControlsWindow> controlsWindow_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace milkdawp::app
