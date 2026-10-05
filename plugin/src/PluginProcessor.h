// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include <juce_audio_processors/juce_audio_processors.h>

#include "LayerRegistry.h"
#include "milkdawp/core/HostTransport.h"
#include "milkdawp/core/SeqlockSnapshot.h"
#include "milkdawp/core/StateSchema.h"
#include "milkdawp/engine/ControlMapping.h"
#include "milkdawp/engine/OscRemote.h"
#include "milkdawp/engine/OutputWindow.h"
#include "milkdawp/engine/Visualizer.h"

// processBlock never allocates, locks, logs, or calls the message thread
// (§4.2). It's a type attribute, so it goes after the parameter list and
// `noexcept` (Clang requires noexcept with it), not before the return type.
// Attribute is Clang-only (the RTSan job, 0.4, is Clang-only too);
// __has_cpp_attribute degrades to 0 -- and this to nothing -- everywhere
// else, so MSVC/GCC builds are unaffected.
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define MILKDAWP_NONBLOCKING [[clang::nonblocking]]
#endif
#endif
#ifndef MILKDAWP_NONBLOCKING
#define MILKDAWP_NONBLOCKING
#endif

namespace milkdawp::plugin {

/// Phase 3.1/3.2 plus 2.6's wiring: a ParameterModel-driven APVTS, and an
/// `engine::Visualizer` (audio ring, render thread, director thread) bound
/// to the processor's own lifetime (§4.5). processBlock copies audio into
/// the ring, publishes the host transport and the parameter snapshot, and
/// is otherwise a bit-exact passthrough (§4.1): `buffer` is only read.
///
/// The Output window (§4.9, 2.4/3.12) is owned here, not by the editor, so
/// it keeps running when the editor closes and goes away with the plugin.
/// Whether it is open, fullscreen, and where it sits are saved in the state,
/// so reopening a project puts it back on the capture display.
///
/// State save/restore (getStateInformation/setStateInformation) round-trips
/// `core::StateSchemaV2`: parameters, editor size, preset folder and current
/// preset. v1 session migration (§4.8, §7 Phase 3.2) is *not* implemented
/// here: `core::migrateFromV1` (Phase 1.14) needs a `V1StateRecord` built
/// from the actual bytes of a v1 blob, and no real one has been provided yet.
class MilkDAWpAudioProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::AsyncUpdater,
                                     private juce::Timer {
public:
  MilkDAWpAudioProcessor();
  ~MilkDAWpAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) noexcept MILKDAWP_NONBLOCKING override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }

  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;
  void updateTrackProperties(const TrackProperties& properties) override;

  /// Message/UI thread. Snapshot of the host transport as of the most
  /// recent processBlock() call (or a default-constructed TransportInfo if
  /// audio hasn't run yet), in the host's own sample numbering.
  [[nodiscard]] core::TransportInfo currentTransport() const noexcept { return transportSnapshot_.read(); }

  /// Message/UI thread. `core::HostTransport`'s reading of the same
  /// snapshot (§4.3: confidence 1.0 whenever the host reports isPlaying and
  /// a valid bpm/ppq). The engine's own view (host or detected beat) is
  /// `visualizer().director().status()`.
  [[nodiscard]] core::BeatClockState currentBeatClock() const noexcept { return beatClockSnapshot_.read(); }

  /// UI thread. Backs editor-size persistence: the size lives here, on the
  /// processor, rather than being pushed into an editor that may not exist
  /// yet -- this is the actual fix for v1's Cubase lesson (§2.9: "host may
  /// create the editor before setStateInformation"), since reading a plain
  /// member works regardless of construction order.
  [[nodiscard]] int editorWidth() const noexcept { return editorWidth_; }
  [[nodiscard]] int editorHeight() const noexcept { return editorHeight_; }
  void setEditorSize(int width, int height) noexcept;

  [[nodiscard]] engine::Visualizer& visualizer() noexcept { return *visualizer_; }
  [[nodiscard]] engine::RenderEngine& renderEngine() noexcept { return visualizer_->renderEngine(); }

  /// Message thread. Opens the Output window (fullscreen or windowed), or
  /// brings it to the front if it is already open.
  void openOutputWindow(bool fullscreen);
  void closeOutputWindow();
  [[nodiscard]] bool isOutputWindowOpen() const noexcept { return outputWindow_ != nullptr; }
  /// Message thread. F11 from the editor: open fullscreen, or toggle
  /// fullscreen if already open (§4.9).
  void toggleOutputFullscreen();
  /// Message thread. The drawer's pop-out button: opens the Output window,
  /// fullscreen if Settings -> Output says to default to fullscreen.
  void popOutOutputWindow();

  /// Message thread. Settings -> Output. Saved with the state.
  void setOutputDefaultFullscreen(bool fullscreen);
  /// An empty `display` means automatic. Otherwise it is the chosen display's
  /// own bounds. If the Output window is already open it moves to that display.
  void setOutputTargetDisplay(const core::WindowBounds& display);

  // ---- Layers (layers_like_shrek.md): sending this instance's picture to another's Output window ----
  /// Stable across project reloads; what another instance saves to point at this one.
  [[nodiscard]] std::string instanceId() const;
  /// What the target picker calls this instance: the user's own label, else the
  /// host's track name, else a short form of the id.
  [[nodiscard]] std::string instanceDisplayName() const;
  [[nodiscard]] std::string instanceLabel() const;
  void setInstanceLabel(const std::string& label);
  /// The instances this one could send to right now (self excluded, instances
  /// that already send elsewhere marked `canBeTarget = false`).
  [[nodiscard]] std::vector<InstanceInfo> otherInstances() const;
  /// False while other instances send to this one: Layers does not chain.
  [[nodiscard]] bool canChooseOutputTarget() const;
  /// How many instances currently send to this one.
  [[nodiscard]] int layerSenderCount() const;
  /// The instances sending to this one, with how each is mixed: the Sources list.
  [[nodiscard]] std::vector<LayerRegistry::Entry::SenderInfo> layerSenders() const {
    return registryEntry_->senders();
  }
  /// Edits how one of those senders is mixed (`layerOpacity`, `layerBlend`, `layerMute`, `layerOrder`).
  void setLayerSenderParameter(const std::string& senderId, const std::string& parameterId, float plainValue) {
    registryEntry_->setSenderParameter(senderId, parameterId, plainValue);
  }
  /// True while this instance's picture is part of another instance's canvas.
  [[nodiscard]] bool isSendingToOtherInstance() const noexcept { return hub_ != nullptr; }
  /// The name of the instance this one sends to (empty when it does not).
  [[nodiscard]] std::string outputTargetName() const { return hub_ != nullptr ? hub_->name() : std::string{}; }
  /// Empty means this instance has its own Output window. Saved with the state;
  /// the link is kept while the target does not exist and made when it appears.
  void setOutputTargetInstance(const std::string& instanceId);
  /// Attaches to / detaches from the target to match `windowLayout().outputTargetInstance`.
  /// Message thread. Runs on its own whenever instances come and go; public so
  /// it can be driven directly.
  void reconcileLayers();

  /// The session's window layout (Phase 3.12/3.13), saved with the plugin
  /// state. Any thread may read it (getStateInformation can run off the
  /// message thread); the message thread keeps it current as windows open,
  /// close, and move.
  [[nodiscard]] core::WindowLayout windowLayout() const;
  /// Message thread: the editor reports its detached-controls window.
  void setControlsLayout(bool floating, juce::Rectangle<int> bounds);
  /// 8.6: the media source Media Mix shows (an image file; empty: none).
  /// Saved with the state. A file that can't be opened leaves no source and
  /// lands in the recent errors. Any thread.
  void setMediaSourcePath(const std::string& path);
  [[nodiscard]] std::string mediaSourcePath() const;
  /// 8.6e: how the media meets the picture (Displace included). Saved with the state.
  void setMediaBlend(engine::LayerBlend blend) noexcept { visualizer_->renderEngine().primaryLayer().setMediaBlend(blend); }
  [[nodiscard]] engine::LayerBlend mediaBlend() noexcept { return visualizer_->renderEngine().primaryLayer().mediaBlend(); }
  /// 8.4: the process-wide OSC remote (Settings > OSC remote control).
  [[nodiscard]] engine::OscRemote& oscRemote() noexcept { return *osc_; }

  juce::AudioProcessorValueTreeState apvts;

