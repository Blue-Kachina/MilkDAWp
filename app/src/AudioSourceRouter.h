// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>

#include "AudioInput.h"
#include "AudioPeakReader.h"
#include "SystemAudioCapture.h"

namespace milkdawp::app {

/// Picks between the two sources that can feed the app's `Visualizer`
/// (§4.7): the device input (`AudioInput`) and system-audio loopback
/// capture (`SystemAudioCapture`). Exactly one runs at a time -- both write
/// into the same `AudioRing`, so running both together would mix two
/// unrelated audio streams into it.
class AudioSourceRouter {
public:
  AudioSourceRouter(engine::Visualizer& visualizer, AudioInput& device, SystemAudioCapture& systemAudio)
      : visualizer_(visualizer), device_(device), systemAudio_(systemAudio) {}

  /// Opens the device described by `savedDeviceState` (or the default),
  /// closing system-audio capture first if it was running.
  juce::String openDevice(const juce::String& savedDeviceState) {
    systemAudio_.close();
    usingSystemAudio_ = false;
    return device_.open(savedDeviceState);
  }

  /// Switches to system-audio loopback capture, closing the device input
  /// first. Returns an error string (empty on success); see
  /// `SystemAudioCapture::open` for what "success" means on platforms that
  /// need consent.
  juce::String openSystemAudio() {
    device_.close();
    usingSystemAudio_ = true;
    return systemAudio_.open(visualizer_);
  }

  void close() {
    device_.close();
    systemAudio_.close();
  }

  [[nodiscard]] bool isUsingSystemAudio() const noexcept { return usingSystemAudio_; }
  [[nodiscard]] bool isOpen() const { return usingSystemAudio_ ? systemAudio_.isOpen() : device_.isOpen(); }
  [[nodiscard]] juce::String describe() const {
    return usingSystemAudio_ ? systemAudio_.describe() : device_.describe();
  }
  [[nodiscard]] float takePeak(PeakReader reader) noexcept {
    return usingSystemAudio_ ? systemAudio_.takePeak(reader) : device_.takePeak(reader);
  }

  [[nodiscard]] AudioInput& device() noexcept { return device_; }
  [[nodiscard]] SystemAudioCapture& systemAudio() noexcept { return systemAudio_; }

private:
  engine::Visualizer& visualizer_;
  AudioInput& device_;
  SystemAudioCapture& systemAudio_;
  bool usingSystemAudio_ = false;
};

} // namespace milkdawp::app
