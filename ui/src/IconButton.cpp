// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/IconButton.h"

#include <algorithm>

#include "milkdawp/ui/DrawerLookAndFeel.h"

namespace milkdawp::ui {

IconButton::IconButton(const juce::String& name, Icon icon, std::optional<Icon> onIcon)
    : juce::Button(name), icon_(icon), onIcon_(onIcon) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void IconButton::setIcons(Icon icon, std::optional<Icon> onIcon) {
  icon_ = icon;
  onIcon_ = onIcon;
  repaint();
}

void IconButton::paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) {
  const auto bounds = getLocalBounds().toFloat();
  const auto side = std::min(bounds.getWidth(), bounds.getHeight());
  const auto face = bounds.withSizeKeepingCentre(side, side).reduced(2.0f);

  if (isEnabled() && (isHighlighted || isDown)) {
    g.setColour(isDown ? drawerTheme::pressFill : drawerTheme::hoverFill);
    g.fillRoundedRectangle(face, 8.0f);
  }

  const bool on = getToggleState();
  auto colour = on ? drawerTheme::accent : drawerTheme::text;
  if (!isEnabled()) {
    colour = colour.withAlpha(0.4f);
  }

  const auto iconSize = std::min(22.0f, face.getWidth() - 8.0f);
  drawIcon(g, on && onIcon_ ? *onIcon_ : icon_, face.withSizeKeepingCentre(iconSize, iconSize), colour);

  if (on && getClickingTogglesState()) {
    g.setColour(colour);
    g.fillRoundedRectangle(face.getCentreX() - 8.0f, face.getBottom() - 5.0f, 16.0f, 2.0f, 1.0f);
  }
}

} // namespace milkdawp::ui
