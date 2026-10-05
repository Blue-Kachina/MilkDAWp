// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/DrawerScrim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "milkdawp/ui/DrawerLookAndFeel.h"

namespace milkdawp::ui {

namespace {

// (proportion from the top, black alpha). Clear at the top so the drawer hides
// as little of the picture as it can; behind the progress track it is already
// half dark, and from the top of the button row (0.44 of the docked 96 px)
// down it is dark enough that the drawer's dimmest text keeps WCAG AA contrast
// (4.5:1) over a pure-white preset (5.8; the old straight ramp gave 3.4:1).
constexpr std::array<std::pair<float, float>, 4> kStops{{{0.0f, 0.0f}, {0.3f, 0.5f}, {0.44f, 0.8f}, {1.0f, 0.9f}}};

float linearChannel(float value) {
  return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float relativeLuminance(juce::Colour colour) {
  return 0.2126f * linearChannel(colour.getFloatRed()) + 0.7152f * linearChannel(colour.getFloatGreen()) +
         0.0722f * linearChannel(colour.getFloatBlue());
}

} // namespace

float DrawerScrim::alphaAt(float proportion) noexcept {
  if (proportion <= kStops.front().first) {
    return kStops.front().second;
  }
  for (std::size_t i = 1; i < kStops.size(); ++i) {
    const auto [y1, a1] = kStops[i];
    if (proportion <= y1) {
      const auto [y0, a0] = kStops[i - 1];
      return a0 + (a1 - a0) * (proportion - y0) / (y1 - y0);
    }
  }
  return kStops.back().second;
}

float contrastRatio(juce::Colour a, juce::Colour b) {
  const float la = relativeLuminance(a);
  const float lb = relativeLuminance(b);
  return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

void DrawerScrim::paint(juce::Graphics& g) {
  if (floating_) {
    g.fillAll(drawerTheme::panel);
    return;
  }
  const auto height = static_cast<float>(getHeight());
  juce::ColourGradient gradient(juce::Colours::black.withAlpha(kStops.front().second), 0.0f, 0.0f,
                                juce::Colours::black.withAlpha(kStops.back().second), 0.0f, height, false);
  for (std::size_t i = 1; i + 1 < kStops.size(); ++i) {
    gradient.addColour(kStops[i].first, juce::Colours::black.withAlpha(kStops[i].second));
  }
  g.setGradientFill(gradient);
  g.fillAll();
}

void DrawerScrim::setFloating(bool floating) {
  floating_ = floating;
  repaint();
}

} // namespace milkdawp::ui
