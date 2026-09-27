// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/ui/Shortcuts.h"

using namespace milkdawp::ui;

namespace {
juce::KeyPress plainKey(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys(), 0); }

juce::KeyPress modifiedKey(int keyCode, juce::ModifierKeys mods) { return juce::KeyPress(keyCode, mods, 0); }
} // namespace

TEST_CASE("mapKeyPress maps F11 to ToggleFullscreen regardless of shell", "[ui][Shortcuts]") {
  CHECK(mapKeyPress(plainKey(juce::KeyPress::F11Key), false) == ShortcutAction::ToggleFullscreen);
  CHECK(mapKeyPress(plainKey(juce::KeyPress::F11Key), true) == ShortcutAction::ToggleFullscreen);
}

TEST_CASE("mapKeyPress maps Esc to ExitFullscreenOrRevealDrawer", "[ui][Shortcuts]") {
  CHECK(mapKeyPress(plainKey(juce::KeyPress::escapeKey), false) == ShortcutAction::ExitFullscreenOrRevealDrawer);
}

TEST_CASE("mapKeyPress maps the arrow keys to preset navigation", "[ui][Shortcuts]") {
  CHECK(mapKeyPress(plainKey(juce::KeyPress::leftKey), false) == ShortcutAction::PreviousPreset);
  CHECK(mapKeyPress(plainKey(juce::KeyPress::rightKey), false) == ShortcutAction::NextPreset);
}

TEST_CASE("mapKeyPress maps unmodified L/S/H/P to their drawer actions", "[ui][Shortcuts]") {
  CHECK(mapKeyPress(plainKey('L'), false) == ShortcutAction::ToggleLock);
  CHECK(mapKeyPress(plainKey('S'), false) == ShortcutAction::ToggleShuffle);
  CHECK(mapKeyPress(plainKey('H'), false) == ShortcutAction::ToggleDrawer);
  CHECK(mapKeyPress(plainKey('P'), false) == ShortcutAction::TogglePin);
}

TEST_CASE("mapKeyPress ignores L/S/H/P when a modifier is held", "[ui][Shortcuts]") {
  CHECK(mapKeyPress(modifiedKey('L', juce::ModifierKeys::commandModifier), false) == ShortcutAction::None);
  CHECK(mapKeyPress(modifiedKey('S', juce::ModifierKeys::ctrlModifier), false) == ShortcutAction::None);
  CHECK(mapKeyPress(modifiedKey('H', juce::ModifierKeys::altModifier), false) == ShortcutAction::None);
}

TEST_CASE("mapKeyPress claims Space for ToggleLock only in the app shell", "[ui][Shortcuts]") {
  CHECK(mapKeyPress(plainKey(juce::KeyPress::spaceKey), true) == ShortcutAction::ToggleLock);
  CHECK(mapKeyPress(plainKey(juce::KeyPress::spaceKey), false) == ShortcutAction::None);
}

TEST_CASE("mapKeyPress returns None for an unrelated key", "[ui][Shortcuts]") {
  CHECK(mapKeyPress(plainKey('Q'), false) == ShortcutAction::None);
  CHECK(mapKeyPress(plainKey('Q'), true) == ShortcutAction::None);
}

TEST_CASE("mapKeyPress accepts lower-case letter codes", "[ui][Shortcuts]") {
  // Windows keys injected without a scan code reach JUCE as the lower-case
  // character rather than the upper-case key code.
  CHECK(mapKeyPress(plainKey('l'), false) == ShortcutAction::ToggleLock);
  CHECK(mapKeyPress(plainKey('s'), false) == ShortcutAction::ToggleShuffle);
  CHECK(mapKeyPress(plainKey('h'), false) == ShortcutAction::ToggleDrawer);
  CHECK(mapKeyPress(plainKey('p'), false) == ShortcutAction::TogglePin);
  CHECK(mapKeyPress(modifiedKey('l', juce::ModifierKeys::ctrlModifier), false) == ShortcutAction::None);
}
