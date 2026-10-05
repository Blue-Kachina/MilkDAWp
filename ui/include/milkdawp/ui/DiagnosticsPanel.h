// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "milkdawp/core/Diagnostics.h"
#include "milkdawp/ui/KeyboardNavigation.h"

namespace milkdawp::ui {

/// The lines the panel shows live: renderer, frame timing, beat, presets,
/// the shell's input and surface, and the newest `maxErrors` errors.
[[nodiscard]] juce::StringArray formatDiagnosticsLines(const core::DiagnosticsInfo& info, int maxErrors);

/// The full text "Copy diagnostics" puts on the clipboard (and the app's log
/// bundle writes): the shell, when it was collected, everything the panel
/// shows, and every recent error.
[[nodiscard]] juce::String formatDiagnosticsReport(const core::DiagnosticsInfo& info);

/// 5.9: the diagnostics panel both shells show over the picture (Settings /
/// View > Show diagnostics). Replaces the two shells' own overlay labels.
/// The shell calls `update()` from its UI timer with the engine's
/// `Visualizer::diagnostics()` plus its own fields; the panel only draws and
/// copies. Message thread only.
class DiagnosticsPanel : public juce::Component {
public:
  /// Errors drawn in the panel; the copied report has all of them.
  static constexpr int kVisibleErrors = 5;

  DiagnosticsPanel();

  void update(core::DiagnosticsInfo info);
  [[nodiscard]] const core::DiagnosticsInfo& info() const noexcept { return info_; }
  [[nodiscard]] const juce::StringArray& lines() const noexcept { return lines_; }

  /// Height that fits the current lines, for the shell's layout.
  [[nodiscard]] int preferredHeight() const noexcept;

  /// Puts formatDiagnosticsReport() on the clipboard (the Copy button).
  void copyToClipboard();

  std::function<void()> onCloseRequested;

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override { focusRing_.paint(g); }
  void resized() override;

  juce::Label titleLabel;
  juce::TextButton copyButton{"Copy diagnostics"};
  juce::TextButton closeButton{"Close"};

private:
  static constexpr int kLineHeight = 16;
  static constexpr int kHeaderHeight = 28;

  core::DiagnosticsInfo info_;
  juce::StringArray lines_;
  juce::Font font_;

  KeyboardFocusRing focusRing_{*this}; // 5.8

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiagnosticsPanel)
};

} // namespace milkdawp::ui
