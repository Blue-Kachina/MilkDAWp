// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// Translucent dark band painted behind `ControlDrawer`'s controls (§4.9):
/// knobs over a moving psychedelic field are unreadable without it. Optional
/// blur is not implemented -- a real backdrop blur is a separate, per-
/// platform-cost investigation (Direct2D/EGL/CoreGraphics each need their
/// own approach) that flat translucency does not need in order to make the
/// drawer legible, which is this component's whole job.
class DrawerScrim final : public juce::Component {
public:
  void paint(juce::Graphics& g) override;
};

} // namespace milkdawp::ui
