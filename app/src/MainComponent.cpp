// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MainComponent.h"

#include <algorithm>
#include <utility>

#include "AudioSettingsPanel.h"
#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/ui/Icons.h"
#include "milkdawp/ui/PresetMenu.h"
#include "milkdawp/ui/Shortcuts.h"

namespace milkdawp::app {

namespace {

constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

enum MenuIndex { FileMenu = 0, PlaybackMenu = 1, ViewMenu = 2 };

juce::PopupMenu::Item makeItem(const juce::String& text, std::function<void()> action, bool ticked = false,
                               const juce::String& shortcut = {}, bool enabled = true) {
  juce::PopupMenu::Item item(text);
  item.setTicked(ticked);
  item.setEnabled(enabled);
  item.shortcutKeyDescription = shortcut;
  item.setAction(std::move(action));
  return item;
}

} // namespace

MainComponent::MainComponent(engine::Visualizer& visualizer, AudioInput& input, AppState& state)
    : visualizer_(visualizer), input_(input), state_(state), surface_(visualizer.renderEngine()),
      drawer_(ui::DrawerStateMachine::Config{.startPinned = state.drawerPinned}),
      binding_(state.parameters, [this] {
        publishControls();
        if (transitionSettings_.isVisible()) {
          transitionSettings_.refreshRelevance();
        }
        notifyStateChanged();
      }) {
  addAndMakeVisible(surface_);

  // Everything drawn over the video is a child of the surface, never a
  // sibling: JUCE only composites a GL component's own children over its
  // frame (§4.11, 2.3's finding).
  diagnosticsLabel_.setJustificationType(juce::Justification::topLeft);
  diagnosticsLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
  diagnosticsLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::black.withAlpha(0.5f));
  surface_.addChildComponent(diagnosticsLabel_);
  diagnosticsLabel_.setVisible(state_.showDiagnostics);

  inputHint_.setColour(juce::TextButton::buttonColourId, juce::Colours::black.withAlpha(0.6f));
  inputHint_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  inputHint_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  inputHint_.onClick = [this] { showAudioSettings(); };
  surface_.addChildComponent(inputHint_);

  surface_.addAndMakeVisible(drawer_);
  drawer_.prevButton.onClick = [this] { previousPreset(); };
  drawer_.nextButton.onClick = [this] { nextPreset(); };
  drawer_.outputButton.onClick = [this] { toggleOutputWindow(false); };
  drawer_.settingsButton.onClick = [this] {
    auto menu = createMenu(-1);
    menu.setLookAndFeel(&drawer_.getLookAndFeel());
    menu.showMenuAsync(ui::DrawerLookAndFeel::menuOptions(drawer_.settingsMenuAnchor()));
  };
  drawer_.onPresetTitleClicked = [this] { showPresetPicker(); };

  // Above the drawer; hidden until chosen.
  surface_.addChildComponent(transitionSettings_);
  transitionSettings_.onCloseRequested = [this] { setTransitionSettingsVisible(false); };

  binding_.bind(drawer_.lockButton, "lockCurrentPreset");
  binding_.bind(drawer_.shuffleButton, "shuffle");
  binding_.bind(drawer_.transitionModeCombo, "transitionMode");
  binding_.bind(transitionSettings_.modeCombo, "transitionMode");
  binding_.bind(transitionSettings_.barsSlider, "transitionBars");
  binding_.bind(transitionSettings_.timedDurationSlider, "transitionDurationSeconds");
  binding_.bind(transitionSettings_.jitterMinSlider, "transitionDurationMin");
  binding_.bind(transitionSettings_.jitterMaxSlider, "transitionDurationMax");
  binding_.bind(transitionSettings_.energyThresholdSlider, "energyThreshold");
  binding_.bind(transitionSettings_.blendSlider, "softCutDuration");
  binding_.bind(transitionSettings_.jitterToggle, "transitionJitterEnabled");
  binding_.bind(transitionSettings_.hardCutToggle, "hardCutEnabled");
  transitionSettings_.refreshRelevance();

  input_.onDeviceChanged = [this] {
    state_.audioDeviceState = input_.stateXml();
    juce::Logger::writeToLog("Audio input: " + input_.describe());
    notifyStateChanged();
  };

  publishControls();
  setWantsKeyboardFocus(true);
  setSize(kDefaultWidth, kDefaultHeight);
  timerCallback();
  startTimerHz(10);
}

MainComponent::~MainComponent() {
  stopTimer();
  input_.onDeviceChanged = nullptr;
  // Hand the drawer back before it is destroyed; the saved layout stays as
  // it is, so the next run floats the controls again.
  controlsWindow_.reset();
  outputWindow_.reset();
}

