// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AppPreferences.h"

#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::app {

namespace {

// Keys are part of the preferences file format: add new ones, never rename.
constexpr const char* kPresetFolder = "presetFolder";
constexpr const char* kCurrentPreset = "currentPreset";
constexpr const char* kAudioDevice = "audioDevice";
constexpr const char* kUseSystemAudio = "useSystemAudio";
constexpr const char* kMainBounds = "mainWindowBounds";
constexpr const char* kMainFullscreen = "mainWindowFullscreen";
constexpr const char* kOutputOpen = "outputWindowOpen";
constexpr const char* kOutputBounds = "outputWindowBounds";
constexpr const char* kOutputFullscreen = "outputWindowFullscreen";
constexpr const char* kOutputDefaultFullscreen = "outputDefaultFullscreen";
constexpr const char* kOutputTargetDisplay = "outputTargetDisplay";
constexpr const char* kControlsFloating = "controlsFloating";
constexpr const char* kControlsBounds = "controlsWindowBounds";
constexpr const char* kDrawerPinned = "drawerPinned";
constexpr const char* kShowDiagnostics = "showDiagnostics";
constexpr const char* kLogging = "logging";
constexpr const char* kFavourites = "favouritePresets";
constexpr const char* kRecentlyPlayed = "recentlyPlayedPresets";
constexpr const char* kMidiMappings = "midiMappings";
constexpr const char* kTagFilter = "tagFilter";
constexpr const char* kMediaSource = "mediaSource";
constexpr const char* kMediaBlend = "mediaBlend";
constexpr const char* kParameterPrefix = "param.";

// One path per line; paths never contain newlines.
juce::StringArray readList(const juce::PropertySet& properties, const char* key) {
  const auto text = properties.getValue(key);
  return text.isEmpty() ? juce::StringArray() : juce::StringArray::fromLines(text);
}

void writeList(juce::PropertySet& properties, const char* key, const juce::StringArray& list) {
  if (list.isEmpty()) {
    properties.removeValue(key);
  } else {
    properties.setValue(key, list.joinIntoString("\n"));
  }
}

juce::Rectangle<int> readBounds(const juce::PropertySet& properties, const char* key) {
  const auto text = properties.getValue(key);
  return text.isEmpty() ? juce::Rectangle<int>() : juce::Rectangle<int>::fromString(text);
}

void writeBounds(juce::PropertySet& properties, const char* key, juce::Rectangle<int> bounds) {
  if (bounds.isEmpty()) {
    properties.removeValue(key);
  } else {
    properties.setValue(key, bounds.toString());
  }
}

} // namespace

AppState loadAppState(const juce::PropertySet& properties) {
  AppState state;
  for (const auto& spec : core::allParameters()) {
    float* field = engine::parameterField(state.parameters, spec.id);
    const auto key = kParameterPrefix + juce::String(spec.id);
    if (field == nullptr || !properties.containsKey(key)) {
      continue;
    }
    // Out-of-range values (a hand-edited file, an older range) are clamped,
    // not trusted.
    const auto value = static_cast<float>(properties.getDoubleValue(key, spec.defaultValue));
    *field = spec.type == core::ParameterType::Bool ? (value > 0.5f ? 1.0f : 0.0f)
                                                    : juce::jlimit(spec.minValue, spec.maxValue, value);
  }
  state.presetFolder = properties.getValue(kPresetFolder);
  state.currentPresetPath = properties.getValue(kCurrentPreset);
  state.audioDeviceState = properties.getValue(kAudioDevice);
  state.useSystemAudio = properties.getBoolValue(kUseSystemAudio, false);
  state.favouritePresets = readList(properties, kFavourites);
  state.recentlyPlayedPresets = readList(properties, kRecentlyPlayed);
  state.midiMappings = properties.getValue(kMidiMappings);
  state.tagFilter = properties.getValue(kTagFilter);
  state.mediaSourcePath = properties.getValue(kMediaSource);
  state.mediaBlend = properties.getIntValue(kMediaBlend, 0);
  state.mainWindowBounds = readBounds(properties, kMainBounds);
  state.mainWindowFullscreen = properties.getBoolValue(kMainFullscreen, false);
  state.outputWindowOpen = properties.getBoolValue(kOutputOpen, false);
  state.outputWindowBounds = readBounds(properties, kOutputBounds);
  state.outputWindowFullscreen = properties.getBoolValue(kOutputFullscreen, false);
  state.outputDefaultFullscreen = properties.getBoolValue(kOutputDefaultFullscreen, false);
  state.outputTargetDisplay = readBounds(properties, kOutputTargetDisplay);
  state.controlsFloating = properties.getBoolValue(kControlsFloating, false);
  state.controlsWindowBounds = readBounds(properties, kControlsBounds);
  state.drawerPinned = properties.getBoolValue(kDrawerPinned, true);
  state.showDiagnostics = properties.getBoolValue(kShowDiagnostics, false);
  state.loggingEnabled = properties.getBoolValue(kLogging, false);
  return state;
}

void saveAppState(const AppState& state, juce::PropertySet& properties) {
  for (const auto& spec : core::allParameters()) {
    if (const float* field = engine::parameterField(state.parameters, spec.id)) {
      properties.setValue(kParameterPrefix + juce::String(spec.id), static_cast<double>(*field));
    }
  }
  properties.setValue(kPresetFolder, state.presetFolder);
  properties.setValue(kCurrentPreset, state.currentPresetPath);
  properties.setValue(kAudioDevice, state.audioDeviceState);
  properties.setValue(kUseSystemAudio, state.useSystemAudio);
  writeList(properties, kFavourites, state.favouritePresets);
  writeList(properties, kRecentlyPlayed, state.recentlyPlayedPresets);
  properties.setValue(kMidiMappings, state.midiMappings);
  properties.setValue(kTagFilter, state.tagFilter);
  properties.setValue(kMediaSource, state.mediaSourcePath);
  properties.setValue(kMediaBlend, state.mediaBlend);
  writeBounds(properties, kMainBounds, state.mainWindowBounds);
  properties.setValue(kMainFullscreen, state.mainWindowFullscreen);
  properties.setValue(kOutputOpen, state.outputWindowOpen);
  writeBounds(properties, kOutputBounds, state.outputWindowBounds);
  properties.setValue(kOutputFullscreen, state.outputWindowFullscreen);
  properties.setValue(kOutputDefaultFullscreen, state.outputDefaultFullscreen);
  writeBounds(properties, kOutputTargetDisplay, state.outputTargetDisplay);
  properties.setValue(kControlsFloating, state.controlsFloating);
  writeBounds(properties, kControlsBounds, state.controlsWindowBounds);
  properties.setValue(kDrawerPinned, state.drawerPinned);
  properties.setValue(kShowDiagnostics, state.showDiagnostics);
  properties.setValue(kLogging, state.loggingEnabled);
}

} // namespace milkdawp::app
