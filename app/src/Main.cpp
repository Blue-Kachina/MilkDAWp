// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_gui_extra/juce_gui_extra.h>

#include "AppPreferences.h"
#include "AudioInput.h"
#include "AudioSourceRouter.h"
#include "MainComponent.h"
#include "MainWindow.h"
#include "SystemAudioCapture.h"
#include "milkdawp/engine/Visualizer.h"

namespace milkdawp::app {

/// The standalone app (Phase 4, D8): one engine (`engine::Visualizer`), fed
/// by the chosen audio input, shown in the video-first main window.
///
/// Single instance: a second launch brings the running window to the front
/// instead of opening a second engine on the same audio device.
///
/// Preferences (4.3) live in a `PropertiesFile` in the user's application
/// data folder ("MilkDAWp/app.settings", or "MilkDAWp2 Dev/..." for
/// MILKDAWP_DEV_ALT_IDENTITY builds, so a dev build never touches a real
/// install's settings). It saves itself a moment after each change, and
/// once more on quit.
class MilkDAWpApplication final : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
  const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
  bool moreThanOneInstanceAllowed() override { return false; }

  /// `commandLine` is a `.milk` file or a preset folder when the OS launched
  /// this instance via a file association or "Open with" (§4.6); empty on a
  /// plain launch.
  void initialise(const juce::String& commandLine) override {
    juce::PropertiesFile::Options options;
    options.applicationName = "app";
    options.folderName = MILKDAWP_APP_DATA_FOLDER;
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = 1000;
    properties_.setStorageParameters(options);
    state_ = loadAppState(*properties_.getUserSettings());
    setLogging(state_.loggingEnabled);
    juce::Logger::writeToLog(getApplicationName() + " " + getApplicationVersion() + " starting");

    visualizer_ = std::make_unique<engine::Visualizer>(engine::Visualizer::Config{});
    if (state_.presetFolder.isNotEmpty() && juce::File(state_.presetFolder).isDirectory()) {
      visualizer_->director().setPresetFolder(state_.presetFolder.toStdString(),
                                              state_.currentPresetPath.toStdString());
    }

    input_ = std::make_unique<AudioInput>(*visualizer_);
    systemAudio_ = createSystemAudioCapture();
    router_ = std::make_unique<AudioSourceRouter>(*visualizer_, *input_, *systemAudio_);
    const auto error =
        state_.useSystemAudio ? router_->openSystemAudio() : router_->openDevice(state_.audioDeviceState);
    if (error.isNotEmpty()) {
      const juce::String prefix = state_.useSystemAudio ? "System audio: " : "Audio input: ";
      juce::Logger::writeToLog(prefix + error);
    }
    juce::Logger::writeToLog("Audio input: " + router_->describe());

    auto content = std::make_unique<MainComponent>(*visualizer_, *router_, state_);
    auto* component = content.get();
    component->onStateChanged = [this] { saveState(); };
    component->onLoggingChanged = [this](bool enabled) { setLogging(enabled); };
    window_ = std::make_unique<MainWindow>(getApplicationName(), std::move(content), state_);
    window_->onStateChanged = [this] { saveState(); };
    component->restoreSecondaryWindows();
    component->grabKeyboardFocus();

    if (const auto path = commandLine.unquoted().trim(); path.isNotEmpty()) {
      component->openPath(path);
    }
  }

  void shutdown() override {
    // The audio source first (no more audio into the engine), then the
    // windows (their surfaces unregister from the engine), then the engine.
    if (router_ != nullptr) {
      state_.audioDeviceState = input_->stateXml();
      state_.useSystemAudio = router_->isUsingSystemAudio();
      router_->close();
    }
    if (visualizer_ != nullptr) {
      if (const auto current = visualizer_->director().currentPresetPath(); !current.empty()) {
        state_.currentPresetPath = current;
      }
    }
    saveState();
    properties_.saveIfNeeded();
    window_.reset();
    router_.reset();
    systemAudio_.reset();
    input_.reset();
    visualizer_.reset();
    juce::Logger::writeToLog("Stopped");
    setLogging(false);
  }

  void systemRequestedQuit() override { quit(); }

  /// Another launch, e.g. double-clicking a second `.milk` file: this
  /// (single-instance) window comes forward and opens it (§4.6), rather
  /// than a second engine opening on the same audio device.
  void anotherInstanceStarted(const juce::String& commandLine) override {
    if (window_ != nullptr) {
      window_->setMinimised(false);
      window_->toFront(true);
      if (const auto path = commandLine.unquoted().trim(); path.isNotEmpty()) {
        window_->content().openPath(path);
      }
    }
  }

private:
  void saveState() {
    if (auto* settings = properties_.getUserSettings()) {
      saveAppState(state_, *settings); // PropertiesFile writes it to disk shortly after
    }
  }

  void setLogging(bool enabled) {
    if (enabled && logger_ == nullptr) {
      logger_.reset(juce::FileLogger::createDefaultAppLogger(MILKDAWP_APP_DATA_FOLDER, "MilkDAWp.log",
                                                             getApplicationName() + " log"));
      juce::Logger::setCurrentLogger(logger_.get());
    } else if (!enabled && logger_ != nullptr) {
      juce::Logger::setCurrentLogger(nullptr);
      logger_.reset();
    }
  }

  juce::ApplicationProperties properties_;
  AppState state_;
  std::unique_ptr<juce::FileLogger> logger_;
  std::unique_ptr<engine::Visualizer> visualizer_;
  std::unique_ptr<AudioInput> input_;
  std::unique_ptr<SystemAudioCapture> systemAudio_;
  std::unique_ptr<AudioSourceRouter> router_;
  std::unique_ptr<MainWindow> window_;
};

} // namespace milkdawp::app

START_JUCE_APPLICATION(milkdawp::app::MilkDAWpApplication)
