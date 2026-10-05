// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// 5.8: the keyboard half of a popover (Transitions, Output, Diagnostics).
///
/// The drawer's own controls never take keyboard focus (a focused button
/// would swallow Space, the DAW's transport key, and make the shortcuts
/// depend on what was clicked last), so the keyboard reaches them through
/// the shortcuts instead. A popover is different: the user opened it to work
/// in it. So once open its sliders, combo boxes, buttons and text fields
/// take focus: Tab / Shift+Tab move between them, arrows / Space / Return
/// operate them, and Esc (unhandled by the control, so it reaches the shell)
/// closes the popover and gives focus back to the window.

/// A widget a keyboard user operates: slider, combo box, button, text field.
[[nodiscard]] bool isKeyboardOperable(const juce::Component& component) noexcept;

/// Makes `panel` a keyboard focus container and every operable widget in it
/// focusable. Safe to call again (rows added since are picked up).
void makeKeyboardNavigable(juce::Component& panel);

/// makeKeyboardNavigable(), then focuses the first control in Tab order.
/// Call right after showing the popover. False if nothing could take focus
/// (the panel is not on screen, or has no controls).
bool focusFirstControl(juce::Component& panel);

/// Draws an accent outline around whichever of `panel`'s widgets has
/// keyboard focus, and repaints the panel whenever focus moves, so a
/// keyboard user always sees where they are. Construct as a panel member and
/// call `paint()` from its `paintOverChildren()`.
class KeyboardFocusRing final : private juce::FocusChangeListener {
public:
  explicit KeyboardFocusRing(juce::Component& panel);
  ~KeyboardFocusRing() override;
  KeyboardFocusRing(const KeyboardFocusRing&) = delete;
  KeyboardFocusRing& operator=(const KeyboardFocusRing&) = delete;

  void paint(juce::Graphics& g) const;

private:
  void globalFocusChanged(juce::Component* focused) override;

  juce::Component& panel_;
  bool painted_ = false;
};

} // namespace milkdawp::ui
