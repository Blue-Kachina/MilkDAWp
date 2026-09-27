// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>

#include <juce_audio_devices/juce_audio_devices.h>

#include "AudioPeakReader.h"
#include "milkdawp/engine/Visualizer.h"

namespace milkdawp::app {

/// The app's audio input (4.2, §4.7): a `juce::AudioDeviceManager` opened for
/// input only (up to one stereo pair, never any output, so nothing can feed
/// back), whose callback hands every block to the engine exactly as a
/// plugin's processBlock would. Also tracks the input peak for the level
/// meter and the "no signal" hint.
///
/// Device selection, including which input pair, is JUCE's own
/// `AudioDeviceSelectorComponent` over `deviceManager()`. `SystemAudioCapture`
/// (4.7/4.8, and Android's in Phase 7) is a further source feeding the same
/// `Visualizer`, not part of this class; `AudioSourceRouter` picks between
/// the two.
class AudioInput final : private juce::AudioIODeviceCallback, private juce::ChangeListener {
public:
  explicit AudioInput(engine::Visualizer& visualizer);
  ~AudioInput() override;
  AudioInput(const AudioInput&) = delete;
  AudioInput& operator=(const AudioInput&) = delete;

  /// Opens the device described by `savedState` (from `stateXml()`), or the
  /// system's default input when it is empty, unparseable or missing (the
  /// interface was unplugged). Returns JUCE's error text, empty on success.
  juce::String open(const juce::String& savedState);
  void close();

  /// The current device setup, to save in preferences.
  [[nodiscard]] juce::String stateXml() const;
  [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept { return devices_; }
  [[nodiscard]] bool isOpen() const;
  /// "Device name (inputs 1+2)", or "No input device".
  [[nodiscard]] juce::String describe() const;

  /// Largest absolute input sample since this reader's previous call.
  [[nodiscard]] float takePeak(PeakReader reader) noexcept {
    return peaks_[static_cast<std::size_t>(reader)].exchange(0.0f, std::memory_order_relaxed);
  }

  /// Message thread, after the device or its settings change.
  std::function<void()> onDeviceChanged;

private:
  void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                        int numOutputs, int numSamples,
                                        const juce::AudioIODeviceCallbackContext& context) override;
  void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
  void audioDeviceStopped() override {}
  void changeListenerCallback(juce::ChangeBroadcaster*) override;

  engine::Visualizer& visualizer_;
  juce::AudioDeviceManager devices_;
  std::array<std::atomic<float>, 2> peaks_{};
  bool callbackAdded_ = false;
};

} // namespace milkdawp::app
