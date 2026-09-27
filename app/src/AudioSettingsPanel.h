// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioInput.h"

namespace milkdawp::app {

/// The audio input settings (4.2): JUCE's device selector (driver type,
/// device, input pair, sample rate, buffer size) over the app's
/// `AudioInput`, plus a live input level meter so "is anything arriving?"
/// is answered on the spot.
class AudioSettingsPanel final : public juce::Component, private juce::Timer {
public:
  explicit AudioSettingsPanel(AudioInput& input);
  ~AudioSettingsPanel() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  void timerCallback() override;

  AudioInput& input_;
  juce::AudioDeviceSelectorComponent selector_;
  juce::Label statusLabel_;
  juce::Rectangle<int> meterArea_;
  float level_ = 0.0f; // decaying display level, 0..1
};

} // namespace milkdawp::app
