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

private:
  bool floating_ = false;
};

} // namespace milkdawp::ui
