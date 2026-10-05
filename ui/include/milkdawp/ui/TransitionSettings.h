// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/ui/KeyboardNavigation.h"

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
  bool grid = false;           // BeatQuantized, and Energy's fallback to it: cut on a fixed beat grid
  bool gridOffset = false;     // grid relevant and switched on
};

[[nodiscard]] TransitionSettingsRelevance transitionSettingsRelevance(core::TransitionMode mode, bool jitterEnabled,
                                                                      bool hardCuts, bool gridSync = false) noexcept;

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
  static constexpr int preferredHeight = 232;

  TransitionSettingsPanel();
  ~TransitionSettingsPanel() override;

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override { focusRing_.paint(g); }
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
  /// Layers: cut on the beats where beat % (bars x 4) == the offset, so
  /// instances following the host's tempo cut together (or staggered).
  juce::ToggleButton gridToggle{"Bar grid"};
  juce::Slider gridOffsetSlider;
  juce::ToggleButton hardCutToggle{"Hard cuts"};
  juce::Slider blendSlider;
  /// 5.5: `beatSensitivity`, projectM's reactivity. Not a transition
  /// setting, but this is where both shells' settings live; it is labelled
  /// for what it does rather than for its parameter name.
  juce::Slider reactivitySlider;
  /// 5.2: automatic picks only choose presets with one of these tags
  /// (comma-separated; empty: any). Not a parameter: the shell saves it and
  /// passes it to `Director::setTagFilter` from `onTagFilterChanged`.
  juce::TextEditor tagFilterEditor;
  std::function<void(const juce::String&)> onTagFilterChanged;
  /// "12 of 340 presets", or a warning when no preset matches.
  void setTagFilterStatus(const juce::String& text, bool warning);

private:
  struct Row {
    juce::Label label;
    juce::Component* control = nullptr;
  };
  void addRow(Row& row, const juce::String& text, juce::Component& control);

  juce::Label titleLabel;
  juce::TextButton closeButton{"Close"};
  Row modeRow, barsRow, timedRow, energyRow, blendRow, jitterMinRow, jitterMaxRow, tagsRow, reactivityRow;
  juce::Label tagFilterStatus;

  KeyboardFocusRing focusRing_{*this}; // 5.8

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransitionSettingsPanel)
};

/// 5.2: the "Only tags" row's status: how many presets automatic picks can
/// choose (`DirectorStatus::autoSelectable` of `playlistSize`).
struct AutoSelectionStatus {
  juce::String text;
  bool warning = false;
};
[[nodiscard]] AutoSelectionStatus describeAutoSelection(std::uint32_t autoSelectable, std::uint32_t playlistSize,
                                                        bool filterMatchesNothing);

/// 5.3: the render scale for the drawer's preset detail line, e.g.
/// "render 70%". Empty at full resolution, so it only appears when quality
/// is actually reduced. A fixed (non-Auto) choice says so.
[[nodiscard]] juce::String describeRenderQuality(float scale, bool automatic);

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
