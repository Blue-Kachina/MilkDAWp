// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace milkdawp::core {

/// A plain-data mirror of v1's `MilkDAWpState` ValueTree (§4.8), populated
/// by the plugin layer (which owns the actual `juce::ValueTree::readFromData`
/// call -- ValueTree is a JUCE type, so it never appears in milkdawp_core,
/// per §4.1). paramValues holds v1's raw APVTS values keyed by v1's
/// parameter id (see the "PARAM id/value" children JUCE's APVTS writes).
struct V1StateRecord {
  std::string version;
  std::string presetPath;
  std::string playlistFolderPath;
  int editorWidth = 0;
  int editorHeight = 0;
  std::map<std::string, float> paramValues;
};

/// A window's screen rectangle, in logical pixels. Empty (zero size) means
/// "never placed": the shell picks a default.
struct WindowBounds {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  [[nodiscard]] bool isEmpty() const noexcept { return width <= 0 || height <= 0; }
  bool operator==(const WindowBounds&) const = default;
};

/// The windows a session had open (Phase 3.12/3.13), so reopening a project
/// puts the Output window back on the capture display and the controls where
/// they were.
struct WindowLayout {
  bool outputWindowOpen = false;
  bool outputWindowFullscreen = false;
  /// Windowed bounds; fullscreen covers the display these fall on.
  WindowBounds outputWindowBounds;
  bool controlsFloating = false;
  WindowBounds controlsWindowBounds;
  /// Settings -> Output (M0 in layers_like_shrek.md): the pop-out button opens
  /// fullscreen when set.
  bool outputDefaultFullscreen = false;
  /// The display the Output window opens on, identified by that display's
  /// own bounds (JUCE has no stable monitor id). Empty means automatic. Kept
  /// even while that display is unplugged, so it is used again when it returns.
  WindowBounds outputTargetDisplay;
  /// Layers: the `instanceId` of the instance whose Output window this one's
  /// picture goes to. Empty means this instance has its own window. Kept even
  /// while that instance does not exist, so the link comes back with it.
  std::string outputTargetInstance;

  bool operator==(const WindowLayout&) const = default;
};

/// MilkDAWp 2's canonical state (§4.8 "StateSchema v2"). Preset references
/// are stored as both an absolute path and (once PresetLibrary exists,
/// Phase 2.5) a {libraryRoot, relativePath, contentHash} triple so a moved
/// preset folder can be relinked; Phase 1 only has the plain paths.
struct StateSchemaV2 {
  static constexpr int currentSchemaVersion = 2;

  int schemaVersion = currentSchemaVersion;
  std::string presetAbsolutePath;
  std::string playlistFolderPath;
  int editorWidth = 0;
  int editorHeight = 0;
  /// Layers: a stable identity for this plugin instance, so another instance
  /// can point at it across project reloads. Empty in states saved before
  /// Layers; the plugin makes one up and saves it from then on.
  std::string instanceId;
  /// What the user calls this instance in the target picker. Empty means "use
  /// the host's track name".
  std::string instanceLabel;
  /// 5.2: Transitions -> "Only tags" (Director::setTagFilter). Additive key.
  std::string tagFilter;
  WindowLayout windows; // additive keys: older v2 states load with everything closed
  std::map<std::string, float> paramValues; // keyed by ParameterModel's v2 ids
};

/// Maps a v1 session onto v2's schema (§4.8, §7 Phase 1.14): every v1
/// parameter carries its value forward via ParameterSpec::v1Alias; v2-only
/// parameters get ParameterModel's default, except presetSelectionPolicy,
/// which is derived from v1's boolean `shuffle` (true -> ShuffleNoRepeat,
/// false -> Sequential) so a migrated session's playback behaviour doesn't
/// silently change.
[[nodiscard]] StateSchemaV2 migrateFromV1(const V1StateRecord& v1);

/// Simple, deterministic, JUCE-free serialization for StateSchemaV2 (round-trip
/// tested per §4.8). One "key=value" pair per line; not intended to be the
/// plugin's actual on-disk/host-state format (Phase 3.2 wraps this, or
/// something compatible with it, in whatever juce::AudioProcessor::
/// getStateInformation needs) -- it exists so milkdawp_core's state logic is
/// testable without a JUCE dependency.
[[nodiscard]] std::string serializeStateSchemaV2(const StateSchemaV2& state);
[[nodiscard]] StateSchemaV2 deserializeStateSchemaV2(const std::string& text);

} // namespace milkdawp::core
