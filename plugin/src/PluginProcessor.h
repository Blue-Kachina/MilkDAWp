// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "milkdawp/core/HostTransport.h"
#include "milkdawp/core/SeqlockSnapshot.h"
#include "milkdawp/engine/OutputWindow.h"
#include "milkdawp/engine/Visualizer.h"

// processBlock never allocates, locks, logs, or calls the message thread
// (§4.2). Attribute is Clang-only (the RTSan job, 0.4, is Clang-only too);
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
///
/// State save/restore (getStateInformation/setStateInformation) round-trips
/// `core::StateSchemaV2`: parameters, editor size, preset folder and current
/// preset. v1 session migration (§4.8, §7 Phase 3.2) is *not* implemented
/// here: `core::migrateFromV1` (Phase 1.14) needs a `V1StateRecord` built
/// from the actual bytes of a v1 blob, and no real one has been provided yet.
class MilkDAWpAudioProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorValueTreeState::Listener {
public:
  MilkDAWpAudioProcessor();
  ~MilkDAWpAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  MILKDAWP_NONBLOCKING void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

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

  juce::AudioProcessorValueTreeState apvts;

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  void parameterChanged(const juce::String& parameterId, float newValue) override;
  [[nodiscard]] engine::EngineControls readControls() const noexcept;

  std::unique_ptr<engine::Visualizer> visualizer_;
  std::unique_ptr<engine::OutputWindow> outputWindow_;
  juce::Rectangle<int> outputWindowBounds_;

  core::SeqlockSnapshot<core::TransportInfo> transportSnapshot_;
  std::unique_ptr<core::HostTransport> hostTransport_;
  core::SeqlockSnapshot<core::BeatClockState> beatClockSnapshot_;

  // Raw parameter values, read on the audio thread every block.
  struct RawParameters {
    std::atomic<float>* beatSensitivity = nullptr;
    std::atomic<float>* transitionDurationSeconds = nullptr;
    std::atomic<float>* shuffle = nullptr;
    std::atomic<float>* lockCurrentPreset = nullptr;
    std::atomic<float>* presetIndex = nullptr;
    std::atomic<float>* transitionJitterEnabled = nullptr;
    std::atomic<float>* transitionDurationMin = nullptr;
    std::atomic<float>* transitionDurationMax = nullptr;
    std::atomic<float>* hardCutEnabled = nullptr;
    std::atomic<float>* softCutDuration = nullptr;
    std::atomic<float>* qualityOverride = nullptr;
    std::atomic<float>* transitionMode = nullptr;
    std::atomic<float>* transitionBars = nullptr;
    std::atomic<float>* presetSelectionPolicy = nullptr;
  } raw_;

  int editorWidth_ = 480;
  int editorHeight_ = 270;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MilkDAWpAudioProcessor)
};

} // namespace milkdawp::plugin
