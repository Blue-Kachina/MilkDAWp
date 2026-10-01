// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/DisplayLayout.h"

#include <algorithm>
#include <cmath>

namespace milkdawp::core {

int findDisplay(std::span<const WindowBounds> displays, const WindowBounds& target) noexcept {
  if (target.isEmpty()) {
    return -1;
  }
  for (std::size_t i = 0; i < displays.size(); ++i) {
    if (displays[i] == target) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

WindowBounds placeOnDisplay(const WindowBounds& saved, const WindowBounds& display, int defaultWidth,
                            int defaultHeight) noexcept {
  const auto centreX = saved.x + saved.width / 2;
  const auto centreY = saved.y + saved.height / 2;
  const bool savedIsOnDisplay = !saved.isEmpty() && centreX >= display.x && centreX < display.x + display.width &&
                                centreY >= display.y && centreY < display.y + display.height;

  WindowBounds result = saved;
  if (!savedIsOnDisplay) {
    result.width = defaultWidth;
    result.height = defaultHeight;
  }
  result.width = std::min(result.width, display.width);
  result.height = std::min(result.height, display.height);
  if (!savedIsOnDisplay) {
    result.x = display.x + (display.width - result.width) / 2;
    result.y = display.y + (display.height - result.height) / 2;
  }
  return result;
}

std::vector<WindowBounds> layoutDisplayMap(std::span<const WindowBounds> displays, int width, int height, int gap) {
  std::vector<WindowBounds> tiles;
  if (displays.empty() || width <= 0 || height <= 0) {
    return tiles;
  }

  int left = displays.front().x;
  int top = displays.front().y;
  int right = left + displays.front().width;
  int bottom = top + displays.front().height;
  for (const auto& d : displays) {
    left = std::min(left, d.x);
    top = std::min(top, d.y);
    right = std::max(right, d.x + d.width);
    bottom = std::max(bottom, d.y + d.height);
  }
  const double desktopWidth = std::max(right - left, 1);
  const double desktopHeight = std::max(bottom - top, 1);
  const double scale = std::min(width / desktopWidth, height / desktopHeight);
  const double offsetX = (width - desktopWidth * scale) / 2.0;
  const double offsetY = (height - desktopHeight * scale) / 2.0;

  tiles.reserve(displays.size());
  for (const auto& d : displays) {
    const int x = static_cast<int>(std::lround(offsetX + (d.x - left) * scale));
    const int y = static_cast<int>(std::lround(offsetY + (d.y - top) * scale));
    const int w = static_cast<int>(std::lround(d.width * scale));
    const int h = static_cast<int>(std::lround(d.height * scale));
    tiles.push_back({x + gap, y + gap, std::max(w - 2 * gap, 1), std::max(h - 2 * gap, 1)});
  }
  return tiles;
}

} // namespace milkdawp::core
