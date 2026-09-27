// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/ui/Icons.h"

using namespace milkdawp::ui;

TEST_CASE("Icon table order matches the enum", "[ui][Icons]") {
  for (std::size_t i = 0; i < allIcons.size(); ++i) {
    CHECK(static_cast<std::size_t>(allIcons[i]) == i);
  }
}

TEST_CASE("Every icon parses to ink inside its 24x24 box", "[ui][Icons]") {
  // parseSVGPath builds Drawables (Components) internally.
  const juce::ScopedJuceInitialiser_GUI juce;
  const juce::Rectangle<float> box(0.0f, 0.0f, 24.0f, 24.0f);
  for (const auto icon : allIcons) {
    INFO("icon " << static_cast<int>(icon));
    const auto& stroke = iconStrokePath(icon);
    CHECK(stroke.getBounds() == box); // pinned, so every icon scales alike

    // Ink, not just the two bounds-pinning move-tos. Rounded joins, as
    // drawIcon() strokes them (mitred, the zap's sharp corners spike out).
    juce::Path outline;
    juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath(outline, stroke);
    const auto ink = outline.getBounds().getUnion(iconFillPath(icon).getBounds());
    CHECK(std::max(ink.getWidth(), ink.getHeight()) > 12.0f); // the "more" dots are tall but narrow
    CHECK(std::min(ink.getWidth(), ink.getHeight()) > 2.0f);
    CHECK(box.expanded(1.0f).contains(ink)); // a 2-unit stroke may reach 1 unit past the grid
  }
}

TEST_CASE("Only the prev/next triangles are filled", "[ui][Icons]") {
  const juce::ScopedJuceInitialiser_GUI juce;
  CHECK_FALSE(iconFillPath(Icon::Prev).isEmpty());
  CHECK_FALSE(iconFillPath(Icon::Next).isEmpty());
  CHECK(iconFillPath(Icon::Lock).isEmpty());
}

TEST_CASE("Each transition mode has its own icon", "[ui][Icons]") {
  const auto* spec = milkdawp::core::findParameter(milkdawp::core::allParameters(), "transitionMode");
  REQUIRE(spec != nullptr);
  REQUIRE(spec->choices.size() == 5);
  CHECK(iconForTransitionMode(0) == Icon::ModeManual);
  CHECK(iconForTransitionMode(1) == Icon::ModeTimed);
  CHECK(iconForTransitionMode(2) == Icon::ModeBeat);
  CHECK(iconForTransitionMode(3) == Icon::ModeHybrid);
  CHECK(iconForTransitionMode(4) == Icon::ModeEnergy);
  CHECK(iconForTransitionMode(-1) == Icon::ModeTimed); // nothing selected yet
}
