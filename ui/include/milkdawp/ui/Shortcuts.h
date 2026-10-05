// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

namespace milkdawp::ui {

/// A logical action from §4.9's keyboard shortcut table.
enum class ShortcutAction : std::uint8_t {
  None,
  ToggleFullscreen,             // F11
  ExitFullscreenOrRevealDrawer, // Esc
  PreviousPreset,               // Left
  NextPreset,                   // Right
  ToggleLock,                   // L, or Space in the app shell
  ToggleShuffle,                // S
  ToggleDrawer,                 // H
  TogglePin,                    // P
  BrowsePresets,                // B: the preset picker (5.8)
  OpenSettingsMenu,             // M: the settings menu (5.8)
  ToggleDiagnostics,            // D: the diagnostics panel (5.8, 5.9)
};

/// Maps a key press to a logical action (§4.9), shared verbatim by every
/// window `milkdawp_ui` composes -- the primary window, the Output window,
/// and detached controls. Deliberately just a KeyPress -> Action mapping:
/// what `ToggleFullscreen` or `ExitFullscreenOrRevealDrawer` actually *does*
/// differs per window (an Output window fullscreens itself; the plugin
/// editor opens an Output window fullscreen instead, since a host-framed
/// editor can never fullscreen itself) -- that routing is each window's own
/// job, not this function's, which is why it stays a pure, JUCE-`KeyPress`-
/// in/`ShortcutAction`-out function rather than something that reaches into
/// window state.
///
/// `isAppShell` gates Space: every DAW binds Space to transport, so the
/// plugin must never claim it (§4.9) -- only the standalone app shell
/// should pass `isAppShell = true`.
///
/// The single-letter shortcuts (L, S, H, P, B, M, D) only fire unmodified, per §4.9's
/// "unmodified letters only" rule; F11/Esc/arrows/Space are unaffected by
/// modifiers, since the table places no such restriction on them.
///
/// Callers are responsible for the two preconditions §4.9 lists that this
/// function has no way to check itself: only call this while the
/// visualization or drawer has keyboard focus, and never while a text field
/// is being edited.
[[nodiscard]] ShortcutAction mapKeyPress(const juce::KeyPress& key, bool isAppShell) noexcept;

} // namespace milkdawp::ui