private:
  /// Restored state reopens (or closes) the Output window here, on the
  /// message thread, whatever thread setStateInformation ran on.
  void handleAsyncUpdate() override;
  /// Message thread, 10 Hz: Lock Macros (Phase 8.1). When the playing preset
  /// changes, un-locked Macros move to the new preset's defaults as gestures.
  void timerCallback() override;
  void updateOutputLayout();
  /// Windowed bounds for a new Output window: the saved ones, moved onto the
  /// chosen target display when Settings -> Output picked one that is connected.
  [[nodiscard]] juce::Rectangle<int> outputOpenBounds() const;

  static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  void parameterChanged(const juce::String& parameterId, float newValue) override;
  [[nodiscard]] engine::EngineControls readControls() const noexcept;

  /// This instance's own Output window, whatever the target setting says.
  void openOwnOutputWindow();
  void toggleOwnOutputFullscreen();
  /// Pushes the layer parameters (opacity, blend, mute, order) to the engine's primary layer.
  void applyLayerParams() noexcept;
  void attachToHub(const std::shared_ptr<LayerRegistry::Entry>& hub, const std::string& hubId);
  void detachFromHub();
  void refreshRegistryName();

  std::unique_ptr<engine::Visualizer> visualizer_;
  std::unique_ptr<engine::OutputWindow> outputWindow_;
  // 8.4: the process's OSC remote, shared with every other instance.
  juce::SharedResourcePointer<engine::OscRemote> osc_;
  int oscHandle_ = 0;
  // Layers. `registryEntry_` is this instance in the process-wide registry;
  // `hub_` is the entry it currently sends its picture to (null: own window).
  std::shared_ptr<LayerRegistry::Entry> registryEntry_;
  std::shared_ptr<LayerRegistry::Entry> hub_;
  std::string hubId_;
  int registryListener_ = 0;
  std::string instanceLabel_;  // guarded by layoutMutex_
  std::string hostTrackName_;  // guarded by layoutMutex_
  mutable std::mutex layoutMutex_; // never taken on the audio thread
  core::WindowLayout layout_;

  core::SeqlockSnapshot<core::TransportInfo> transportSnapshot_;
  std::unique_ptr<core::HostTransport> hostTransport_;
  core::SeqlockSnapshot<core::BeatClockState> beatClockSnapshot_;

  // Every engine-read parameter and its APVTS atomic, copied into
  // ParameterValues on the audio thread every block (readControls). Filled once
  // in the constructor; never resized after.
  struct EngineParameter {
    float engine::ParameterValues::*member;
    std::atomic<float>* raw;
  };
  std::vector<EngineParameter> engineParameters_;
  // The layer parameters, also read on parameter-change callbacks (applyLayerParams).
  struct RawParameters {
    std::atomic<float>* layerOpacity = nullptr;
    std::atomic<float>* layerBlend = nullptr;
    std::atomic<float>* layerMute = nullptr;
    std::atomic<float>* layerOrder = nullptr;
  } raw_;

  // Lock Macros: the preset last seen playing (message thread), and the one a
  // restored session is about to bring back. Changes from nothing, and the
  // restore landing, are not preset changes: the session's own Macro values stay.
  std::string lastPresetPath_;
  std::string pendingRestorePath_; // guarded by layoutMutex_
  std::string mediaSourcePath_;    // guarded by layoutMutex_
  juce::uint32 restoreDeadlineMs_ = 0;

  int editorWidth_ = 480;
  int editorHeight_ = 270;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MilkDAWpAudioProcessor)
};

} // namespace milkdawp::plugin
