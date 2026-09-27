// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AudioInput.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::app {

namespace {
constexpr int kMaxInputChannels = 2; // one stereo pair: the engine's ring is stereo
}

AudioInput::AudioInput(engine::Visualizer& visualizer) : visualizer_(visualizer) {
  devices_.addChangeListener(this);
}

AudioInput::~AudioInput() {
  close();
  devices_.removeChangeListener(this);
}

juce::String AudioInput::open(const juce::String& savedState) {
  close();
  std::unique_ptr<juce::XmlElement> xml;
  if (savedState.isNotEmpty()) {
    xml = juce::parseXML(savedState);
  }
  // selectDefaultDeviceOnFailure: a saved device that has gone away falls
  // back to the default input instead of leaving the app deaf.
  auto error = devices_.initialise(kMaxInputChannels, 0, xml.get(), /*selectDefaultDeviceOnFailure=*/true);
  devices_.addAudioCallback(this);
  callbackAdded_ = true;
  return error;
}

void AudioInput::close() {
  if (callbackAdded_) {
    devices_.removeAudioCallback(this);
    callbackAdded_ = false;
  }
  devices_.closeAudioDevice();
}

juce::String AudioInput::stateXml() const {
  if (const auto xml = devices_.createStateXml()) {
    return xml->toString(juce::XmlElement::TextFormat().singleLine().withoutHeader());
  }
  return {};
}

bool AudioInput::isOpen() const {
  const auto* device = devices_.getCurrentAudioDevice();
  return device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

juce::String AudioInput::describe() const {
  const auto* device = devices_.getCurrentAudioDevice();
  if (device == nullptr) {
    return "No input device";
  }
  const auto active = device->getActiveInputChannels();
  if (active.countNumberOfSetBits() == 0) {
    return device->getName() + " (no inputs enabled)";
  }
  juce::StringArray channels;
  for (int bit = active.findNextSetBit(0); bit >= 0; bit = active.findNextSetBit(bit + 1)) {
    channels.add(juce::String(bit + 1));
  }
  return device->getName() + " (input " + channels.joinIntoString("+") + ")";
}

void AudioInput::audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                                  int numOutputs, int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&) {
  for (int c = 0; c < numOutputs; ++c) {
    if (outputs[c] != nullptr) {
      std::fill(outputs[c], outputs[c] + numSamples, 0.0f);
    }
  }
  const int channels = std::min(numInputs, kMaxInputChannels);
  float peak = 0.0f;
  for (int c = 0; c < channels; ++c) {
    if (inputs[c] == nullptr) {
      return; // JUCE passes null for channels it could not open; skip the block
    }
    for (int i = 0; i < numSamples; ++i) {
      peak = std::max(peak, std::abs(inputs[c][i]));
    }
  }
  // Keep the largest peak until each reader takes it.
  for (auto& slot : peaks_) {
    float previous = slot.load(std::memory_order_relaxed);
    while (peak > previous && !slot.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {
    }
  }
  visualizer_.processAudio(inputs, channels, numSamples, nullptr);
}

void AudioInput::audioDeviceAboutToStart(juce::AudioIODevice* device) {
  visualizer_.prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioInput::changeListenerCallback(juce::ChangeBroadcaster*) {
  if (onDeviceChanged) {
    onDeviceChanged();
  }
}

} // namespace milkdawp::app
