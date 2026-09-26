// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// mdw-view (Phase 2.9): a developer viewer. Plays a WAV file through the
// default audio output while feeding the same audio to the engine, and shows
// the result in the video-first window with the shared ControlDrawer. This is
// the first place a human can watch beat-aligned transitions against music
// they can hear, without a DAW.
//
//   mdw-view [--output] [audio-file] [preset-folder]
//
// --output opens the Output window (windowed) at startup, to see the
// primary window and the Output window showing the same frames.
//
// Both arguments are optional: without a folder it uses the repo's test
// presets (fixtures/presets); the drawer's "Set" menu opens other files and
// folders. Space plays/pauses; arrows, L, S, H, P and F11 work as in §4.9.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "milkdawp/engine/OutputSurface.h"
#include "milkdawp/engine/OutputWindow.h"
#include "milkdawp/engine/Visualizer.h"
#include "milkdawp/ui/ControlDrawer.h"
#include "milkdawp/ui/Shortcuts.h"

namespace {

using namespace milkdawp;

/// Plays the transport to the speakers and hands every block to the engine
/// on the way out, exactly as a plugin's processBlock would see it.
class TappedTransport final : public juce::AudioSource {
public:
  TappedTransport(juce::AudioTransportSource& transport, engine::Visualizer& visualizer)
      : transport_(transport), visualizer_(visualizer) {}

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override {
    transport_.prepareToPlay(samplesPerBlockExpected, sampleRate);
    visualizer_.prepare(sampleRate, samplesPerBlockExpected);
  }
  void releaseResources() override { transport_.releaseResources(); }

  void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
    transport_.getNextAudioBlock(info);
    const float* channels[2] = {};
    const int numChannels = std::min(info.buffer->getNumChannels(), 2);
    for (int c = 0; c < numChannels; ++c) {
      channels[c] = info.buffer->getReadPointer(c, info.startSample);
    }
    visualizer_.processAudio(channels, numChannels, info.numSamples, nullptr);
  }

private:
  juce::AudioTransportSource& transport_;
  engine::Visualizer& visualizer_;
};

class ViewerComponent final : public juce::Component, private juce::Timer {
public:
  ViewerComponent(engine::Visualizer& visualizer, juce::AudioTransportSource& transport,
                  juce::AudioFormatManager& formats)
      : visualizer_(visualizer), transport_(transport), formats_(formats), surface_(visualizer.renderEngine()),
        drawer_(ui::DrawerStateMachine::Config{.startPinned = true}) {
    addAndMakeVisible(surface_);
    surface_.addAndMakeVisible(info_);
    surface_.addAndMakeVisible(drawer_);
    info_.setJustificationType(juce::Justification::topLeft);
    info_.setColour(juce::Label::textColourId, juce::Colours::white);
    info_.setColour(juce::Label::backgroundColourId, juce::Colours::black.withAlpha(0.5f));

    drawer_.prevButton.onClick = [this] { visualizer_.director().requestPrevious(); };
    drawer_.nextButton.onClick = [this] { visualizer_.director().requestNext(); };
    drawer_.lockButton.onClick = [this] {
      controls_.locked = drawer_.lockButton.getToggleState();
      publish();
    };
    drawer_.shuffleButton.onClick = [this] {
      controls_.policy = drawer_.shuffleButton.getToggleState() ? core::PlaylistPolicy::ShuffleNoRepeat
                                                                 : core::PlaylistPolicy::Sequential;
      publish();
    };
    drawer_.transitionModeCombo.setSelectedId(static_cast<int>(controls_.transitionMode) + 1,
                                              juce::dontSendNotification);
    drawer_.transitionModeCombo.onChange = [this] {
      controls_.transitionMode =
          static_cast<core::TransitionMode>(std::max(drawer_.transitionModeCombo.getSelectedId() - 1, 0));
      publish();
    };
    drawer_.outputButton.onClick = [this] { toggleOutputWindow(false); };
    drawer_.settingsButton.onClick = [this] { showMenu(); };

    publish();
    setWantsKeyboardFocus(true);
    setSize(1280, 720);
    startTimerHz(10);
  }

  ~ViewerComponent() override { outputWindow_.reset(); }

