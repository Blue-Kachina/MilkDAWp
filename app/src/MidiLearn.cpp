// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MidiLearn.h"

#include <algorithm>
#include <optional>
#include <thread>

#include "ParameterBinding.h"

namespace milkdawp::app {

MidiLearn::MidiLearn(juce::AudioDeviceManager& devices, ParameterBinding& binding)
    : devices_(devices), binding_(binding) {
  devices_.addChangeListener(this);
  enableAllInputs();
}

MidiLearn::~MidiLearn() {
  // Not joined with any in-flight background enable (see enableAllInputs):
  // a stuck driver call there could otherwise hang app shutdown. alive_ (a
  // shared_ptr that thread also holds) stops it from touching `this` from
  // that point on; it can still be mid-call on `devices_` when this runs,
  // an accepted, narrow risk that only matters if the app quits in the same
  // instant a device's driver is already hung.
  alive_->store(false, std::memory_order_release);
  devices_.removeChangeListener(this);
  for (const auto& info : juce::MidiInput::getAvailableDevices()) {
    devices_.removeMidiInputDeviceCallback(info.identifier, this);
  }
}

void MidiLearn::enableAllInputs() {
  // A MIDI input's driver is free to block `setMidiInputDeviceEnabled` for
  // a long time, or forever (observed here against real USB MIDI hardware
  // whose driver never returned) -- off the message thread so one bad
  // driver never hangs the app, and detached rather than tracked so this
  // destructing never has to wait for it either. `alive` is checked between
  // devices so a `MidiLearn` destroyed while this is running is only
  // touched up to that point, not after.
  std::thread([alive = alive_, this] {
    for (const auto& info : juce::MidiInput::getAvailableDevices()) {
      if (!alive->load(std::memory_order_acquire)) {
        return;
      }
      if (!devices_.isMidiInputDeviceEnabled(info.identifier)) {
        devices_.setMidiInputDeviceEnabled(info.identifier, true);
      }
      if (!alive->load(std::memory_order_acquire)) {
        return;
      }
      devices_.removeMidiInputDeviceCallback(info.identifier, this); // avoid a duplicate registration
      devices_.addMidiInputDeviceCallback(info.identifier, this);
    }
  }).detach();
}

void MidiLearn::changeListenerCallback(juce::ChangeBroadcaster*) { enableAllInputs(); }

void MidiLearn::startLearning(const std::string& parameterId) {
  learningParameterId_ = parameterId;
  fireChanged();
}

void MidiLearn::cancelLearning() {
  if (learningParameterId_.empty()) {
    return;
  }
  learningParameterId_.clear();
  fireChanged();
}

bool MidiLearn::hasMapping(const std::string& parameterId) const { return mappings_.find(parameterId) != mappings_.end(); }

juce::String MidiLearn::describeMapping(const std::string& parameterId) const {
  const auto it = mappings_.find(parameterId);
  if (it == mappings_.end()) {
    return {};
  }
  const auto& source = it->second;
  if (source.type == SourceType::ControlChange) {
    return "CC " + juce::String(source.number) + " ch " + juce::String(source.channel);
  }
  return "Note " + juce::MidiMessage::getMidiNoteName(source.number, true, true, 3) + " ch " +
        juce::String(source.channel);
}

void MidiLearn::clearMapping(const std::string& parameterId) {
  if (mappings_.erase(parameterId) > 0) {
    fireChanged();
  }
}

juce::String MidiLearn::stateString() const {
  juce::StringArray lines;
  for (const auto& [id, source] : mappings_) {
    lines.add(juce::String(id) + "=" + juce::String(source.type == SourceType::ControlChange ? 0 : 1) + "," +
              juce::String(source.channel) + "," + juce::String(source.number));
  }
  return lines.joinIntoString("\n");
}

void MidiLearn::restoreFromState(const juce::String& state) {
  mappings_.clear();
  for (const auto& line : juce::StringArray::fromLines(state)) {
    const auto equals = line.indexOfChar('=');
    if (equals <= 0) {
      continue;
    }
    const auto id = line.substring(0, equals).toStdString();
    const auto fields = juce::StringArray::fromTokens(line.substring(equals + 1), ",", "");
    if (fields.size() != 3) {
      continue;
    }
    Source source;
    source.type = fields[0].getIntValue() == 0 ? SourceType::ControlChange : SourceType::Note;
    source.channel = juce::jlimit(1, 16, fields[1].getIntValue());
    source.number = juce::jlimit(0, 127, fields[2].getIntValue());
    mappings_[id] = source;
  }
}

void MidiLearn::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
  std::optional<Source> incoming;
  float value = 0.0f;
  if (message.isController()) {
    incoming = Source{SourceType::ControlChange, message.getChannel(), message.getControllerNumber()};
    value = static_cast<float>(message.getControllerValue()) / 127.0f;
  } else if (message.isNoteOn()) {
    incoming = Source{SourceType::Note, message.getChannel(), message.getNoteNumber()};
    value = static_cast<float>(message.getFloatVelocity());
  }
  if (!incoming) {
    return;
  }
  juce::MessageManager::callAsync([alive = alive_, this, source = *incoming, value] {
    if (alive->load(std::memory_order_acquire)) {
      applyIncoming(source, value);
    }
  });
}

void MidiLearn::applyIncoming(Source source, float value) {
  if (isLearning()) {
    // One parameter per source: drop any other parameter already mapped to
    // the control being learned, so turning one knob never silently moves
    // two.
    for (auto it = mappings_.begin(); it != mappings_.end();) {
      it = (it->second == source) ? mappings_.erase(it) : std::next(it);
    }
    mappings_[learningParameterId_] = source;
    learningParameterId_.clear();
    fireChanged();
    return;
  }
  for (const auto& [id, mapped] : mappings_) {
    if (mapped == source) {
      binding_.set(id, value);
    }
  }
}

void MidiLearn::fireChanged() {
  if (onChanged) {
    onChanged();
  }
}

} // namespace milkdawp::app
