// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MainComponent.h"

#include <algorithm>
#include <utility>

#include "AudioSettingsPanel.h"
#include "milkdawp/core/DisplayLayout.h"
#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/Version.h"
#include "milkdawp/ui/Icons.h"
#include "milkdawp/ui/PresetInfoMenu.h"
#include "milkdawp/ui/PresetMenu.h"
#include "milkdawp/ui/Shortcuts.h"

namespace milkdawp::app {

namespace {

constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

enum MenuIndex { FileMenu = 0, PlaybackMenu = 1, ViewMenu = 2, HelpMenu = 3 };

bool isPresetFile(const juce::File& file) { return file.hasFileExtension("milk"); }

core::WindowBounds toWindowBounds(juce::Rectangle<int> r) noexcept {
  return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}

// Where the Output window opens: where it was, moved onto the display chosen in
// Settings -> Output when that display is connected. A chosen display that is
// unplugged falls back to the saved bounds (the choice itself is kept).
juce::Rectangle<int> outputOpenBounds(const AppState& state) {
  if (state.outputTargetDisplay.isEmpty()) {
    return state.outputWindowBounds;
  }
  std::vector<core::WindowBounds> displays;
  for (const auto& display : ui::currentDisplays()) {
    displays.push_back(display.id);
  }
  const auto index = core::findDisplay(displays, toWindowBounds(state.outputTargetDisplay));
  if (index < 0) {
    return state.outputWindowBounds;
  }
  const auto placed = core::placeOnDisplay(toWindowBounds(state.outputWindowBounds),
                                           displays[static_cast<std::size_t>(index)], kDefaultWidth, kDefaultHeight);
  return {placed.x, placed.y, placed.width, placed.height};
}

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

MainComponent::MainComponent(engine::Visualizer& visualizer, AudioSourceRouter& input, AppState& state)
    : visualizer_(visualizer), input_(input), state_(state), surface_(visualizer.renderEngine()),
      drawer_(ui::DrawerStateMachine::Config{.startPinned = state.drawerPinned}),
      binding_(state.parameters,
              [this] {
                publishControls();
                if (transitionSettings_.isVisible()) {
                  transitionSettings_.refreshRelevance();
                }
                notifyStateChanged();
              }),
      midiLearn_(binding_) {
  addAndMakeVisible(surface_);

  // Everything drawn over the video is a child of the surface, never a
  // sibling: JUCE only composites a GL component's own children over its
  // frame (§4.11, 2.3's finding).
  surface_.addChildComponent(diagnosticsPanel_);
  diagnosticsPanel_.onCloseRequested = [this] { setDiagnosticsVisible(false); };
  diagnosticsPanel_.setVisible(state_.showDiagnostics);

  inputHint_.setColour(juce::TextButton::buttonColourId, juce::Colours::black.withAlpha(0.6f));
  inputHint_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  inputHint_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  inputHint_.onClick = [this] { showAudioSettings(); };
  surface_.addChildComponent(inputHint_);

  surface_.addAndMakeVisible(drawer_);
  drawer_.prevButton.onClick = [this] { previousPreset(); };
  drawer_.nextButton.onClick = [this] { nextPreset(); };
  drawer_.outputButton.onClick = [this] { toggleOutputWindowFromDrawer(); };
  drawer_.settingsButton.onClick = [this] {
    auto menu = createMenu(-1);
    menu.setLookAndFeel(&drawer_.getLookAndFeel());
    menu.showMenuAsync(ui::DrawerLookAndFeel::menuOptions(drawer_.settingsMenuAnchor()));
  };
  drawer_.onPresetTitleClicked = [this] { setPresetBrowserVisible(!presetBrowser_.isVisible()); };

  // Above the drawer; hidden until chosen.
  surface_.addChildComponent(transitionSettings_);
  transitionSettings_.onCloseRequested = [this] { setTransitionSettingsVisible(false); };

  // 6.1b: the preset browser, a popover like the others.
  surface_.addChildComponent(presetBrowser_);
  presetBrowser_.onCloseRequested = [this] { setPresetBrowserVisible(false); };
  {
    auto& director = visualizer_.director();
    ui::PresetBrowser::Source source;
    source.playlistGeneration = [&director] { return director.status().playlistGeneration; };
    source.names = [&director] { return director.presetNames(); };
    source.paths = [&director] { return director.presetPaths(); };
    source.currentIndex = [&director] { return director.status().currentIndex; };
    source.folder = [&director] { return director.presetFolder(); };
    source.metadata = [&director]() -> std::shared_ptr<const core::PresetMetadata> {
      auto store = director.presetMetadata();
      return store != nullptr ? store->snapshot() : nullptr;
    };
    source.metadataGeneration = [&director]() -> std::uint64_t {
      auto store = director.presetMetadata();
      return store != nullptr ? store->generation() : 0;
    };
    source.setInfo = [&director](const std::string& path, const core::PresetInfo& info) {
      if (auto store = director.presetMetadata(); store != nullptr && !store->set(path, info)) {
        juce::Logger::writeToLog("Couldn't save preset ratings to " + store->file().getFullPathName());
      }
    };
    source.allTags = [&director] {
      auto store = director.presetMetadata();
      return store != nullptr ? store->allTags() : std::vector<std::string>{};
    };
    source.blacklistedPaths = [&director] { return director.blacklistedPaths(); };
    source.setBlacklisted = [&director](const std::string& path, bool blacklisted) {
      if (blacklisted) {
        director.blacklistPreset(path);
      } else {
        director.unblacklistPreset(path);
      }
    };
    source.recentPaths = [this] {
      std::vector<std::string> paths;
      for (const auto& path : state_.recentlyPlayedPresets) {
        paths.push_back(path.toStdString());
      }
      return paths;
    };
    source.onPick = [&director](int index) { director.requestPreset(index); };
    source.onChooseFolder = [this] { choosePresetFolder(); };
    source.onRescan = [this] { rescanPresets(); };
    presetBrowser_.setSource(std::move(source));
  }
  migrateFavourites();

  // 6.7: About, and the opt-in update check (off until turned on there).
  surface_.addChildComponent(aboutPanel_);
  aboutPanel_.onCloseRequested = [this] { setAboutVisible(false); };
  aboutPanel_.onAutoCheckChanged = [this](bool enabled) {
    updateChecker_.setEnabled(enabled);
    refreshAboutUpdateStatus();
  };
  aboutPanel_.onCheckNow = [this] {
    updateChecker_.checkNow();
    refreshAboutUpdateStatus();
  };
  updateChecker_.onFinished = [this] {
    const auto status = updateChecker_.status();
    juce::Logger::writeToLog("Update check: " + engine::describeUpdateStatus(status));
    refreshAboutUpdateStatus();
  };

  // Settings -> Output, same slot and compositing rules. The standalone app has no
  // other instances to link to, so it only gets the window settings.
  surface_.addChildComponent(outputSettings_);
  outputSettings_.setLayersAvailable(false);
  outputSettings_.onCloseRequested = [this] { setOutputSettingsVisible(false); };
  outputSettings_.onPreferredSizeChanged = [this] { layoutOutputSettings(); };
  outputSettings_.onDefaultFullscreenChanged = [this](bool fullscreen) {
    state_.outputDefaultFullscreen = fullscreen;
    notifyStateChanged();
  };
  outputSettings_.onTargetDisplayChanged = [this](const core::WindowBounds& display) {
    state_.outputTargetDisplay = {display.x, display.y, display.width, display.height};
    notifyStateChanged();
    if (outputWindow_ != nullptr) {
      // A native window cannot change display and keep its GL context: reopen it
      // on the chosen one, in the same mode, centred (not left where it was).
      const bool wasFullscreen = outputWindow_->isFullscreen();
      outputWindow_.reset();
      state_.outputWindowBounds = {};
      toggleOutputWindow(wasFullscreen);
    }
  };

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
  binding_.bind(transitionSettings_.reactivitySlider, "beatSensitivity");
  binding_.bind(transitionSettings_.jitterToggle, "transitionJitterEnabled");
  binding_.bind(transitionSettings_.hardCutToggle, "hardCutEnabled");
  binding_.bind(transitionSettings_.gridToggle, "transitionGridSync");
  binding_.bind(transitionSettings_.gridOffsetSlider, "transitionGridOffset");
  transitionSettings_.refreshRelevance();
  transitionSettings_.tagFilterEditor.setText(state_.tagFilter, juce::dontSendNotification);
  transitionSettings_.onTagFilterChanged = [this](const juce::String& text) {
    state_.tagFilter = text;
    visualizer_.director().setTagFilter(text.toStdString());
    notifyStateChanged();
  };

  input_.device().onDeviceChanged = [this] {
    if (!input_.isUsingSystemAudio()) {
      state_.audioDeviceState = input_.device().stateXml();
    }
    juce::Logger::writeToLog("Audio input: " + input_.describe());
    notifyStateChanged();
  };
  input_.systemAudio().onPermissionChanged = [this] {
    juce::Logger::writeToLog("System audio: " + input_.systemAudio().describe());
    notifyStateChanged();
  };

  // §4.4: every widget bound above and below gets the right-click "MIDI
  // Learn..." affordance; restore what was saved before any of them refresh.
  binding_.attachMidiLearn(midiLearn_);
  midiLearn_.restoreFromState(state_.midiMappings);
  midiLearn_.onChanged = [this] {
    binding_.refreshMidiLearnTooltips();
    state_.midiMappings = midiLearn_.stateString();
    notifyStateChanged();
  };
  binding_.refreshMidiLearnTooltips();

  publishControls();
  setWantsKeyboardFocus(true);
  setSize(kDefaultWidth, kDefaultHeight);
  timerCallback();
  startTimerHz(10);
}

MainComponent::~MainComponent() {
  stopTimer();
  input_.device().onDeviceChanged = nullptr;
  input_.systemAudio().onPermissionChanged = nullptr;
  midiLearn_.onChanged = nullptr;
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

void MainComponent::openPath(const juce::String& path) {
  if (path.isEmpty()) {
    return;
  }
  const juce::File file(path);
  if (isPresetFile(file) && file.existsAsFile()) {
    const auto folder = file.getParentDirectory();
    visualizer_.director().setPresetFolder(folder.getFullPathName().toStdString(), file.getFullPathName().toStdString());
    state_.presetFolder = folder.getFullPathName();
    state_.currentPresetPath = file.getFullPathName();
  } else if (file.isDirectory()) {
    visualizer_.director().setPresetFolder(file.getFullPathName().toStdString());
    state_.presetFolder = file.getFullPathName();
    state_.currentPresetPath = {};
  } else {
    return;
  }
  juce::Logger::writeToLog("Preset folder: " + state_.presetFolder);
  notifyStateChanged();
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files) {
  return std::any_of(files.begin(), files.end(), [](const juce::String& path) {
    const juce::File file(path);
    return file.isDirectory() || isPresetFile(file);
  });
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int) {
  const auto it = std::find_if(files.begin(), files.end(), [](const juce::String& path) {
    const juce::File file(path);
    return file.isDirectory() || isPresetFile(file);
  });
  if (it != files.end()) {
    openPath(*it);
  }
}

void MainComponent::setPresetBrowserVisible(bool visible) {
  if (visible) {
    // One popover at a time: at small window sizes they would overlap.
    setTransitionSettingsVisible(false);
    setOutputSettingsVisible(false);
    layoutPresetBrowser();
  }
  presetBrowser_.setVisible(visible);
  if (visible) {
    presetBrowser_.toFront(false);
    drawer_.reveal();
    presetBrowser_.opened(); // focuses the search box; Esc closes
  } else {
    grabKeyboardFocus(); // the search box had it; shortcuts need it back
  }
}

void MainComponent::layoutPresetBrowser() {
  // Above the drawer, left-aligned under the preset title.
  const int drawerHeight = controlsWindow_ != nullptr ? 0 : ui::ControlDrawer::controlsHeight;
  auto area = surface_.getLocalBounds().withTrimmedBottom(drawerHeight).reduced(6);
  const auto width = std::min(ui::PresetBrowser::preferredWidth, area.getWidth());
  const auto height = std::min(ui::PresetBrowser::preferredHeight, area.getHeight());
  presetBrowser_.setBounds(area.removeFromBottom(height).removeFromLeft(width));
}

void MainComponent::layoutAbout() {
  const int drawerHeight = controlsWindow_ != nullptr ? 0 : ui::ControlDrawer::controlsHeight;
  const auto area = surface_.getLocalBounds().withTrimmedBottom(drawerHeight).reduced(6);
  aboutPanel_.setBounds(area.withSizeKeepingCentre(std::min(ui::AboutPanel::preferredWidth, area.getWidth()),
                                                   std::min(ui::AboutPanel::preferredHeight, area.getHeight())));
}

void MainComponent::refreshAboutUpdateStatus() {
  const auto status = updateChecker_.status();
  const bool available = status.state == engine::UpdateChecker::State::Available;
  aboutPanel_.setUpdateControls(true, updateChecker_.enabled());
  aboutPanel_.setUpdateStatus(engine::describeUpdateStatus(status), available ? juce::String(status.url) : juce::String(),
                              available);
}

void MainComponent::setAboutVisible(bool visible) {
  if (visible) {
    ui::AboutInfo info;
    info.version = core::versionString();
    info.shell = "app";
    info.projectMVersion = juce::String(visualizer_.renderEngine().projectMVersion());
    info.juceVersion = juce::SystemStats::getJUCEVersion();
    info.operatingSystem = juce::SystemStats::getOperatingSystemName();
    aboutPanel_.setInfo(info);
    refreshAboutUpdateStatus();
    layoutAbout();
  }
  aboutPanel_.setVisible(visible);
  if (visible) {
    aboutPanel_.toFront(false);
    ui::focusFirstControl(aboutPanel_); // Esc closes
  } else {
    grabKeyboardFocus();
  }
}

void MainComponent::migrateFavourites() {
  auto store = visualizer_.director().presetMetadata();
  if (state_.favouritePresets.isEmpty() || store == nullptr) {
    return;
  }
  for (const auto& path : state_.favouritePresets) {
    auto info = store->get(path.toStdString());
    if (!info.favourite) {
      info.favourite = true;
      store->set(path.toStdString(), info);
    }
  }
  juce::Logger::writeToLog("Moved " + juce::String(state_.favouritePresets.size()) +
                           " favourite presets into the shared preset metadata");
  state_.favouritePresets.clear();
  notifyStateChanged();
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

void MainComponent::setOutputSettingsVisible(bool visible) {
  if (visible) {
    setTransitionSettingsVisible(false);
    setPresetBrowserVisible(false);
    outputSettings_.refresh(state_.outputDefaultFullscreen,
                            {state_.outputTargetDisplay.getX(), state_.outputTargetDisplay.getY(),
                             state_.outputTargetDisplay.getWidth(), state_.outputTargetDisplay.getHeight()});
    layoutOutputSettings();
  }
  outputSettings_.setVisible(visible);
  if (visible) {
    outputSettings_.toFront(false);
    drawer_.reveal();
    ui::focusFirstControl(outputSettings_); // 5.8: Tab through it, Esc closes
  } else {
    grabKeyboardFocus();
  }
}

void MainComponent::toggleOutputWindowFromDrawer() {
  if (isOutputWindowOpen()) {
    toggleOutputWindow(false); // closes it
  } else {
    toggleOutputWindow(state_.outputDefaultFullscreen);
  }
}

void MainComponent::setTransitionSettingsVisible(bool visible) {
  if (visible && outputSettings_.isVisible()) {
    setOutputSettingsVisible(false);
  }
  if (visible && presetBrowser_.isVisible()) {
    setPresetBrowserVisible(false);
  }
  transitionSettings_.setVisible(visible);
  if (visible) {
    transitionSettings_.refreshRelevance();
    transitionSettings_.toFront(false);
    drawer_.reveal();
    ui::focusFirstControl(transitionSettings_); // 5.8: Tab through it, Esc closes
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
  outputWindow_->show(outputOpenBounds(state_), fullscreen);
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
    outputSettings_.toFront(false);
    grabKeyboardFocus();
  }
  state_.controlsFloating = floating;
  notifyStateChanged();
  resized();
}

void MainComponent::setDiagnosticsVisible(bool visible) {
  if (visible) {
    diagnosticsPanel_.update(diagnosticsInfo());
  }
  diagnosticsPanel_.setVisible(visible);
  state_.showDiagnostics = visible;
  updateStatusText();
  resized();
  if (visible) {
    ui::focusFirstControl(diagnosticsPanel_); // 5.8: Copy is one Space away
  } else {
    grabKeyboardFocus();
  }
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

juce::StringArray MainComponent::menuNames() { return {"File", "Playback", "View", "Help"}; }

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
    menu.addItem(makeItem("Browse presets...", [this] { setPresetBrowserVisible(true); }, false, "B", !folder.empty()));
    menu.addSeparator();
    menu.addItem(makeItem("Audio input...", [this] { showAudioSettings(); }));
    if (!all) {
      menu.addSeparator();
      menu.addItem(makeItem("Write a log file", [this] { setLoggingEnabled(!state_.loggingEnabled); },
                            state_.loggingEnabled));
      menu.addItem(makeItem("Show log file", [this] {
        if (onShowLogFile) {
          onShowLogFile();
        }
      }, false, {}, state_.loggingEnabled));
      menu.addItem(makeItem("Collect logs...", [this] {
        if (onCollectLogs) {
          onCollectLogs();
        }
      }));
#if !JUCE_MAC // macOS puts Quit in the application menu
      menu.addSeparator();
      menu.addItem(makeItem("Quit", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }));
#endif
    }
  }

  if (!all && menuIndex == PlaybackMenu) {
    menu.addItem(makeItem("Previous preset", [this] { previousPreset(); }, false, "Left"));
    menu.addItem(makeItem("Next preset", [this] { nextPreset(); }, false, "Right"));
    menu.addItem(makeItem("Choose preset...", [this] { setPresetBrowserVisible(true); }, false, "B"));
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
    menu.addItem(makeItem("Output window", [this] { toggleOutputWindowFromDrawer(); }, isOutputWindowOpen()));
    menu.addItem(makeItem("Output window fullscreen", [this] { toggleOutputWindow(true); },
                          isOutputWindowOpen() && outputWindow_->isFullscreen()));
    menu.addItem(makeItem("Output settings...", [this] { setOutputSettingsVisible(!isOutputSettingsVisible()); },
                          isOutputSettingsVisible()));
    menu.addItem(makeItem(areControlsFloating() ? "Dock controls" : "Float controls in a window",
                          [this] { setControlsFloating(!areControlsFloating()); }));
    if (!all) {
      menu.addItem(makeItem("Pin controls", [this] { drawer_.togglePin(); }, drawer_.isPinned(), "P"));
    }
    // 5.3: Auto adapts the render scale to the GPU; the others fix it.
    if (const auto* spec = core::findParameter(core::allParameters(), "qualityOverride")) {
      juce::PopupMenu quality;
      const auto current = static_cast<int>(binding_.get("qualityOverride"));
      for (int i = 0; i < static_cast<int>(spec->choices.size()); ++i) {
        quality.addItem(makeItem(juce::String(spec->choices[static_cast<std::size_t>(i)]),
                                 [this, i] { binding_.set("qualityOverride", static_cast<float>(i)); }, i == current));
      }
      menu.addSubMenu("Quality", quality);
    }
    menu.addItem(makeItem("Show diagnostics", [this] { setDiagnosticsVisible(!isDiagnosticsVisible()); },
                          isDiagnosticsVisible(), "D"));
  }

  // 6.6/6.7.
  if (all || menuIndex == HelpMenu) {
    if (all) {
      menu.addSeparator();
    }
    menu.addItem(makeItem("User guide", [] {
      juce::URL("https://github.com/Blue-Kachina/MilkDAWp/blob/HEAD/docs/user-guide/README.md").launchInDefaultBrowser();
    }));
    menu.addItem(makeItem("Releases", [] { juce::URL(engine::kReleasesPage).launchInDefaultBrowser(); }));
    const bool available = updateChecker_.status().state == engine::UpdateChecker::State::Available;
    menu.addItem(makeItem(available ? "About MilkDAWp (update available)..." : "About MilkDAWp...",
                          [this] { setAboutVisible(true); }));
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
  // Diagnostics top left over the picture; the popovers sit bottom right.
  const int drawerHeight = controlsWindow_ != nullptr ? 0 : ui::ControlDrawer::controlsHeight;
  auto area = surface_.getLocalBounds().withTrimmedBottom(drawerHeight).reduced(8);
  diagnosticsPanel_.setBounds(area.removeFromTop(std::min(diagnosticsPanel_.preferredHeight(), area.getHeight()))
                                  .removeFromLeft(std::min(760, area.getWidth())));
  const int hintTop = diagnosticsPanel_.isVisible() ? diagnosticsPanel_.getBottom() + 8 : 12;
  const int hintWidth = std::min(520, surface_.getWidth() - 24);
  inputHint_.setBounds((surface_.getWidth() - hintWidth) / 2, hintTop, hintWidth, 30);
  if (controlsWindow_ == nullptr) {
    drawer_.setBounds(surface_.getLocalBounds().removeFromBottom(ui::ControlDrawer::preferredHeight));
  }
  layoutTransitionSettings();
  layoutOutputSettings();
  layoutPresetBrowser();
  layoutAbout();
}

void MainComponent::layoutOutputSettings() {
  const int drawerHeight = controlsWindow_ != nullptr ? 0 : ui::ControlDrawer::controlsHeight;
  auto area = surface_.getLocalBounds().withTrimmedBottom(drawerHeight).reduced(6);
  const auto width = std::min(ui::OutputSettingsPanel::preferredWidth, area.getWidth());
  const auto height = std::min(outputSettings_.preferredHeight(), area.getHeight());
  outputSettings_.setBounds(area.removeFromBottom(height).removeFromRight(width));
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
    if (aboutPanel_.isVisible()) {
      setAboutVisible(false); // Esc closes the popover first
    } else if (presetBrowser_.isVisible()) {
      setPresetBrowserVisible(false);
    } else if (outputSettings_.isVisible()) {
      setOutputSettingsVisible(false);
    } else if (transitionSettings_.isVisible()) {
      setTransitionSettingsVisible(false);
    } else if (diagnosticsPanel_.isVisible()) {
      setDiagnosticsVisible(false);
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
  case ShortcutAction::BrowsePresets:
    setPresetBrowserVisible(!presetBrowser_.isVisible());
    return true;
  case ShortcutAction::OpenSettingsMenu:
    drawer_.reveal();
    drawer_.settingsButton.triggerClick();
    return true;
  case ShortcutAction::ToggleDiagnostics:
    setDiagnosticsVisible(!isDiagnosticsVisible());
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
    recordRecentlyPlayed(current);
    notifyStateChanged();
  }
}

void MainComponent::recordRecentlyPlayed(const juce::String& absolutePath) {
  state_.recentlyPlayedPresets.removeString(absolutePath);
  state_.recentlyPlayedPresets.insert(0, absolutePath);
  while (state_.recentlyPlayedPresets.size() > AppState::kMaxRecentlyPlayed) {
    state_.recentlyPlayedPresets.remove(state_.recentlyPlayedPresets.size() - 1);
  }
}

void MainComponent::updateInputHint() {
  const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
  const auto state = signalMonitor_.update(input_.isOpen(), input_.takePeak(PeakReader::Monitor), now);
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
    const auto renderStats = visualizer_.renderEngine().stats();
    if (const auto quality = ui::describeRenderQuality(renderStats.qualityScale, renderStats.qualityAuto);
        quality.isNotEmpty()) {
      detail << juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) << quality; // 5.3
    }
    drawer_.setPresetInfo(juce::String(parts.leaf), detail, juce::String(fullName));
  }

