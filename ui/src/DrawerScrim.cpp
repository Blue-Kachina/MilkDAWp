// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/DrawerScrim.h"

#include "milkdawp/ui/DrawerLookAndFeel.h"

namespace milkdawp::ui {

void DrawerScrim::paint(juce::Graphics& g) {
  if (floating_) {
    g.fillAll(drawerTheme::panel);
    return;
  }
  const auto height = static_cast<float>(getHeight());
  juce::ColourGradient gradient(juce::Colours::black.withAlpha(0.0f), 0.0f, 0.0f, juce::Colours::black.withAlpha(0.88f),
                                0.0f, height * 0.7f, false);
  g.setGradientFill(gradient);
  g.fillAll();
}

void DrawerScrim::setFloating(bool floating) {
  floating_ = floating;
  repaint();
}

} // namespace milkdawp::ui
