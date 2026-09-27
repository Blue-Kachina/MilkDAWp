// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Icon path data from Lucide (https://lucide.dev), ISC licence:
// Copyright (c) for portions of Lucide are held by Cole Bemis 2013-2022 as
// part of Feather (MIT). All other copyright (c) for Lucide are held by
// Lucide Contributors 2022. Permission to use, copy, modify, and/or
// distribute this software for any purpose with or without fee is hereby
// granted, provided that the above copyright notice and this permission
// notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE
// AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE.
//
// Circles, rects and lines are rewritten as path data, and every command is
// explicit (no implicit repeats), so each icon is one parseSVGPath() string.

#include "milkdawp/ui/Icons.h"

#include <algorithm>
#include <cstddef>

namespace milkdawp::ui {

namespace {

struct IconSource {
  const char* stroke;
  const char* fill; // nullptr: nothing filled
};

// Circle of radius r at (cx, cy) as two arcs, for the strings below.
#define MDW_CIRCLE(cx, cy, r, left, right) "M" right " " cy "A" r " " r " 0 1 1 " left " " cy "A" r " " r " 0 1 1 " right " " cy "Z"

IconSource sourceFor(Icon icon) {
  switch (icon) {
  case Icon::Prev:
    return {"M6 5V19M19 5L9 12L19 19Z", "M19 5L9 12L19 19Z"};
  case Icon::Next:
    return {"M18 5V19M5 5L15 12L5 19Z", "M5 5L15 12L5 19Z"};
  case Icon::Lock:
    return {"M6 11H18A2 2 0 0 1 20 13V19A2 2 0 0 1 18 21H6A2 2 0 0 1 4 19V13A2 2 0 0 1 6 11Z"
            "M8 11V7A4 4 0 0 1 16 7V11",
            nullptr};
  case Icon::Unlock:
    return {"M6 11H18A2 2 0 0 1 20 13V19A2 2 0 0 1 18 21H6A2 2 0 0 1 4 19V13A2 2 0 0 1 6 11Z"
            "M8 11V7A4 4 0 0 1 15.9 6",
            nullptr};
  case Icon::Shuffle:
    return {"M16 3H21V8M4 20L21 3M21 16V21H16M15 15L21 21M4 4L9 9", nullptr};
  case Icon::PopOut: // lucide "square-arrow-out-up-right"
    return {"M21 13V19A2 2 0 0 1 19 21H5A2 2 0 0 1 3 19V5A2 2 0 0 1 5 3H11M21 3L12 12M15 3H21V9", nullptr};
  case Icon::Settings:
    return {MDW_CIRCLE("12", "12", "3", "9", "15")
            "M19.4 15A1.65 1.65 0 0 0 19.73 16.82L19.79 16.88A2 2 0 1 1 16.96 19.71L16.9 19.65"
            "A1.65 1.65 0 0 0 15.08 19.32A1.65 1.65 0 0 0 14.08 20.83V21A2 2 0 1 1 10.08 21V20.91"
            "A1.65 1.65 0 0 0 9 19.4A1.65 1.65 0 0 0 7.18 19.73L7.12 19.79A2 2 0 1 1 4.29 16.96L4.35 16.9"
            "A1.65 1.65 0 0 0 4.68 15A1.65 1.65 0 0 0 3.17 14H3A2 2 0 1 1 3 10H3.09"
            "A1.65 1.65 0 0 0 4.6 9A1.65 1.65 0 0 0 4.27 7.18L4.21 7.12A2 2 0 1 1 7.04 4.29L7.1 4.35"
            "A1.65 1.65 0 0 0 9 4.68A1.65 1.65 0 0 0 10 3.17V3A2 2 0 1 1 14 3V3.09"
            "A1.65 1.65 0 0 0 15 4.6A1.65 1.65 0 0 0 16.82 4.27L16.88 4.21A2 2 0 1 1 19.71 7.04L19.65 7.1"
            "A1.65 1.65 0 0 0 19.4 9A1.65 1.65 0 0 0 20.91 10H21A2 2 0 1 1 21 14H20.91"
            "A1.65 1.65 0 0 0 19.4 15Z",
            nullptr};
  case Icon::Pin:
    return {"M12 17V22M5 17H19V15.2A2 2 0 0 0 17.9 13.4L16.1 12.5A2 2 0 0 1 15 10.7V6H16A2 2 0 0 0 16 2H8"
            "A2 2 0 0 0 8 6H9V10.7A2 2 0 0 1 7.9 12.5L6.1 13.4A2 2 0 0 0 5 15.2Z",
            nullptr};
  case Icon::More:
    return {MDW_CIRCLE("12", "5", "1", "11", "13") MDW_CIRCLE("12", "12", "1", "11", "13")
                MDW_CIRCLE("12", "19", "1", "11", "13"),
            nullptr};
  case Icon::ChevronDown:
    return {"M6 9L12 15L18 9", nullptr};
  case Icon::ChevronUp:
    return {"M18 15L12 9L6 15", nullptr};
  case Icon::Check:
    return {"M20 6L9 17L4 12", nullptr};
  case Icon::Folder:
    return {"M20 20A2 2 0 0 0 22 18V8A2 2 0 0 0 20 6H12.1A2 2 0 0 1 10.4 5.1L9.6 3.9A2 2 0 0 0 7.93 3H4"
            "A2 2 0 0 0 2 5V18A2 2 0 0 0 4 20Z",
            nullptr};
  case Icon::Rescan: // lucide "rotate-cw"
    return {"M21 12A9 9 0 1 1 18.36 5.64L21 8M21 3V8H16", nullptr};
  case Icon::ModeManual: // lucide "hand"
    return {"M18 11V6A2 2 0 0 0 14 6M14 10V4A2 2 0 0 0 10 4V6M10 10.5V6A2 2 0 0 0 6 6V14"
            "M18 8A2 2 0 1 1 22 8V14A8 8 0 0 1 14 22H12C9.2 22 7.5 21.14 6.01 19.66L2.41 16.06"
            "A2 2 0 0 1 5.24 13.24L7 15",
            nullptr};
  case Icon::ModeTimed: // lucide "clock"
    return {MDW_CIRCLE("12", "12", "9", "3", "21") "M12 7V12L15.5 14", nullptr};
  case Icon::ModeBeat: // a metronome
    return {"M9 3H15L19 21H5ZM12 17L17 7M7.5 14H16.5", nullptr};
  case Icon::ModeHybrid: // two overlapping circles
    return {MDW_CIRCLE("9", "12", "6", "3", "15") MDW_CIRCLE("15", "12", "6", "9", "21"), nullptr};
  case Icon::ModeEnergy: // lucide "zap"
    return {"M13 2L3 14H12L11 22L21 10H12Z", nullptr};
  }
  return {"", nullptr};
}

#undef MDW_CIRCLE

struct ParsedIcon {
  juce::Path stroke;
  juce::Path fill;
};

const ParsedIcon& parsed(Icon icon) {
  static const auto table = [] {
    std::array<ParsedIcon, allIcons.size()> result;
    for (std::size_t i = 0; i < allIcons.size(); ++i) {
      const auto source = sourceFor(allIcons[i]);
      result[i].stroke = juce::Drawable::parseSVGPath(source.stroke);
      // Pin the bounds to the whole 24x24 grid: move-tos add no ink but do
      // count towards getBounds(), so every icon scales by the same factor.
      result[i].stroke.startNewSubPath(0.0f, 0.0f);
      result[i].stroke.startNewSubPath(24.0f, 24.0f);
      if (source.fill != nullptr) {
        result[i].fill = juce::Drawable::parseSVGPath(source.fill);
      }
    }
    return result;
  }();
  return table[static_cast<std::size_t>(icon)];
}

} // namespace

const juce::Path& iconStrokePath(Icon icon) { return parsed(icon).stroke; }
const juce::Path& iconFillPath(Icon icon) { return parsed(icon).fill; }

void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour) {
  const auto size = std::min(area.getWidth(), area.getHeight());
  const auto scale = size / 24.0f;
  const auto box = area.withSizeKeepingCentre(size, size);
  const auto transform = juce::AffineTransform::scale(scale).translated(box.getX(), box.getY());

  const auto& icons = parsed(icon);
  g.setColour(colour);
  if (!icons.fill.isEmpty()) {
    g.fillPath(icons.fill, transform);
  }
  g.strokePath(icons.stroke,
               juce::PathStrokeType(2.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
               transform);
}

std::unique_ptr<juce::Drawable> createIconDrawable(Icon icon, juce::Colour colour) {
  const auto& icons = parsed(icon);
  juce::Path outline;
  juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
      .createStrokedPath(outline, icons.stroke);
  outline.addPath(icons.fill);
  outline.startNewSubPath(0.0f, 0.0f); // keep the 24x24 box, as above
  outline.startNewSubPath(24.0f, 24.0f);

  auto drawable = std::make_unique<juce::DrawablePath>();
  drawable->setPath(outline);
  drawable->setFill(colour);
  return drawable;
}

Icon iconForTransitionMode(int choiceIndex) noexcept {
  switch (choiceIndex) {
  case 0:
    return Icon::ModeManual;
  case 1:
    return Icon::ModeTimed;
  case 2:
    return Icon::ModeBeat;
  case 3:
    return Icon::ModeHybrid;
  case 4:
    return Icon::ModeEnergy;
  default:
    return Icon::ModeTimed;
  }
}

} // namespace milkdawp::ui