// ---- actions ----

void MainComponent::previousPreset() { visualizer_.director().requestPrevious(); }

void MainComponent::nextPreset() { visualizer_.director().requestNext(); }

void MainComponent::choosePresetFolder() {
  const auto current = visualizer_.director().presetFolder();
  folderChooser_ = std::make_unique<juce::FileChooser>(
      "Choose a folder of MilkDrop presets (.milk)",
      current.empty() ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory) : juce::File(current));
  folderChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this](const juce::FileChooser& chooser) {
                                const auto result = chooser.getResult();
                                if (!result.isDirectory()) {
                                  return;
                                }
                                visualizer_.director().setPresetFolder(result.getFullPathName().toStdString());
                                state_.presetFolder = result.getFullPathName();
                                state_.currentPresetPath = {};
                                juce::Logger::writeToLog("Preset folder: " + state_.presetFolder);
                                notifyStateChanged();
                              });
}

void MainComponent::rescanPresets() { visualizer_.director().rescan(); }

void MainComponent::showPresetPicker() {
  auto& director = visualizer_.director();
  const auto folder = director.presetFolder();

  juce::PopupMenu menu;
  menu.setLookAndFeel(&drawer_.getLookAndFeel());
  menu.addSectionHeader(folder.empty() ? juce::String("No preset folder") : juce::String(folder));
  const auto menuIcon = [](ui::Icon icon) { return ui::createIconDrawable(icon, ui::drawerTheme::menuText); };
  juce::PopupMenu::Item choose("Choose preset folder...");
  choose.setImage(menuIcon(ui::Icon::Folder));
  choose.setAction([this] { choosePresetFolder(); });
  menu.addItem(std::move(choose));
  juce::PopupMenu::Item rescan("Rescan preset folder");
  rescan.setImage(menuIcon(ui::Icon::Rescan));
  rescan.setEnabled(!folder.empty());
  rescan.setAction([this] { rescanPresets(); });
  menu.addItem(std::move(rescan));

  if (const auto names = director.presetNames(); !names.empty()) {
    menu.addSeparator();
    ui::addPresetTree(menu, ui::buildPresetTree(names), director.status().currentIndex,
                      [this](int index) { visualizer_.director().requestPreset(index); });
  }
  menu.showMenuAsync(ui::DrawerLookAndFeel::menuOptions(drawer_.presetTitleComponent()));
}

void MainComponent::showAudioSettings() {
  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(new AudioSettingsPanel(input_));
  options.dialogTitle = "Audio Input";
  options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.componentToCentreAround = this;
  options.launchAsync();
}

void MainComponent::setTransitionSettingsVisible(bool visible) {
  transitionSettings_.setVisible(visible);
  if (visible) {
    transitionSettings_.refreshRelevance();
    transitionSettings_.toFront(false);
    drawer_.reveal();
  } else {
    grabKeyboardFocus(); // the panel's widgets may have taken it; shortcuts need it back
  }
}

void MainComponent::toggleOutputWindow(bool fullscreen) {
  if (outputWindow_ != nullptr && !fullscreen) {
    outputWindow_.reset();
    state_.outputWindowOpen = false;
    notifyStateChanged();
    return;
  }
  if (outputWindow_ != nullptr) {
    outputWindow_->toggleFullscreen();
    return;
  }
  outputWindow_ = std::make_unique<engine::OutputWindow>(visualizer_.renderEngine());
  outputWindow_->onCloseRequested = [this] {
    // Deferred: the close button fires inside the window's own handler.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
      if (safe != nullptr && safe->outputWindow_ != nullptr) {
        safe->toggleOutputWindow(false);
      }
    });
  };
  outputWindow_->onLayoutChanged = [this] {
    if (outputWindow_ != nullptr) {
      state_.outputWindowBounds = outputWindow_->windowedBounds();
      state_.outputWindowFullscreen = outputWindow_->isFullscreen();
      notifyStateChanged();
    }
  };
  outputWindow_->show(state_.outputWindowBounds, fullscreen);
  state_.outputWindowOpen = true;
  state_.outputWindowBounds = outputWindow_->windowedBounds();
  state_.outputWindowFullscreen = outputWindow_->isFullscreen();
  notifyStateChanged();
}

