// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MainWindow.h"

namespace milkdawp::app {

namespace {
constexpr int kMinWidth = 480; // §4.9: small minimum; the drawer collapses to icons
constexpr int kMinHeight = 270;

bool isOnAConnectedDisplay(juce::Rectangle<int> bounds) {
  for (const auto& display : juce::Desktop::getInstance().getDisplays().displays) {
    if (display.userBounds.intersects(bounds.toFloat())) {
      return true;
    }
  }
  return false;
}
} // namespace

MainWindow::MainWindow(const juce::String& title, std::unique_ptr<MainComponent> content, AppState& state)
    : DocumentWindow(title, juce::Colours::black, DocumentWindow::allButtons), state_(state), content_(content.get()) {
  setUsingNativeTitleBar(true);
  setContentOwned(content.release(), true);
  setResizable(true, false);
  setResizeLimits(kMinWidth, kMinHeight, 16384, 16384);

#if JUCE_MAC
  juce::MenuBarModel::setMacMainMenu(this);
#else
  setMenuBar(this);
#endif

  content_->onToggleMainFullscreen = [this] { setFullscreen(!fullscreen_); };
  content_->isMainFullscreen = [this] { return fullscreen_; };

  const auto saved = state_.mainWindowBounds;
  if (!saved.isEmpty() && isOnAConnectedDisplay(saved)) {
    setBounds(saved);
  } else {
    centreWithSize(getWidth(), getHeight());
  }
  setVisible(true);
  if (state_.mainWindowFullscreen) {
    setFullscreen(true);
  }
}

MainWindow::~MainWindow() {
  if (fullscreen_) {
    juce::Desktop::getInstance().setKioskModeComponent(nullptr, false);
  }
#if JUCE_MAC
  juce::MenuBarModel::setMacMainMenu(nullptr);
#else
  setMenuBar(nullptr);
#endif
  clearContentComponent();
}

void MainWindow::setFullscreen(bool shouldBeFullscreen) {
  if (shouldBeFullscreen == fullscreen_) {
    return;
  }
  if (shouldBeFullscreen) {
    rememberBounds(); // the windowed bounds to come back to
  }
  fullscreen_ = shouldBeFullscreen;
#if !JUCE_MAC
  setMenuBar(shouldBeFullscreen ? nullptr : this);
#endif
  juce::Desktop::getInstance().setKioskModeComponent(shouldBeFullscreen ? this : nullptr, /*allowMenusAndBars=*/false);
  if (!shouldBeFullscreen && !state_.mainWindowBounds.isEmpty()) {
    setBounds(state_.mainWindowBounds);
  }
  content_->mainFullscreenChanged(shouldBeFullscreen);
  state_.mainWindowFullscreen = shouldBeFullscreen;
  if (onStateChanged) {
    onStateChanged();
  }
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
  return content_->createMenu(topLevelMenuIndex);
}

void MainWindow::closeButtonPressed() { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

void MainWindow::moved() {
  DocumentWindow::moved();
  rememberBounds();
}

void MainWindow::resized() {
  DocumentWindow::resized();
  rememberBounds();
}

void MainWindow::rememberBounds() {
  if (fullscreen_ || !isVisible() || isMinimised()) {
    return;
  }
  if (getBounds() != state_.mainWindowBounds) {
    state_.mainWindowBounds = getBounds();
    if (onStateChanged) {
      onStateChanged();
    }
  }
}

} // namespace milkdawp::app
