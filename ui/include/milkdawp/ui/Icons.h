// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// The drawer's stroke icons: Lucide (https://lucide.dev, ISC licence) path
/// data on its 24x24 grid, drawn as vector paths. Paths rather than font
/// glyphs: JUCE's font fallback does not reliably find emoji or symbol
/// codepoints on every platform (the reason the drawer used plain text
/// labels before).
enum class Icon {
  Prev,
  Next,
  Lock,
  Unlock,
  Shuffle,
  PopOut,
  Settings,
  Pin,
  More,
  ChevronDown,
  ChevronUp,
  Check,
  Folder,
  Rescan,
  ModeManual,
  ModeTimed,
  ModeBeat,
  ModeHybrid,
  ModeEnergy,
};

inline constexpr std::array allIcons{
    Icon::Prev,        Icon::Next,      Icon::Lock,      Icon::Unlock,    Icon::Shuffle,
    Icon::PopOut,      Icon::Settings,  Icon::Pin,       Icon::More,      Icon::ChevronDown,
    Icon::ChevronUp,   Icon::Check,     Icon::Folder,    Icon::Rescan,    Icon::ModeManual,
    Icon::ModeTimed,   Icon::ModeBeat,  Icon::ModeHybrid, Icon::ModeEnergy,
};

/// The part drawn as a 2-unit stroke, in 24x24 icon units. Its bounds are
/// always exactly the icon's 24x24 box, so icons scale consistently.
[[nodiscard]] const juce::Path& iconStrokePath(Icon icon);
/// The part filled solid (the prev/next triangles); empty for most icons.
[[nodiscard]] const juce::Path& iconFillPath(Icon icon);

/// Draws `icon` scaled into `area` (square, 24 units -> area's size).
void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);

/// A filled outline of the icon, for places that take a Drawable (menu items).
[[nodiscard]] std::unique_ptr<juce::Drawable> createIconDrawable(Icon icon, juce::Colour colour);

/// The icon for a `transitionMode` choice index (ParameterModel order:
/// Manual, Timed, BeatQuantized, Hybrid, Energy).
[[nodiscard]] Icon iconForTransitionMode(int choiceIndex) noexcept;

} // namespace milkdawp::ui