void MainComponent::setControlsFloating(bool floating) {
  if (floating == (controlsWindow_ != nullptr)) {
    return;
  }
  if (floating) {
    surface_.removeChildComponent(&drawer_);
    drawer_.setFloating(true);
    drawer_.setSize(drawer_.getWidth(), ui::ControlDrawer::floatingHeight);
    controlsWindow_ = std::make_unique<ui::DetachedControlsWindow>(drawer_);
    controlsWindow_->onDockRequested = [this] {
      juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr) {
          safe->setControlsFloating(false);
        }
      });
    };
    controlsWindow_->onKeyPressed = [this](const juce::KeyPress& key) { return keyPressed(key); };
    controlsWindow_->onLayoutChanged = [this] {
      if (controlsWindow_ != nullptr) {
        state_.controlsWindowBounds = controlsWindow_->getBounds();
        notifyStateChanged();
      }
    };
    controlsWindow_->show(state_.controlsWindowBounds);
    state_.controlsWindowBounds = controlsWindow_->getBounds();
  } else {
    state_.controlsWindowBounds = controlsWindow_->getBounds();
    controlsWindow_.reset(); // releases the drawer
    drawer_.setFloating(false);
    surface_.addAndMakeVisible(drawer_);
    transitionSettings_.toFront(false); // the popover stays above the drawer
    grabKeyboardFocus();
  }
  state_.controlsFloating = floating;
  notifyStateChanged();
  resized();
}

void MainComponent::setDiagnosticsVisible(bool visible) {
  diagnosticsLabel_.setVisible(visible);
  state_.showDiagnostics = visible;
  updateStatusText();
  resized();
  notifyStateChanged();
}

void MainComponent::setLoggingEnabled(bool enabled) {
  state_.loggingEnabled = enabled;
  if (onLoggingChanged) {
    onLoggingChanged(enabled);
  }
  notifyStateChanged();
}

// ---- menus ----

juce::StringArray MainComponent::menuNames() { return {"File", "Playback", "View"}; }

juce::PopupMenu MainComponent::createMenu(int menuIndex) {
  const bool all = menuIndex < 0; // the drawer's settings popup: everything
  const bool fullscreen = isMainFullscreen && isMainFullscreen();
  const auto folder = visualizer_.director().presetFolder();
  juce::PopupMenu menu;

  if (all || menuIndex == FileMenu) {
    if (all) {
      menu.addSectionHeader(folder.empty() ? juce::String("No preset folder") : juce::String(folder));
    }
    menu.addItem(makeItem("Choose preset folder...", [this] { choosePresetFolder(); }));
    menu.addItem(makeItem("Rescan preset folder", [this] { rescanPresets(); }, false, {}, !folder.empty()));
    menu.addSeparator();
    menu.addItem(makeItem("Audio input...", [this] { showAudioSettings(); }));
    if (!all) {
      menu.addSeparator();
      menu.addItem(makeItem("Write a log file", [this] { setLoggingEnabled(!state_.loggingEnabled); },
                            state_.loggingEnabled));
      menu.addItem(makeItem("Show log file", [] {
        if (auto* logger = dynamic_cast<juce::FileLogger*>(juce::Logger::getCurrentLogger())) {
          logger->getLogFile().revealToUser();
        }
      }, false, {}, state_.loggingEnabled));
#if !JUCE_MAC // macOS puts Quit in the application menu
      menu.addSeparator();
      menu.addItem(makeItem("Quit", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }));
#endif
    }
  }

  if (!all && menuIndex == PlaybackMenu) {
    menu.addItem(makeItem("Previous preset", [this] { previousPreset(); }, false, "Left"));
    menu.addItem(makeItem("Next preset", [this] { nextPreset(); }, false, "Right"));
    menu.addItem(makeItem("Choose preset...", [this] { showPresetPicker(); }));
    menu.addSeparator();
    menu.addItem(makeItem("Lock preset", [this] { binding_.toggle("lockCurrentPreset"); },
                          binding_.get("lockCurrentPreset") > 0.5f, "Space / L"));
    menu.addItem(makeItem("Shuffle", [this] { binding_.toggle("shuffle"); }, binding_.get("shuffle") > 0.5f, "S"));
    juce::PopupMenu modes;
    if (const auto* spec = core::findParameter(core::allParameters(), "transitionMode")) {
      const auto current = static_cast<int>(binding_.get("transitionMode"));
      for (int i = 0; i < static_cast<int>(spec->choices.size()); ++i) {
        modes.addItem(makeItem(drawer_.transitionModeCombo.getItemText(i),
                               [this, i] { binding_.set("transitionMode", static_cast<float>(i)); }, i == current));
      }
    }
    menu.addSubMenu("Transition mode", modes);
  }

  if (all || menuIndex == PlaybackMenu) {
    menu.addItem(makeItem("Transition settings...", [this] { setTransitionSettingsVisible(!isTransitionSettingsVisible()); },
                          isTransitionSettingsVisible()));
  }

  if (all || menuIndex == ViewMenu) {
    if (all) {
      menu.addSeparator();
    }
    menu.addItem(makeItem("Fullscreen", [this] {
      if (onToggleMainFullscreen) {
        onToggleMainFullscreen();
      }
    }, fullscreen, "F11"));
    menu.addItem(makeItem("Output window", [this] { toggleOutputWindow(false); }, isOutputWindowOpen()));
    menu.addItem(makeItem("Output window fullscreen", [this] { toggleOutputWindow(true); },
                          isOutputWindowOpen() && outputWindow_->isFullscreen()));
    menu.addItem(makeItem(areControlsFloating() ? "Dock controls" : "Float controls in a window",
                          [this] { setControlsFloating(!areControlsFloating()); }));
    if (!all) {
      menu.addItem(makeItem("Pin controls", [this] { drawer_.togglePin(); }, drawer_.isPinned(), "P"));
    }
    menu.addItem(makeItem("Show diagnostics", [this] { setDiagnosticsVisible(!isDiagnosticsVisible()); },
                          isDiagnosticsVisible()));
  }
  return menu;
}

