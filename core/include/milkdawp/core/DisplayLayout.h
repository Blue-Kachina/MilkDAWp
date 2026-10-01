// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <span>
#include <vector>

#include "milkdawp/core/StateSchema.h"

namespace milkdawp::core {

/// Pure geometry behind Settings -> Output's "Target screen" (M0 in
/// layers_like_shrek.md). JUCE-free so it is unit-tested; the shell feeds it
/// the display bounds JUCE reports.

/// Index of the display whose bounds equal `target`, or -1 if `target` is
/// empty ("automatic") or that display is not connected right now.
[[nodiscard]] int findDisplay(std::span<const WindowBounds> displays, const WindowBounds& target) noexcept;

/// Where the Output window opens on `display`. `saved` is kept when its centre
/// is already on that display (the user placed it there); otherwise a
/// `defaultWidth` x `defaultHeight` window is centred on the display. The
/// result never exceeds the display's size. Fullscreen covers the display
/// these bounds fall on, so this also decides which screen goes fullscreen.
[[nodiscard]] WindowBounds placeOnDisplay(const WindowBounds& saved, const WindowBounds& display, int defaultWidth,
                                          int defaultHeight) noexcept;

/// A scaled-down map of the desktop for the monitor picker: one tile per
/// display, in the same order, fitted inside `width` x `height` with the
/// desktop's aspect ratio and relative positions preserved. Tiles are inset
/// by `gap` pixels per side so neighbouring displays do not touch.
[[nodiscard]] std::vector<WindowBounds> layoutDisplayMap(std::span<const WindowBounds> displays, int width,
                                                         int height, int gap = 2);

} // namespace milkdawp::core