  void loadAudioFile(const juce::File& file) {
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr) {
      audioText_ = "Could not open " + file.getFileName();
      return;
    }
    transport_.stop();
    transport_.setSource(nullptr);
    const double rate = reader->sampleRate;
    readerSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    readerSource_->setLooping(true);
    transport_.setSource(readerSource_.get(), 0, nullptr, rate);
    transport_.start();
    audioText_ = file.getFileName();
  }

  void resized() override {
    surface_.setBounds(getLocalBounds());
    info_.setBounds(surface_.getLocalBounds().removeFromTop(64).reduced(8));
    drawer_.setBounds(surface_.getLocalBounds().removeFromBottom(ui::ControlDrawer::preferredHeight));
  }

  bool keyPressed(const juce::KeyPress& key) override {
    if (key == juce::KeyPress::spaceKey) {
      transport_.isPlaying() ? transport_.stop() : transport_.start();
      return true;
    }
    using ui::ShortcutAction;
    switch (ui::mapKeyPress(key, /*isAppShell=*/false)) {
    case ShortcutAction::PreviousPreset:
      visualizer_.director().requestPrevious();
      return true;
    case ShortcutAction::NextPreset:
      visualizer_.director().requestNext();
      return true;
    case ShortcutAction::ToggleLock:
      drawer_.lockButton.triggerClick();
      return true;
    case ShortcutAction::ToggleShuffle:
      drawer_.shuffleButton.triggerClick();
      return true;
    case ShortcutAction::ToggleDrawer:
      drawer_.toggleRevealHide();
      return true;
    case ShortcutAction::TogglePin:
      drawer_.togglePin();
      return true;
    case ShortcutAction::ToggleFullscreen:
      toggleOutputWindow(true);
      return true;
    case ShortcutAction::ExitFullscreenOrRevealDrawer:
      drawer_.reveal();
      return true;
    default:
      return false;
    }
  }

  void openOutputWindow() {
    if (outputWindow_ == nullptr) {
      toggleOutputWindow(false);
    }
  }

private:
  void publish() { visualizer_.setControls(controls_); }

  void toggleOutputWindow(bool fullscreen) {
    if (outputWindow_ != nullptr && !fullscreen) {
      outputWindow_.reset();
      return;
    }
    if (outputWindow_ == nullptr) {
      outputWindow_ = std::make_unique<engine::OutputWindow>(visualizer_.renderEngine());
      outputWindow_->onCloseRequested = [this] {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ViewerComponent>(this)] {
          if (safe != nullptr) {
            safe->outputWindow_.reset();
          }
        });
      };
      // Offset from the main window (both would otherwise centre on the same
      // display and overlap exactly).
      outputWindow_->show(juce::Rectangle<int>(40, 40, 640, 360), fullscreen);
    } else {
      outputWindow_->toggleFullscreen();
    }
  }

  void showMenu() {
    juce::PopupMenu menu;
    menu.addItem("Open audio file...", [this] {
      chooser_ = std::make_unique<juce::FileChooser>("Open an audio file", juce::File(), formats_.getWildcardForAllFormats());
      chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                            [this](const juce::FileChooser& chooser) {
                              if (chooser.getResult().existsAsFile()) {
                                loadAudioFile(chooser.getResult());
                              }
                            });
    });
    menu.addItem("Choose preset folder...", [this] {
      chooser_ = std::make_unique<juce::FileChooser>("Choose a preset folder");
      chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                            [this](const juce::FileChooser& chooser) {
                              if (chooser.getResult().isDirectory()) {
                                visualizer_.director().setPresetFolder(
                                    chooser.getResult().getFullPathName().toStdString());
                              }
                            });
    });
    menu.addSeparator();
    menu.addItem(transport_.isPlaying() ? "Pause (Space)" : "Play (Space)",
                 [this] { transport_.isPlaying() ? transport_.stop() : transport_.start(); });
    menu.addItem("Restart", [this] { transport_.setPosition(0.0); });
    juce::PopupMenu cut;
    cut.addItem("Soft (blend)", true, controls_.cutStyle == core::CutStyle::Soft, [this] {
      controls_.cutStyle = core::CutStyle::Soft;
      publish();
    });
    cut.addItem("Hard", true, controls_.cutStyle == core::CutStyle::Hard, [this] {
      controls_.cutStyle = core::CutStyle::Hard;
      publish();
    });
    menu.addSubMenu("Cut style", cut);
    juce::PopupMenu bars;
    for (const std::uint32_t n : {1U, 2U, 4U, 8U, 16U}) {
      bars.addItem(juce::String(n) + " bars", true, controls_.transitionBars == n, [this, n] {
        controls_.transitionBars = n;
        publish();
      });
    }
    menu.addSubMenu("Bars per transition", bars);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&drawer_.settingsButton));
  }

  void timerCallback() override {
    const auto status = visualizer_.director().status();
    const auto stats = visualizer_.renderEngine().stats();

    juce::String presetText;
    if (status.currentIndex >= 0) {
      presetText = juce::String(status.currentIndex + 1) + "/" + juce::String(status.playlistSize) + "  " +
                   juce::String(visualizer_.director().presetName(status.currentIndex));
    } else {
      presetText = "No presets";
    }
    drawer_.presetLabel.setText(presetText, juce::dontSendNotification);

    juce::String bpm(juce::CharPointer_UTF8("\xE2\x99\xA9"));
    bpm << (status.bpm > 0.0f ? juce::String(status.bpm, 0) : juce::String("--"));
    drawer_.bpmLabel.setText(bpm, juce::dontSendNotification);

    juce::String text;
    text << audioText_ << "  " << juce::String(transport_.getCurrentPosition(), 1) << " s  |  beat confidence "
         << juce::String(status.beatConfidence, 2) << "  |  " << juce::String(status.transitionsIssued) << " transitions, last landing "
         << juce::String(1000.0 * static_cast<double>(stats.lastLandingErrorSamples) / 48000.0, 0) << " ms late\n";
    const auto& render = visualizer_.renderEngine();
    if (render.isAvailable()) {
      text << juce::String(stats.framesPerSecond, 0) << " fps " << stats.width << "x" << stats.height
           << ", last preset load " << juce::String(stats.lastPresetLoadMs, 0) << " ms";
    } else {
      text << "projectM unavailable: " << render.unavailableReason();
    }
    info_.setText(text, juce::dontSendNotification);
  }

  engine::Visualizer& visualizer_;
  juce::AudioTransportSource& transport_;
  juce::AudioFormatManager& formats_;
  engine::OutputSurface surface_;
  juce::Label info_;
  ui::ControlDrawer drawer_;
  engine::EngineControls controls_;
  std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;
  std::unique_ptr<juce::FileChooser> chooser_;
  std::unique_ptr<engine::OutputWindow> outputWindow_;
  juce::String audioText_ = "No audio file (Set > Open audio file...)";
};

