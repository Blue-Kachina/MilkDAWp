// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include "milkdawp/engine/ControlMapping.h"

namespace milkdawp::app {

/// Everything the app remembers between runs (4.3). A plain value the shell
/// reads at startup and writes back as things change; `loadAppState` and
/// `saveAppState` convert it to and from a `juce::PropertySet` (the app's
/// `PropertiesFile`), so the mapping is unit-tested without touching disk.
///
/// Desktop-only keys (window geometry, the Output window's display) are
/// simply unused by a shell that has no such windows (ADR-0010).
struct AppState {
  /// Every engine parameter, in `core::ParameterModel` units. Includes the
  /// quality choice (`qualityOverride`) and the transition settings.
  engine::ParameterValues parameters;

  juce::String presetFolder;      // the library root; empty until chosen
  juce::String currentPresetPath; // reopened in that folder when it still exists
  juce::String audioDeviceState;  // AudioDeviceManager::createStateXml(), as text
  bool useSystemAudio = false;    // §4.7: system-audio loopback instead of a device

  /// §4.5's browser: absolute preset paths, favourites unordered, recently
  /// played most-recent-first and bounded (`kMaxRecentlyPlayed`).
  juce::StringArray favouritePresets;
  juce::StringArray recentlyPlayedPresets;
  static constexpr int kMaxRecentlyPlayed = 20;

  /// §4.4: MidiLearn::stateString() / restoreFromState().
  juce::String midiMappings;

  juce::Rectangle<int> mainWindowBounds; // empty: centre a default size
  bool mainWindowFullscreen = false;

  bool outputWindowOpen = false;
  juce::Rectangle<int> outputWindowBounds; // also picks the display it opens on
  bool outputWindowFullscreen = false;
  /// Settings -> Output: the Output window opens fullscreen from the drawer's
  /// button, and on this display (identified by its own bounds; empty = automatic).
  bool outputDefaultFullscreen = false;
  juce::Rectangle<int> outputTargetDisplay;

  bool controlsFloating = false;
  juce::Rectangle<int> controlsWindowBounds;

  bool drawerPinned = true;
  bool showDiagnostics = false;
  bool loggingEnabled = false;
};

[[nodiscard]] AppState loadAppState(const juce::PropertySet& properties);
void saveAppState(const AppState& state, juce::PropertySet& properties);

} // namespace milkdawp::app
