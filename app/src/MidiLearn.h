// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

namespace milkdawp::app {

class ParameterBinding;

/// MIDI learn for the standalone app (4.4): any MIDI input's CC or note-on
/// messages can be bound to any parameter. There is no per-device picker
/// (out of scope for 4.4): every available MIDI input is opened and listened
/// to, the way most DAWs' global "MIDI learn" works, and inputs plugged in
/// later are opened when JUCE reports the device list changed.
///
/// This class opens the inputs itself rather than through an
/// `AudioDeviceManager`. A device's driver is free to block opening for a
/// long time, or forever, and observably does on at least one real USB MIDI
/// interface, so opening must not happen on the message thread. But JUCE's
/// MIDI device list and `AudioDeviceManager` are message-thread APIs, and
/// driving them from a background thread (as this class used to) raced with
/// the message thread and corrupted the heap in a few percent of test runs.
/// Now only `juce::MidiInput::openDevice` runs on a background thread, after
/// the message thread has listed the devices; the opened input comes back to
/// the message thread, which keeps and starts it.
class MidiLearn final : private juce::MidiInputCallback {
public:
  explicit MidiLearn(ParameterBinding& binding);
  ~MidiLearn() override;
  MidiLearn(const MidiLearn&) = delete;
  MidiLearn& operator=(const MidiLearn&) = delete;
  MidiLearn(MidiLearn&&) = delete;
  MidiLearn& operator=(MidiLearn&&) = delete;

  /// Arms learn mode for `parameterId`: the next CC or note-on message from
  /// any MIDI input binds to it, replacing any existing mapping for that
  /// parameter and any other parameter already mapped to that same source.
  /// Message thread.
  void startLearning(const std::string& parameterId);
  void cancelLearning();
  [[nodiscard]] bool isLearning() const noexcept { return !learningParameterId_.empty(); }
  [[nodiscard]] const std::string& learningParameterId() const noexcept {
    return learningParameterId_;
  }

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

  /// Shared with the background open threads and pending message-thread
  /// callbacks. `alive` turns false in the destructor; `running` counts the
  /// open threads, which the destructor waits for (bounded) so none is still
  /// inside JUCE when this object, or JUCE itself, goes away.
  struct OpenState {
    std::mutex mutex;
    std::condition_variable idle;
    int running = 0;
    bool alive = true;
  };

  void handleIncomingMidiMessage(juce::MidiInput* source,
                                 const juce::MidiMessage& message) override;
  void applyIncoming(Source source, float value);
  /// Message thread: opens (in the background) every input not yet open or
  /// being opened, and forgets inputs that have gone away.
  void openNewInputs();
  void fireChanged();

  ParameterBinding& binding_;
  std::shared_ptr<OpenState> openState_ = std::make_shared<OpenState>();
  // Shared only because it comes through a copyable std::function (callAsync).
  std::vector<std::shared_ptr<juce::MidiInput>> inputs_; // started; message thread only
  std::set<juce::String> requested_; // open or being opened; message thread only
  juce::MidiDeviceListConnection deviceListConnection_;

  std::unordered_map<std::string, Source> mappings_; // parameterId -> source; message thread only
  std::string learningParameterId_;
};

} // namespace milkdawp::app