// ---- window-level coordination ----

void MainComponent::mainFullscreenChanged(bool fullscreen) {
  if (fullscreen == fullscreen_) {
    return;
  }
  fullscreen_ = fullscreen;
  if (fullscreen) {
    // §4.9: the drawer auto-hides in fullscreen whatever its pin state.
    pinnedBeforeFullscreen_ = drawer_.isPinned();
    if (pinnedBeforeFullscreen_) {
      drawer_.togglePin();
    }
  } else if (pinnedBeforeFullscreen_ && !drawer_.isPinned()) {
    drawer_.togglePin();
  }
  grabKeyboardFocus();
}

void MainComponent::restoreSecondaryWindows() {
  if (state_.outputWindowOpen && outputWindow_ == nullptr) {
    toggleOutputWindow(state_.outputWindowFullscreen);
  }
  if (state_.controlsFloating) {
    setControlsFloating(true);
  }
}

// ---- layout, keys, status ----

void MainComponent::resized() {
  surface_.setBounds(getLocalBounds());
  auto area = surface_.getLocalBounds();
  diagnosticsLabel_.setBounds(area.removeFromTop(96).reduced(8));
  const int hintTop = diagnosticsLabel_.isVisible() ? 104 : 12;
  const int hintWidth = std::min(520, surface_.getWidth() - 24);
  inputHint_.setBounds((surface_.getWidth() - hintWidth) / 2, hintTop, hintWidth, 30);
  if (controlsWindow_ == nullptr) {
    drawer_.setBounds(surface_.getLocalBounds().removeFromBottom(ui::ControlDrawer::preferredHeight));
  }
  layoutTransitionSettings();
}

void MainComponent::layoutTransitionSettings() {
  const int drawerHeight = controlsWindow_ != nullptr ? 0 : ui::ControlDrawer::controlsHeight;
  auto area = surface_.getLocalBounds().withTrimmedBottom(drawerHeight).reduced(6);
  const auto width = std::min(ui::TransitionSettingsPanel::preferredWidth, area.getWidth());
  const auto height = std::min(ui::TransitionSettingsPanel::preferredHeight, area.getHeight());
  transitionSettings_.setBounds(area.removeFromBottom(height).removeFromRight(width));
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
  using ui::ShortcutAction;
  switch (ui::mapKeyPress(key, /*isAppShell=*/true)) {
  case ShortcutAction::ToggleFullscreen:
    // §4.9: the Output window if it is open, else the main window.
    if (outputWindow_ != nullptr) {
      outputWindow_->toggleFullscreen();
    } else if (onToggleMainFullscreen) {
      onToggleMainFullscreen();
    }
    return true;
  case ShortcutAction::ExitFullscreenOrRevealDrawer:
    if (transitionSettings_.isVisible()) {
      setTransitionSettingsVisible(false); // Esc closes the popover first
    } else if (isMainFullscreen && isMainFullscreen() && onToggleMainFullscreen) {
      onToggleMainFullscreen();
    } else {
      drawer_.reveal();
    }
    return true;
  case ShortcutAction::PreviousPreset:
    previousPreset();
    return true;
  case ShortcutAction::NextPreset:
    nextPreset();
    return true;
  case ShortcutAction::ToggleLock:
    binding_.toggle("lockCurrentPreset");
    return true;
  case ShortcutAction::ToggleShuffle:
    binding_.toggle("shuffle");
    return true;
  case ShortcutAction::ToggleDrawer:
    drawer_.toggleRevealHide();
    return true;
  case ShortcutAction::TogglePin:
    drawer_.togglePin();
    return true;
  case ShortcutAction::None:
  default:
    return false;
  }
}

