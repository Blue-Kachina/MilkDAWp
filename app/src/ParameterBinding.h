// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/engine/ControlMapping.h"

namespace milkdawp::app {

class MidiLearn;

/// The app's stand-in for the plugin's APVTS attachments: binds the shared
/// drawer and transition-panel widgets to a `engine::ParameterValues`, with
/// ranges, steps and defaults taken from `core::ParameterModel` (the single
/// source of truth), so both shells offer identical controls.
///
/// A widget change writes the value and calls `onChanged`; `set()`/`toggle()`
/// (shortcuts, the menu bar, MIDI learn) write the value and refresh every
/// widget bound to that id. Message thread only. Widgets must outlive the
/// binding (declare it after them).
class ParameterBinding final : private juce::MouseListener {
public:
  ParameterBinding(engine::ParameterValues& values, std::function<void()> onChanged);
  ~ParameterBinding() override;

  void bind(juce::Slider& slider, std::string id);
  /// A toggle: 0/1.
  void bind(juce::Button& button, std::string id);
  /// A choice: item id = choice index + 1 (as the combo boxes are filled).
  void bind(juce::ComboBox& combo, std::string id);

  [[nodiscard]] float get(std::string_view id) const noexcept;
  /// Clamped to the parameter's range; a no-op for unknown ids.
  void set(std::string_view id, float value);
  void toggle(std::string_view id) { set(id, get(id) > 0.5f ? 0.0f : 1.0f); }

  /// Pushes every value to its widgets (after `values` changed wholesale).
  void refreshAll();

  /// §4.4: every bound widget gets a right-click "MIDI Learn..." affordance
  /// once this is called (before any `bind()`, ideally, so widgets bound
  /// earlier also get it retrofitted -- this loops over existing entries
  /// too). Call once, after both this and `midiLearn` exist.
  void attachMidiLearn(MidiLearn& midiLearn);
  /// Updates every bound widget's tooltip to show its current MIDI mapping,
  /// or "listening..." for the one `midiLearn` is currently learning.
  /// Called by the owner whenever `MidiLearn::onChanged` fires.
  void refreshMidiLearnTooltips();

private:
  enum class Kind { Slider, Button, ComboBox };
  struct Entry {
    Kind kind;
    juce::Component* widget;
    std::string id;
  };
  void refresh(const Entry& entry);
  void write(std::string_view id, float value);
  void mouseDown(const juce::MouseEvent& event) override;

  engine::ParameterValues& values_;
  std::function<void()> onChanged_;
  std::vector<Entry> entries_;
  MidiLearn* midiLearn_ = nullptr;
};

} // namespace milkdawp::app
