// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// What `ControlDrawer`'s controls sit on (§4.9): knobs over a moving
/// psychedelic field are unreadable without it. Docked, a video-player-style
/// gradient -- clear at the top, dark behind the controls -- so the drawer
/// covers as little of the visuals as it can; floating in its own window, a
/// solid ground. Optional blur is not implemented -- a real backdrop blur is
/// a separate, per-platform-cost investigation (Direct2D/EGL/CoreGraphics
/// each need their own approach) that the gradient does not need in order to
/// make the drawer legible, which is this component's whole job.
class DrawerScrim final : public juce::Component {
public:
  void paint(juce::Graphics& g) override;
  void setFloating(bool floating);

  /// The docked gradient's black alpha at `proportion` of the height from
  /// the top (0 = top edge). What paint() draws; exposed for the contrast test.
  [[nodiscard]] static float alphaAt(float proportion) noexcept;

private:
  bool floating_ = false;
};

/// WCAG 2 contrast ratio between two opaque colours (1 to 21). 4.5 is AA for
/// normal text, 3 for large text and icons.
[[nodiscard]] float contrastRatio(juce::Colour a, juce::Colour b);

} // namespace milkdawp::ui
