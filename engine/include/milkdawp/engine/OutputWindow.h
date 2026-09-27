// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/engine/OutputSurface.h"
#include "milkdawp/engine/RenderEngine.h"

namespace milkdawp::engine {

/// The Output window (§4.9, Phase 2.4): a top-level window we own, showing
/// the same frame as the primary window through its own `OutputSurface`.
/// Normally a titled, resizable desktop window; fullscreen is borderless
/// and covers the whole display the window is on (the OBS / projector
/// workflow). Never uses JUCE kiosk mode, which is process-wide and would
/// fight with a DAW host.
///
/// Keys (§4.9): F11 toggles fullscreen, Esc leaves it; double-click toggles
/// too. The title is fixed ("MilkDAWp Output") so OBS window capture keeps
/// finding it.
///
/// The owner decides the lifetime: closing the window (title-bar close
/// button) calls `onCloseRequested`, and the owner deletes it. In the
/// plugin the processor owns it, so it outlives the editor.
class OutputWindow final : public juce::Component {
public:
  static constexpr const char* kTitle = "MilkDAWp Output";

  explicit OutputWindow(RenderEngine& engine);
  ~OutputWindow() override;

  /// Shows the window at `windowedBounds`, optionally going straight to
  /// fullscreen. Empty bounds, or bounds on no connected display (a saved
  /// session opened after unplugging a monitor), fall back to a default
  /// size centred on the main display.
  void show(juce::Rectangle<int> windowedBounds, bool fullscreen);

  void setFullscreen(bool fullscreen);
  void toggleFullscreen() { setFullscreen(!fullscreen_); }
  [[nodiscard]] bool isFullscreen() const noexcept { return fullscreen_; }
  /// Bounds to restore when leaving fullscreen or reopening the window.
  [[nodiscard]] juce::Rectangle<int> windowedBounds() const noexcept { return windowedBounds_; }

  std::function<void()> onCloseRequested;
  /// Called on the message thread after the window moves, resizes, or
  /// enters or leaves fullscreen, so the owner can remember the layout.
  std::function<void()> onLayoutChanged;

  void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;
  void mouseDoubleClick(const juce::MouseEvent& event) override;
  void userTriedToCloseWindow() override;
  void moved() override;

private:
  void addToDesktopForMode();
  void notifyLayoutChanged();

  OutputSurface surface_;
  bool fullscreen_ = false;
  juce::Rectangle<int> windowedBounds_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputWindow)
};

} // namespace milkdawp::engine
