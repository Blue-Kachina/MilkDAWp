// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AudioSettingsPanel.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::app {

namespace {
constexpr int kMeterHeight = 14;
constexpr float kMeterFloorDb = -60.0f;
constexpr int kToggleHeight = 24;
} // namespace

AudioSettingsPanel::AudioSettingsPanel(AudioSourceRouter& router)
    : router_(router),
      // Inputs only (1..2 channels, offered as stereo pairs), no outputs: the
      // app listens, it never plays anything.
      selector_(router.device().deviceManager(), 1, 2, 0, 0, /*showMidiInputOptions=*/false,
                /*showMidiOutputSelector=*/false, /*showChannelsAsStereoPairs=*/true,
                /*hideAdvancedOptionsWithButton=*/true) {
  systemAudioToggle_.setButtonText("Capture system audio (loopback) instead of a device");
  systemAudioToggle_.setToggleState(router_.isUsingSystemAudio(), juce::dontSendNotification);
  const bool supported = router_.systemAudio().permissionState() != SystemAudioCapture::PermissionState::Unsupported;
  systemAudioToggle_.setEnabled(supported);
  systemAudioToggle_.setTooltip(supported ? juce::String() : router_.systemAudio().describe());
  systemAudioToggle_.onClick = [this] { systemAudioToggled(); };
  addAndMakeVisible(systemAudioToggle_);

  addAndMakeVisible(selector_);
  selector_.setEnabled(!router_.isUsingSystemAudio());
  statusLabel_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(statusLabel_);
  setSize(520, 460);
  startTimerHz(20);
}

AudioSettingsPanel::~AudioSettingsPanel() { stopTimer(); }

void AudioSettingsPanel::paint(juce::Graphics& g) {
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
  g.setColour(juce::Colours::black);
  g.fillRect(meterArea_);
  const auto db = juce::Decibels::gainToDecibels(level_, kMeterFloorDb);
  const auto fraction = std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0f, 1.0f);
  g.setColour(db > -3.0f ? juce::Colours::orangered : juce::Colours::limegreen);
  g.fillRect(meterArea_.withWidth(juce::roundToInt(static_cast<float>(meterArea_.getWidth()) * fraction)));
  g.setColour(juce::Colours::grey);
  g.drawRect(meterArea_);
}

void AudioSettingsPanel::resized() {
  auto area = getLocalBounds().reduced(12);
  systemAudioToggle_.setBounds(area.removeFromTop(kToggleHeight));
  area.removeFromTop(6);
  auto bottom = area.removeFromBottom(kMeterHeight + 26);
  statusLabel_.setBounds(bottom.removeFromTop(22));
  meterArea_ = bottom.removeFromBottom(kMeterHeight);
  selector_.setBounds(area);
}

void AudioSettingsPanel::systemAudioToggled() {
  if (systemAudioToggle_.getToggleState()) {
    if (const auto error = router_.openSystemAudio(); error.isNotEmpty()) {
      juce::Logger::writeToLog("System audio: " + error);
      systemAudioToggle_.setToggleState(false, juce::dontSendNotification);
    }
  } else {
    if (const auto error = router_.openDevice(router_.device().stateXml()); error.isNotEmpty()) {
      juce::Logger::writeToLog("Audio input: " + error);
    }
  }
  selector_.setEnabled(!router_.isUsingSystemAudio());
}

void AudioSettingsPanel::timerCallback() {
  const float peak = router_.takePeak(PeakReader::Meter);
  level_ = std::max(peak, level_ * 0.85f); // quick attack, smooth release
  statusLabel_.setText(router_.describe(), juce::dontSendNotification);
  repaint(meterArea_);
}

} // namespace milkdawp::app
