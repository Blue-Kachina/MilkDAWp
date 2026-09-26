// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/ui/DrawerScrim.h"
#include "milkdawp/ui/DrawerState.h"

namespace milkdawp::ui {

/// JUCE composition of Phase 2.11's `DrawerStateMachine` (§4.9): the control
/// row shown over the visualization. Deliberately owns only UI-layer widgets
/// -- no `juce::AudioProcessorValueTreeState` here, so `milkdawp_ui` stays
/// decoupled from the plugin layer (§4.1 layering) -- the owner (Phase 3.3's
/// `PluginEditor`) attaches these public widgets to real parameters and
/// wires `prevButton`/`nextButton`'s `onClick`.
///
/// Keeps a fixed footprint at the bottom of its parent at all times (alpha 0
/// when hidden, so it stays hoverable) rather than being added/removed --
/// the classic video-player "move the mouse near the bottom edge reveals the
/// controls" gesture, matching `DrawerStateMachine`'s own class comment that
/// `ControlDrawer` "forwards Component::mouseEnter/mouseDown to
/// onPointerActivity()".
///
/// `transitionModeCombo` is pre-populated from `core::ParameterModel`'s
/// `transitionMode` spec (ui/ already depends on core/, §4.1) so its item
/// list can never drift from the parameter's actual choices; the owner still
/// supplies the `ComboBoxAttachment` since that needs `juce_audio_processors`.
class ControlDrawer final : public juce::Component, private juce::Timer {
public:
  static constexpr int preferredHeight = 44;

  explicit ControlDrawer(DrawerStateMachine::Config config = {});
  ~ControlDrawer() override;

  void resized() override;
  void mouseEnter(const juce::MouseEvent&) override;
  void mouseDown(const juce::MouseEvent&) override;

  /// Esc: "if not fullscreen, reveal the drawer" (§4.9). Also usable by any
  /// other caller-driven reveal (counts as pointer activity).
  void reveal();
  /// 'H': reveal or hide, respecting pin.
  void toggleRevealHide();
  /// 'P': pin or unpin.
  void togglePin();
  [[nodiscard]] bool isPinned() const noexcept { return state_.isPinned(); }

  juce::TextButton prevButton{"<"};
  juce::TextButton nextButton{">"};
  juce::Label presetLabel;
  juce::TextButton lockButton;
  juce::TextButton shuffleButton;
  juce::ComboBox transitionModeCombo;
  juce::Label bpmLabel;
  juce::TextButton outputButton;
  juce::TextButton settingsButton;
  juce::TextButton pinButton;

private:
  void timerCallback() override;
  void updateVisualState();
  [[nodiscard]] static double nowSeconds();

  DrawerStateMachine state_;
  DrawerScrim scrim_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlDrawer)
};

} // namespace milkdawp::ui