class MainWindow final : public juce::DocumentWindow {
public:
  explicit MainWindow(std::unique_ptr<ViewerComponent> content)
      : DocumentWindow("mdw-view", juce::Colours::black, DocumentWindow::allButtons) {
    setUsingNativeTitleBar(true);
    setContentOwned(content.release(), true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }
  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class MdwViewApplication final : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override { return "mdw-view"; }
  const juce::String getApplicationVersion() override { return "0.1"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String& commandLine) override {
    formats_.registerBasicFormats();
    visualizer_ = std::make_unique<engine::Visualizer>(engine::Visualizer::Config{});

    auto args = juce::StringArray::fromTokens(commandLine, true);
    args.removeEmptyStrings();
    juce::File audio;
    juce::File presets(juce::File(MILKDAWP_FIXTURES_DIR).getChildFile("presets"));
    bool openOutput = false;
    for (auto arg : args) {
      if (arg == "--output") {
        openOutput = true;
        continue;
      }
      const juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(arg.unquoted()));
      if (file.isDirectory()) {
        presets = file;
      } else if (file.existsAsFile()) {
        audio = file;
      }
    }
    if (presets.isDirectory()) {
      visualizer_->director().setPresetFolder(presets.getFullPathName().toStdString());
    }

    tap_ = std::make_unique<TappedTransport>(transport_, *visualizer_);
    player_.setSource(tap_.get());
    devices_.initialiseWithDefaultDevices(0, 2);
    devices_.addAudioCallback(&player_);

    auto content = std::make_unique<ViewerComponent>(*visualizer_, transport_, formats_);
    if (audio.existsAsFile()) {
      content->loadAudioFile(audio);
    }
    auto* viewer = content.get();
    window_ = std::make_unique<MainWindow>(std::move(content));
    if (openOutput) {
      viewer->openOutputWindow();
    }
  }

  void shutdown() override {
    window_.reset();
    devices_.removeAudioCallback(&player_);
    player_.setSource(nullptr);
    transport_.setSource(nullptr);
    tap_.reset();
    visualizer_.reset();
  }

  void systemRequestedQuit() override { quit(); }

private:
  juce::AudioFormatManager formats_;
  juce::AudioDeviceManager devices_;
  juce::AudioSourcePlayer player_;
  juce::AudioTransportSource transport_;
  std::unique_ptr<engine::Visualizer> visualizer_;
  std::unique_ptr<TappedTransport> tap_;
  std::unique_ptr<MainWindow> window_;
};

} // namespace

START_JUCE_APPLICATION(MdwViewApplication)
