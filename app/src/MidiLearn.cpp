// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MidiLearn.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <thread>

#include "ParameterBinding.h"

namespace milkdawp::app {

MidiLearn::MidiLearn(ParameterBinding& binding)
    : binding_(binding),
      deviceListConnection_(juce::MidiDeviceListConnection::make([this] { openNewInputs(); })) {
  openNewInputs();
}

MidiLearn::~MidiLearn() {
  // Wait for open threads still inside JUCE (a normal open takes
  // milliseconds), so none outlives this object, or JUCE itself, which a
  // test shuts down right after. Bounded: a driver call that never returns
  // (one real USB interface here does that) must not hang shutdown; only
  // then is a thread left behind.
  {
    std::unique_lock lock(openState_->mutex);
    openState_->alive = false;
    if (!openState_->idle.wait_for(
            lock, std::chrono::seconds(2), [this] { return openState_->running == 0; })) {
      juce::Logger::writeToLog(
          "MIDI learn: a MIDI driver is still busy opening an input; closing anyway");
    }
  }
  deviceListConnection_ = {};
  for (auto& input : inputs_) {
    input->stop();
  }
  inputs_.clear();
}

void MidiLearn::openNewInputs() {
  // Listing the devices here, on the message thread, is also what builds
  // JUCE's device table: the background open below only reads it.
  const auto available = juce::MidiInput::getAvailableDevices();
  const auto isAvailable = [&available](const juce::String& id) {
    return std::any_of(available.begin(), available.end(), [&id](const juce::MidiDeviceInfo& info) {
      return info.identifier == id;
    });
  };

  // Unplugged: close it, and allow it to be opened again when it returns.
  inputs_.erase(std::remove_if(inputs_.begin(),
                               inputs_.end(),
                               [&](const std::shared_ptr<juce::MidiInput>& input) {
                                 if (isAvailable(input->getIdentifier())) {
                                   return false;
                                 }
                                 input->stop();
                                 return true;
                               }),
                inputs_.end());
  for (auto it = requested_.begin(); it != requested_.end();) {
    it = isAvailable(*it) ? std::next(it) : requested_.erase(it);
  }

  std::vector<juce::String> toOpen;
  for (const auto& info : available) {
    if (requested_.insert(info.identifier).second) {
      toOpen.push_back(info.identifier);
    }
  }
  if (toOpen.empty()) {
    return;
  }

  {
    const std::lock_guard lock(openState_->mutex);
    ++openState_->running;
  }
  std::thread([state = openState_, this, toOpen = std::move(toOpen)] {
    for (const auto& id : toOpen) {
      {
        const std::lock_guard lock(state->mutex);
        if (!state->alive) {
          break;
        }
      }
      // May block for a long time. The input isn't started, so `this` as its
      // callback is never called until the message thread has adopted it.
      //
      // JUCE marks openDevice message-thread-only (JUCE_ASSERT_MESSAGE_THREAD
      // in its device list lookup, so a debug build under a debugger breaks
      // here once per device). Calling it here is deliberate: its lookups
      // only read the device table openNewInputs() has just built on the
      // message thread, which JUCE rebuilds only when a device is plugged or
      // unplugged. That narrow window is the price of never letting a hung
      // driver freeze the app.
      std::shared_ptr<juce::MidiInput> input(juce::MidiInput::openDevice(id, this).release());
      if (input != nullptr) {
        juce::MessageManager::callAsync([state, this, input]() mutable {
          // Message thread, like the destructor: `alive` can't change under us.
          if (!state->alive) {
            return; // `input` closes here, never started
          }
          input->start();
          inputs_.push_back(std::move(input));
        });
      }
    }
    const std::lock_guard lock(state->mutex);
    --state->running;
    state->idle.notify_all();
  }).detach();
}

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

bool MidiLearn::hasMapping(const std::string& parameterId) const {
  return mappings_.find(parameterId) != mappings_.end();
}

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
    lines.add(juce::String(id) + "=" +
              juce::String(source.type == SourceType::ControlChange ? 0 : 1) + "," +
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
    incoming =
        Source{SourceType::ControlChange, message.getChannel(), message.getControllerNumber()};
    value = static_cast<float>(message.getControllerValue()) / 127.0f;
  } else if (message.isNoteOn()) {
    incoming = Source{SourceType::Note, message.getChannel(), message.getNoteNumber()};
    value = static_cast<float>(message.getFloatVelocity());
  }
  if (!incoming) {
    return;
  }
  // Runs on the message thread, like the destructor, so `alive` can't change
  // under it.
  juce::MessageManager::callAsync([state = openState_, this, source = *incoming, value] {
    if (state->alive) {
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
