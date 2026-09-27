// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/TransitionScheduler.h"

namespace milkdawp::ui {

/// Which transition settings matter for the current mode (§4.4), so the
/// panel can dim the rest instead of offering knobs that do nothing. Pure,
/// so it is unit-tested without a window.
struct TransitionSettingsRelevance {
  bool bars = false;           // BeatQuantized, Hybrid, Energy (its bar-granular fallback)
  bool timedDuration = false;  // Timed, Hybrid, and BeatQuantized's low-confidence fallback
  bool jitter = false;         // same modes as timedDuration: jitter replaces the fixed duration
  bool jitterRange = false;    // jitter relevant and switched on
  bool energyThreshold = false; // Energy only
  bool blend = false;          // soft cuts only
};

[[nodiscard]] TransitionSettingsRelevance transitionSettingsRelevance(core::TransitionMode mode, bool jitterEnabled,
                                                                      bool hardCuts) noexcept;

/// Phase 3.4: the transition settings popover's widgets -- mode, bars (N),
/// timed duration, jitter and its range, energy threshold, hard cuts, blend.
/// Like `ControlDrawer`, it owns only UI-layer widgets: the shell attaches
/// them to its parameters (the plugin through APVTS attachments) and calls
/// `refreshRelevance()` after any of them changes. The mode combo is filled
/// from `core::ParameterModel` so its items cannot drift from the parameter.
///
/// Shown as a child of the editor's `OutputSurface`, above the drawer, never
/// as a separate desktop window: its lifetime is then the editor's, so the
/// attachments cannot outlive the processor when a host removes the plugin
/// with the popover open, and it composites over the GL content like the
/// drawer does (§4.11).
class TransitionSettingsPanel : public juce::Component {
public:
  static constexpr int preferredWidth = 440;
  static constexpr int preferredHeight = 176;

  TransitionSettingsPanel();
  ~TransitionSettingsPanel() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

  /// Dims the widgets that do nothing in the current mode.
  void refreshRelevance();

  std::function<void()> onCloseRequested;

  juce::ComboBox modeCombo;
  juce::Slider barsSlider;
  juce::Slider timedDurationSlider;
  juce::ToggleButton jitterToggle{"Jitter"};
  juce::Slider jitterMinSlider;
  juce::Slider jitterMaxSlider;
  juce::Slider energyThresholdSlider;
  juce::ToggleButton hardCutToggle{"Hard cuts"};
  juce::Slider blendSlider;

private:
  struct Row {
    juce::Label label;
    juce::Component* control = nullptr;
  };
  void addRow(Row& row, const juce::String& text, juce::Component& control);

  juce::Label titleLabel;
  juce::TextButton closeButton{"Close"};
  Row modeRow, barsRow, timedRow, energyRow, blendRow, jitterMinRow, jitterMaxRow;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransitionSettingsPanel)
};

enum class BeatBadgeSource { None, Detected, Host };

/// Phase 3.5: what the drawer's beat badge shows. The host's tempo is exact,
/// so it is marked "host" and needs no confidence; a detected tempo shows its
/// confidence, and dims while the detector is unsure.
struct BeatBadge {
  juce::String text;
  juce::String tooltip;
  juce::Colour colour;
};

[[nodiscard]] BeatBadge describeBeat(BeatBadgeSource source, float bpm, float confidence);

} // namespace milkdawp::ui