  const auto source = status.beatSource == engine::BeatSource::Detected ? ui::BeatBadgeSource::Detected
                                                                        : ui::BeatBadgeSource::None;
  const auto badge = ui::describeBeat(source, status.bpm, status.beatConfidence);
  drawer_.bpmLabel.setText(badge.text, juce::dontSendNotification);
  drawer_.bpmLabel.setColour(juce::Label::textColourId, badge.colour);
  drawer_.bpmLabel.setTooltip(badge.tooltip);

  if (diagnosticsPanel_.isVisible()) {
    const int before = diagnosticsPanel_.preferredHeight();
    diagnosticsPanel_.update(diagnosticsInfo());
    if (diagnosticsPanel_.preferredHeight() != before) {
      resized();
    }
  }

  if (transitionSettings_.isVisible()) {
    transitionSettings_.refreshRelevance();
    const auto tagStatus = ui::describeAutoSelection(status.autoSelectable, status.playlistSize,
                                                     status.tagFilterMatchesNothing);
    transitionSettings_.setTagFilterStatus(tagStatus.text, tagStatus.warning);
  }
}

core::DiagnosticsInfo MainComponent::diagnosticsInfo() const {
  auto info = visualizer_.diagnostics();
  info.shell = (juce::JUCEApplication::getInstance() != nullptr
                    ? juce::JUCEApplication::getInstance()->getApplicationName() + " " +
                          juce::JUCEApplication::getInstance()->getApplicationVersion() + " (app)"
                    : juce::String("app"))
                   .toStdString();
  info.surface = surface_.isSharingWorking() ? "shared context" : "readback (no shared context)";
  info.input = input_.describe().toStdString();
  return info;
}

juce::String MainComponent::diagnosticsText() const {
  const auto info = diagnosticsInfo();
  return ui::formatDiagnosticsLines(info, static_cast<int>(info.recentErrors.size())).joinIntoString("\n");
}

} // namespace milkdawp::app
