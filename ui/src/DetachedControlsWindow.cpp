// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/ui/DetachedControlsWindow.h"

#include "milkdawp/ui/DrawerLookAndFeel.h"

namespace milkdawp::ui {

namespace {
constexpr int kDefaultWidth = 720;
constexpr int kMinWidth = 360;
} // namespace

DetachedControlsWindow::DetachedControlsWindow(juce::Component& controls)
    : juce::DocumentWindow(kTitle, drawerTheme::panel,
                           juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton,
                           /*addToDesktop=*/false),
      rowHeight_(std::max(controls.getHeight(), 1)) {
  // rowHeight_ is read before setContentNonOwned, which resizes the content
  // to this new window's default size.
  setUsingNativeTitleBar(true);
  setContentNonOwned(&controls, /*resizeToFitWhenContentChangesSize=*/false);
  // Only the width is free: the drawer row has one natural height.
  setResizable(true, false);
  setResizeLimits(kMinWidth, rowHeight_, 8192, rowHeight_);
  setWantsKeyboardFocus(true);
}

DetachedControlsWindow::~DetachedControlsWindow() { release(); }

void DetachedControlsWindow::release() {
  if (getContentComponent() != nullptr) {
    clearContentComponent(); // non-owned: detaches without deleting
  }
}

void DetachedControlsWindow::show(juce::Rectangle<int> bounds) {
  const auto& displays = juce::Desktop::getInstance().getDisplays();
  const auto* target = displays.getDisplayForRect(bounds);
  const bool onADisplay = target != nullptr && target->userBounds.toNearestInt().intersects(bounds);
  const auto height = rowHeight_;
  if (bounds.isEmpty() || !onADisplay) {
    const auto* main = displays.getPrimaryDisplay();
    const auto area = main != nullptr ? main->userBounds.toNearestInt() : juce::Rectangle<int>(0, 0, 1920, 1080);
    bounds = juce::Rectangle<int>(kDefaultWidth, height)
                 .withCentre(area.getCentre())
                 .withY(area.getBottom() - height - area.getHeight() / 6);
  }
  // Content-area bounds (native title bar): the height is always the row's.
  setBounds(bounds.withHeight(height));
  addToDesktop();
  setVisible(true);
  toFront(true);
}

void DetachedControlsWindow::closeButtonPressed() {
  if (onDockRequested) {
    onDockRequested();
  }
}

bool DetachedControlsWindow::keyPressed(const juce::KeyPress& key) {
  return onKeyPressed ? onKeyPressed(key) : false;
}

void DetachedControlsWindow::moved() {
  juce::DocumentWindow::moved();
  if (onLayoutChanged && isOnDesktop()) {
    onLayoutChanged();
  }
}

void DetachedControlsWindow::resized() {
  juce::DocumentWindow::resized();
  if (onLayoutChanged && isOnDesktop()) {
    onLayoutChanged();
  }
}

} // namespace milkdawp::ui
