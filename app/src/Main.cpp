// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_gui_extra/juce_gui_extra.h>

#include "AppPreferences.h"
#include "AudioInput.h"
#include "AudioSourceRouter.h"
#include "CrashReporter.h"
#include "LogBundle.h"
#include "MainComponent.h"
#include "MainWindow.h"
#include "RecentLog.h"
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
///
/// Crash reports (4.10) go in a "Crashes" folder beside the settings file;
/// a launch after a crash offers to collect them (File > Collect logs...).
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
    juce::Logger::setCurrentLogger(&log_);
    setLogging(state_.loggingEnabled);
    juce::Logger::writeToLog(getApplicationName() + " " + getApplicationVersion() + " starting");

    crashReporter_ = std::make_unique<CrashReporter>(
        properties_.getUserSettings()->getFile().getSiblingFile("Crashes"),
        getApplicationName() + " " + getApplicationVersion());
    const auto previousSession = crashReporter_->beginSession();
    crashReporter_->install(log_);
    if (!previousSession.endedCleanly) {
      juce::Logger::writeToLog(previousSession.crashReport.existsAsFile()
                                   ? "The last session crashed: " + previousSession.crashReport.getFullPathName()
                                   : juce::String("The last session didn't shut down cleanly"));
    }

    visualizer_ = std::make_unique<engine::Visualizer>(engine::Visualizer::Config{});
    // 5.2: the library's ratings and tags, shared with the plugin.
    visualizer_->director().setPresetMetadata(engine::PresetMetadataStore::shared());
    visualizer_->director().setTagFilter(state_.tagFilter.toStdString());
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
    component->onShowLogFile = [] { logFile().revealToUser(); };
    component->onCollectLogs = [this] { collectLogs(); };
    window_ = std::make_unique<MainWindow>(getApplicationName(), std::move(content), state_);
    window_->onStateChanged = [this] { saveState(); };
    component->restoreSecondaryWindows();
    component->grabKeyboardFocus();

#if JUCE_DEBUG
    // Checks the crash handler end to end: crashes once the window is up.
    if (commandLine.trim() == "--simulate-crash") {
      juce::Timer::callAfterDelay(2000, [] {
        volatile int* nowhere = nullptr;
        *nowhere = 1; // NOLINT(clang-analyzer-core.NullDereference)
      });
    } else
#endif
    if (const auto path = commandLine.unquoted().trim(); path.isNotEmpty()) {
      component->openPath(path);
    }

    if (previousSession.crashReport.existsAsFile()) {
      offerToCollectLogsAfterCrash();
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
    chooser_.reset();
    juce::Logger::writeToLog("Stopped");
    setLogging(false);
    juce::Logger::setCurrentLogger(nullptr);
    if (crashReporter_ != nullptr) {
      crashReporter_->endSession();
    }
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

  /// Where File > Write a log file writes (whether or not it's on now).
  static juce::File logFile() {
    return juce::FileLogger::getSystemLogFileFolder().getChildFile(MILKDAWP_APP_DATA_FOLDER).getChildFile("MilkDAWp.log");
  }

  void setLogging(bool enabled) {
    if (enabled && !log_.isWritingToFile()) {
      log_.setFileLogger(std::make_unique<juce::FileLogger>(logFile(), getApplicationName() + " log"));
    } else if (!enabled && log_.isWritingToFile()) {
      log_.setFileLogger(nullptr);
    }
  }

  void offerToCollectLogsAfterCrash() {
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                             .withTitle(getApplicationName() + " closed unexpectedly")
                             .withMessage("A crash report was saved on this computer. To report the problem, "
                                          "collect the logs into a zip file and attach it to an issue.")
                             .withButton("Collect logs...")
                             .withButton("Not now")
                             .withAssociatedComponent(window_ != nullptr ? &window_->content() : nullptr);
    juce::AlertWindow::showAsync(options, [this](int result) {
      if (result == 1) {
        collectLogs();
      }
    });
  }

  /// File > Collect logs... (4.10): the user picks where the zip goes.
  void collectLogs() {
    const auto name = defaultLogBundleName(getApplicationName(), juce::Time::getCurrentTime());
    chooser_ = std::make_unique<juce::FileChooser>(
        "Save logs", juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(name), "*.zip");
    constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                           juce::FileBrowserComponent::warnAboutOverwriting;
    chooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
      const auto file = chooser.getResult();
      if (file == juce::File()) {
        return;
      }
      const auto zip = file.hasFileExtension("zip") ? file : file.withFileExtension("zip");
      const auto result = writeLogBundle(makeLogBundle(), zip);
      if (result.wasOk()) {
        juce::Logger::writeToLog("Logs collected: " + zip.getFullPathName());
        zip.revealToUser();
      } else {
        juce::Logger::writeToLog("Collecting logs failed: " + result.getErrorMessage());
        juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::WarningIcon,
                                                                            "Couldn't save the logs",
                                                                            result.getErrorMessage()),
                                     [](int) {});
      }
    });
  }

  [[nodiscard]] LogBundle makeLogBundle() {
    saveState();
    properties_.saveIfNeeded(); // the settings file as it is now
    LogBundle bundle;
    bundle.files.add({logFile(), {}});
    if (auto* settings = properties_.getUserSettings()) {
      bundle.files.add({settings->getFile(), {}});
    }
    if (crashReporter_ != nullptr) {
      bundle.addCrashReports(crashReporter_->reports(), 5);
    }

    auto& text = bundle.diagnostics;
    text << getApplicationName() << " " << getApplicationVersion() << "\n";
    text << "Collected: " << juce::Time::getCurrentTime().toISO8601(true) << "\n\n";
    text << CrashReporter::describeSystem() << "\n\n";
    if (window_ != nullptr) {
      text << window_->content().diagnosticsText() << "\n\n";
    }
    text << "Preset folder: " << state_.presetFolder << "\n";
    text << "Log file: " << (log_.isWritingToFile() ? "on" : "off") << "\n\n";
    const auto lines = log_.lines();
    text << "Recent log (" << lines.size() << " lines):\n" << lines.joinIntoString("\n") << "\n";
    return bundle;
  }

  juce::ApplicationProperties properties_;
  AppState state_;
  RecentLog log_; // the current logger from initialise() to shutdown()
  std::unique_ptr<CrashReporter> crashReporter_;
  std::unique_ptr<juce::FileChooser> chooser_;
  std::unique_ptr<engine::Visualizer> visualizer_;
  std::unique_ptr<AudioInput> input_;
  std::unique_ptr<SystemAudioCapture> systemAudio_;
  std::unique_ptr<AudioSourceRouter> router_;
  std::unique_ptr<MainWindow> window_;
};

} // namespace milkdawp::app

START_JUCE_APPLICATION(milkdawp::app::MilkDAWpApplication)
