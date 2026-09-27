// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/OutputWindow.h"

namespace milkdawp::engine {

namespace {
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;
} // namespace

OutputWindow::OutputWindow(RenderEngine& engine) : surface_(engine) {
  setName(kTitle);
  setOpaque(true);
  addAndMakeVisible(surface_);
  // Clicks and keys on the video go to the window.
  surface_.setInterceptsMouseClicks(false, false);
  setWantsKeyboardFocus(true);
}

OutputWindow::~OutputWindow() {
  // Detach the surface's GL context while the peer still exists.
  removeChildComponent(&surface_);
  if (isOnDesktop()) {
    removeFromDesktop();
  }
}

void OutputWindow::show(juce::Rectangle<int> windowedBounds, bool fullscreen) {
  const auto* target = juce::Desktop::getInstance().getDisplays().getDisplayForRect(windowedBounds);
  const bool onADisplay = target != nullptr && target->userBounds.toNearestInt().intersects(windowedBounds);
  if (windowedBounds.isEmpty() || !onADisplay) {
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int>(0, 0, 1920, 1080);
    windowedBounds = juce::Rectangle<int>(kDefaultWidth, kDefaultHeight).withCentre(area.getCentre());
  }
  windowedBounds_ = windowedBounds;
  fullscreen_ = fullscreen;
  addToDesktopForMode();
  setVisible(true);
  toFront(true);
  grabKeyboardFocus();
}

void OutputWindow::setFullscreen(bool fullscreen) {
  if (fullscreen == fullscreen_ || !isOnDesktop()) {
    fullscreen_ = fullscreen;
    return;
  }
  if (fullscreen) {
    windowedBounds_ = getScreenBounds();
  }
  fullscreen_ = fullscreen;
  addToDesktopForMode();
  toFront(true);
  grabKeyboardFocus();
  notifyLayoutChanged();
}

void OutputWindow::notifyLayoutChanged() {
  if (onLayoutChanged) {
    onLayoutChanged();
  }
}

void OutputWindow::addToDesktopForMode() {
  // Changing between titled and borderless means a new native window: the
  // surface's GL context is recreated and re-shares with the engine, which
  // is exactly the case the engine-owned context exists for.
  if (fullscreen_) {
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect(windowedBounds_);
    const auto area = display != nullptr ? display->logicalBounds.toNearestInt() : windowedBounds_;
    setBounds(area);
    addToDesktop(juce::ComponentPeer::windowAppearsOnTaskbar);
    setBounds(area);
  } else {
    setBounds(windowedBounds_);
    addToDesktop(juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowIsResizable |
                 juce::ComponentPeer::windowHasMinimiseButton | juce::ComponentPeer::windowHasMaximiseButton |
                 juce::ComponentPeer::windowHasCloseButton | juce::ComponentPeer::windowAppearsOnTaskbar);
    if (auto* peer = getPeer()) {
      peer->setConstrainer(nullptr);
    }
  }
}

void OutputWindow::resized() {
  surface_.setBounds(getLocalBounds());
  moved(); // a resize of the windowed window changes the bounds to remember too
}

void OutputWindow::moved() {
  if (!fullscreen_ && isOnDesktop()) {
    windowedBounds_ = getScreenBounds();
    notifyLayoutChanged();
  }
}

bool OutputWindow::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress(juce::KeyPress::F11Key)) {
    toggleFullscreen();
    return true;
  }
  if (key == juce::KeyPress(juce::KeyPress::escapeKey) && fullscreen_) {
    setFullscreen(false);
    return true;
  }
  return false;
}

void OutputWindow::mouseDoubleClick(const juce::MouseEvent&) { toggleFullscreen(); }

void OutputWindow::userTriedToCloseWindow() {
  if (onCloseRequested) {
    onCloseRequested();
  }
}

} // namespace milkdawp::engine
