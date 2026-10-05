// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "AppPreferences.h"

using namespace milkdawp;

TEST_CASE("An empty preferences file gives the defaults", "[app][preferences]") {
  const juce::PropertySet empty;
  const auto state = app::loadAppState(empty);
  const engine::ParameterValues defaults;
  CHECK(state.parameters.transitionMode == defaults.transitionMode);
  CHECK(state.parameters.transitionBars == defaults.transitionBars);
  CHECK(state.presetFolder.isEmpty());
  CHECK(state.mainWindowBounds.isEmpty());
  CHECK(state.drawerPinned);
  CHECK_FALSE(state.outputWindowOpen);
  CHECK_FALSE(state.loggingEnabled);
  CHECK_FALSE(state.useSystemAudio);
  CHECK(state.favouritePresets.isEmpty());
  CHECK(state.recentlyPlayedPresets.isEmpty());
  CHECK(state.midiMappings.isEmpty());
}

TEST_CASE("App preferences round-trip through a PropertySet", "[app][preferences]") {
  app::AppState state;
  state.parameters.transitionMode = 4.0f;
  state.parameters.transitionBars = 8.0f;
  state.parameters.softCutDuration = 1.5f;
  state.parameters.shuffle = 1.0f;
  state.parameters.qualityOverride = 2.0f;
  state.presetFolder = "C:/presets";
  state.currentPresetPath = "C:/presets/a/b.milk";
  state.audioDeviceState = "<DEVICESETUP deviceType=\"Windows Audio\"/>";
  state.useSystemAudio = true;
  state.favouritePresets = {"C:/presets/a/b.milk", "C:/presets/c/d.milk"};
  state.recentlyPlayedPresets = {"C:/presets/c/d.milk", "C:/presets/a/b.milk"};
  state.midiMappings = "shuffle=0,1,21\nlockCurrentPreset=1,3,60";
  state.tagFilter = "calm, dark";
  state.mainWindowBounds = {10, 20, 800, 450};
  state.mainWindowFullscreen = true;
  state.outputWindowOpen = true;
  state.outputWindowBounds = {1920, 0, 1280, 720};
  state.outputWindowFullscreen = true;
  state.outputDefaultFullscreen = true;
  state.outputTargetDisplay = {1920, -120, 2560, 1440};
  state.controlsFloating = true;
  state.controlsWindowBounds = {100, 900, 700, 72};
  state.drawerPinned = false;
  state.showDiagnostics = true;
  state.loggingEnabled = true;

  juce::PropertySet properties;
  app::saveAppState(state, properties);
  const auto loaded = app::loadAppState(properties);

  CHECK(loaded.parameters.transitionMode == 4.0f);
  CHECK(loaded.parameters.transitionBars == 8.0f);
  CHECK(loaded.parameters.softCutDuration == 1.5f);
  CHECK(loaded.parameters.shuffle == 1.0f);
  CHECK(loaded.parameters.qualityOverride == 2.0f);
  CHECK(loaded.presetFolder == state.presetFolder);
  CHECK(loaded.currentPresetPath == state.currentPresetPath);
  CHECK(loaded.audioDeviceState == state.audioDeviceState);
  CHECK(loaded.useSystemAudio);
  CHECK(loaded.favouritePresets == state.favouritePresets);
  CHECK(loaded.recentlyPlayedPresets == state.recentlyPlayedPresets);
  CHECK(loaded.midiMappings == state.midiMappings);
  CHECK(loaded.tagFilter == state.tagFilter);
  CHECK(loaded.mainWindowBounds == state.mainWindowBounds);
  CHECK(loaded.mainWindowFullscreen);
  CHECK(loaded.outputWindowOpen);
  CHECK(loaded.outputWindowBounds == state.outputWindowBounds);
  CHECK(loaded.outputWindowFullscreen);
  CHECK(loaded.outputDefaultFullscreen);
  CHECK(loaded.outputTargetDisplay == state.outputTargetDisplay);
  CHECK(loaded.controlsFloating);
  CHECK(loaded.controlsWindowBounds == state.controlsWindowBounds);
  CHECK_FALSE(loaded.drawerPinned);
  CHECK(loaded.showDiagnostics);
  CHECK(loaded.loggingEnabled);
}

TEST_CASE("Out-of-range saved parameters are clamped, not trusted", "[app][preferences]") {
  juce::PropertySet properties;
  properties.setValue("param.transitionBars", 999.0);
  properties.setValue("param.transitionMode", -3.0);
  properties.setValue("param.shuffle", 0.7);
  properties.setValue("param.softCutDuration", "not a number");
  const auto state = app::loadAppState(properties);
  CHECK(state.parameters.transitionBars == 16.0f);
  CHECK(state.parameters.transitionMode == 0.0f);
  CHECK(state.parameters.shuffle == 1.0f);
  CHECK(state.parameters.softCutDuration >= 0.5f); // clamped to the model's range
}

TEST_CASE("Emptied window bounds are removed rather than saved as zero", "[app][preferences]") {
  juce::PropertySet properties;
  app::AppState state;
  state.mainWindowBounds = {1, 2, 300, 200};
  app::saveAppState(state, properties);
  CHECK(properties.containsKey("mainWindowBounds"));
  state.mainWindowBounds = {};
  app::saveAppState(state, properties);
  CHECK_FALSE(properties.containsKey("mainWindowBounds"));
}