void MainComponent::publishControls() { visualizer_.setControls(engine::toEngineControls(state_.parameters)); }

void MainComponent::notifyStateChanged() {
  if (onStateChanged) {
    onStateChanged();
  }
}

void MainComponent::timerCallback() {
  updateStatusText();
  updateInputHint();

  // Remember what the user did with the drawer and where the playlist is,
  // so the next launch starts in the same place.
  if (!fullscreen_ && drawer_.isPinned() != state_.drawerPinned) {
    state_.drawerPinned = drawer_.isPinned();
    notifyStateChanged();
  }
  if (const juce::String current(visualizer_.director().currentPresetPath());
      current.isNotEmpty() && current != state_.currentPresetPath) {
    state_.currentPresetPath = current;
    notifyStateChanged();
  }
}

void MainComponent::updateInputHint() {
  const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
  const auto state = signalMonitor_.update(input_.isOpen(), input_.takePeak(AudioInput::PeakReader::Monitor), now);
  switch (state) {
  case SignalMonitor::State::NoDevice:
    inputHint_.setButtonText("No audio input. Click to choose one.");
    break;
  case SignalMonitor::State::NoSignal:
    inputHint_.setButtonText("No signal from " + input_.describe() + ". Click to check the input.");
    break;
  case SignalMonitor::State::Signal:
    break;
  }
  inputHint_.setVisible(state != SignalMonitor::State::Signal);
}

void MainComponent::updateStatusText() {
  auto& director = visualizer_.director();
  auto& engine = visualizer_.renderEngine();
  const auto status = director.status();

  if (status.playlistSize == 0) {
    drawer_.setPresetInfo("No presets loaded", "Click to choose a preset folder", {});
  } else if (status.currentIndex >= 0) {
    const auto fullName = director.presetName(status.currentIndex);
    const auto parts = ui::splitPresetName(fullName);
    juce::String detail;
    if (!parts.folder.empty()) {
      detail << juce::String(parts.folder) << juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
    }
    detail << juce::String(status.currentIndex + 1) << " / " << juce::String(status.playlistSize);
    drawer_.setPresetInfo(juce::String(parts.leaf), detail, juce::String(fullName));
  }

  const auto source = status.beatSource == engine::BeatSource::Detected ? ui::BeatBadgeSource::Detected
                                                                        : ui::BeatBadgeSource::None;
  const auto badge = ui::describeBeat(source, status.bpm, status.beatConfidence);
  drawer_.bpmLabel.setText(badge.text, juce::dontSendNotification);
  drawer_.bpmLabel.setColour(juce::Label::textColourId, badge.colour);
  drawer_.bpmLabel.setTooltip(badge.tooltip);

  if (diagnosticsLabel_.isVisible()) {
    const auto stats = engine.stats();
    juce::String text;
    if (engine.isAvailable()) {
      text << "projectM " << engine.projectMVersion() << ": " << juce::String(stats.framesPerSecond, 1) << " fps, "
           << stats.width << "x" << stats.height << ", render " << juce::String(stats.cpuFrameMs, 1) << " ms (gpu "
           << juce::String(stats.gpuFrameMs, 1) << " ms), last preset load " << juce::String(stats.lastPresetLoadMs, 1)
           << " ms\n";
    } else {
      const auto reason = engine.unavailableReason();
      text << "projectM: " << (reason.empty() ? juce::String("starting...") : juce::String("unavailable (" + reason + ")"))
           << "\n";
    }
    text << "presets: " << juce::String(status.playlistSize) << " in folder, " << juce::String(stats.presetsLoaded)
         << " loaded, " << juce::String(status.presetsSkipped) << " skipped; surface "
         << (surface_.isSharingWorking() ? "shared" : "readback (no shared context)") << "\n";
    text << "input: " << input_.describe() << "; beat confidence " << juce::String(status.beatConfidence, 2) << "\n";
    text << engine.glDescription();
    diagnosticsLabel_.setText(text, juce::dontSendNotification);
  }

  if (transitionSettings_.isVisible()) {
    transitionSettings_.refreshRelevance();
  }
}

} // namespace milkdawp::app
