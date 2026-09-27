// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <juce_audio_devices/juce_audio_devices.h>

namespace milkdawp::app {

class ParameterBinding;

/// MIDI learn for the standalone app (4.4): any enabled MIDI input's CC or
/// note-on messages can be bound to any parameter. There is no per-device
/// picker (out of scope for 4.4) -- every available MIDI input is enabled
/// and listened to, the way most DAWs' global "MIDI learn" works, and the
/// device list is re-scanned whenever `devices` reports a change (a
/// controller plugged in after launch).
///
/// Enabling a MIDI input runs on its own detached background thread
/// (`enableAllInputs`): a device's driver is free to block that call for a
/// long time, or forever, and observably does on at least one real USB MIDI
/// interface. Keeping it off the message thread is what keeps a bad driver
/// from hanging the whole app; a console unit test that never triggers a
/// real device open never pays for this at all.
class MidiLearn final : private juce::MidiInputCallback, private juce::ChangeListener {
public:
  MidiLearn(juce::AudioDeviceManager& devices, ParameterBinding& binding);
  ~MidiLearn() override;
  MidiLearn(const MidiLearn&) = delete;
  MidiLearn& operator=(const MidiLearn&) = delete;

  /// Arms learn mode for `parameterId`: the next CC or note-on message from
  /// any enabled MIDI input binds to it, replacing any existing mapping for
  /// that parameter and any other parameter already mapped to that same
  /// source. Message thread.
  void startLearning(const std::string& parameterId);
  void cancelLearning();
  [[nodiscard]] bool isLearning() const noexcept { return !learningParameterId_.empty(); }
  [[nodiscard]] const std::string& learningParameterId() const noexcept { return learningParameterId_; }

  [[nodiscard]] bool hasMapping(const std::string& parameterId) const;
  /// "CC 21 ch 1" / "Note C3 ch 10", or empty if `parameterId` is unmapped.
  [[nodiscard]] juce::String describeMapping(const std::string& parameterId) const;
  void clearMapping(const std::string& parameterId);

  /// One mapping per line ("parameterId=type,channel,number"); round-trips
  /// through `AppState.midiMappings`.
  [[nodiscard]] juce::String stateString() const;
  void restoreFromState(const juce::String& state);

  /// Message thread: a mapping was added or removed, or learn mode changed
  /// (armed/cancelled/completed). The owner persists and refreshes tooltips.
  std::function<void()> onChanged;

private:
  enum class SourceType { ControlChange, Note };
  struct Source {
    SourceType type = SourceType::ControlChange;
    int channel = 1; // 1-16
    int number = 0;  // CC number or note number, 0-127
    [[nodiscard]] bool operator==(const Source& other) const noexcept {
      return type == other.type && channel == other.channel && number == other.number;
    }
  };

  void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
  void changeListenerCallback(juce::ChangeBroadcaster*) override;
  void applyIncoming(Source source, float value);
  void enableAllInputs();
  void fireChanged();

  juce::AudioDeviceManager& devices_;
  ParameterBinding& binding_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

  std::unordered_map<std::string, Source> mappings_; // parameterId -> source; message thread only
  std::string learningParameterId_;
};

} // namespace milkdawp::app
