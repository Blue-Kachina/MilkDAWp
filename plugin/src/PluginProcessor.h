// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/core/SeqlockSnapshot.h"
#include "milkdawp/core/HostTransport.h"
#include "milkdawp/engine/RenderEngine.h"

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

/// Phase 3.1/3.2: ParameterModel-driven APVTS, RT-safe `AudioRing` writes,
/// a lock-free host-transport snapshot, and an engine bound to the
/// processor's own lifetime (§4.5: "for the lifetime of the processor or
/// app"). Still a bit-exact passthrough (§4.1 "zero audio impact") -- this
/// class never writes to `buffer`, only reads from it.
///
/// State save/restore (getStateInformation/setStateInformation) round-trips
/// `core::StateSchemaV2` (native v2 sessions only). v1 session migration
/// (§4.8, §7 Phase 3.2) is *not* implemented here: `core::migrateFromV1`
/// (Phase 1.14) needs a `V1StateRecord` built from the actual bytes of a
/// v1 `juce::ValueTree::readFromData` blob, and 1.14's own note already
/// flags that nobody has fed it a real one yet ("still need Matthew's 2-3
/// real .vstpreset/project blobs"). Wiring that parse in without a real v1
/// blob to test against would be unverifiable guessing at v1's exact tree
/// shape, so this is left as a clearly-scoped follow-up.
class MilkDAWpAudioProcessor final : public juce::AudioProcessor {
public:
  MilkDAWpAudioProcessor();
  ~MilkDAWpAudioProcessor() override = default;

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
  /// audio hasn't run yet).
  [[nodiscard]] core::TransportInfo currentTransport() const noexcept { return transportSnapshot_.read(); }

  /// Message/UI thread. `core::HostTransport`'s reading of the same
  /// snapshot (§4.3: confidence 1.0 whenever the host reports isPlaying and
  /// a valid bpm/ppq). HostTransport is stateless (recomputes fresh from
  /// ppq every call), so this needs no special-casing for stop/loop/relocate
  /// -- see HostTransport's own class comment (Phase 1.7).
  [[nodiscard]] core::BeatClockState currentBeatClock() const noexcept { return beatClockSnapshot_.read(); }

  /// UI thread. Backs editor-size persistence: the size lives here, on the
  /// processor, rather than being pushed into an editor that may not exist
  /// yet -- this is the actual fix for v1's Cubase lesson (§2.9: "host may
  /// create the editor before setStateInformation"), since reading a plain
  /// member works regardless of construction order.
  [[nodiscard]] int editorWidth() const noexcept { return editorWidth_; }
  [[nodiscard]] int editorHeight() const noexcept { return editorHeight_; }
  void setEditorSize(int width, int height) noexcept;

  /// UI thread. Guaranteed non-null: RenderEngine::create() never returns
  /// null (§4.5) and this processor creates one unconditionally in its
  /// constructor. The editor uses this to build its `OutputSurface` (2.3/2.4).
  [[nodiscard]] milkdawp::engine::RenderEngine& renderEngine() noexcept { return *renderEngine_; }

  juce::AudioProcessorValueTreeState apvts;

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  void pushChangedRenderParameters();

  std::unique_ptr<milkdawp::engine::RenderEngine> renderEngine_;
  std::unique_ptr<core::AudioRing> audioRing_;
  std::vector<float> interleaveScratch_;
  core::SeqlockSnapshot<core::TransportInfo> transportSnapshot_;
  std::unique_ptr<core::HostTransport> hostTransport_;
  core::SeqlockSnapshot<core::BeatClockState> beatClockSnapshot_;

  std::atomic<float>* beatSensitivityParam_ = nullptr;
  std::atomic<float>* transitionDurationParam_ = nullptr;
  float lastPushedBeatSensitivity_ = -1.0f;
  float lastPushedTransitionDuration_ = -1.0f;

  int editorWidth_ = 480;
  int editorHeight_ = 270;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MilkDAWpAudioProcessor)
};

} // namespace milkdawp::plugin
