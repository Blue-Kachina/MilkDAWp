// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "milkdawp/core/OscAddress.h"
#include "milkdawp/engine/Director.h"

namespace milkdawp::engine {

/// `core::OscSettings` in the user data folder (`osc.json`), beside the preset
/// metadata: one setting for the app and every plugin instance.
[[nodiscard]] juce::File oscSettingsFile();
[[nodiscard]] core::OscSettings loadOscSettings(const juce::File& file);
bool saveOscSettings(const core::OscSettings& settings, const juce::File& file);

/// Phase 8.4: OSC in and out, one per process (like `LayerRegistry`), shared by
/// every instance that holds a `juce::SharedResourcePointer<OscRemote>`.
///
/// In: messages addressed as `core::parseOscAddress` reads them go to every
/// endpoint they address, on the message thread, so each shell can make the
/// move a real parameter gesture (in the plugin the DAW records it as
/// automation; automation playing back still wins on its next block).
///
/// Out, to `sendHost:sendPort`, per endpoint (`<name>` as `core::oscName`):
///   /milkdawp/<name>/beat    i   the beat count, on each beat
///   /milkdawp/<name>/bar     i   the bar count, on each bar
///   /milkdawp/<name>/drop        a drop (Energy mode's detector)
///   /milkdawp/<name>/preset  s   the preset now playing
///
/// Off until enabled (`core::OscSettings`). Message thread only.
class OscRemote final : private juce::Timer {
public:
  struct Endpoint {
    std::function<std::string()> name; // the instance's display name
    std::function<std::string()> id;
    /// A parameter move from OSC: `value` in the parameter's units, or 0..1
    /// when `normalized`. False if the id isn't a parameter.
    std::function<bool(const std::string& parameterId, float value, bool normalized)> setParameter;
    std::function<void()> next;
    std::function<void()> previous;
    /// For the outbound signals; may be empty.
    std::function<DirectorStatus()> status;
    std::function<std::string()> presetName;
  };

  OscRemote();
  ~OscRemote() override;
  OscRemote(const OscRemote&) = delete;
  OscRemote& operator=(const OscRemote&) = delete;

  /// Returns a handle for `remove()`.
  int add(Endpoint endpoint);
  void remove(int handle);

  /// Starts, restarts or stops listening and sending. Loaded from
  /// `oscSettingsFile()` when the remote is created; `apply()` does not save.
  void apply(const core::OscSettings& settings);
  [[nodiscard]] const core::OscSettings& settings() const noexcept { return settings_; }
  /// "Listening on port 9000, sending to 127.0.0.1:9001", "Off", or why it failed.
  [[nodiscard]] juce::String statusText() const;

  /// Handles one incoming message as if it had arrived (also what the receiver
  /// calls). Returns how many endpoints it reached.
  int dispatch(const juce::String& address, const juce::String& firstArgument, float value, bool hasValue);

private:
  struct Impl;
  void timerCallback() override;

  core::OscSettings settings_;
  std::map<int, Endpoint> endpoints_;
  int nextHandle_ = 1;
  std::unique_ptr<Impl> impl_;
};

} // namespace milkdawp::engine
