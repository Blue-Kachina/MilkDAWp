// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/ParameterModel.h"
#include "milkdawp/core/StateSchema.h"

using namespace milkdawp::core;

namespace {
/// A V1StateRecord matching v1 0.7.5's real defaults (see
/// PluginProcessor.cpp's createParameterLayout(), fetched from
/// github.com/Blue-Kachina/MilkDAWp for this migration).
V1StateRecord makeV1Defaults() {
  V1StateRecord v1;
  v1.version = "0.7.5";
  v1.presetPath = "C:/Presets/favorite.milk";
  v1.playlistFolderPath = "C:/Presets";
  v1.editorWidth = 800;
  v1.editorHeight = 600;
  v1.paramValues = {
      {"beatSensitivity", 1.0f},
      {"transitionDurationSeconds", 5.0f},
      {"shuffle", 0.0f},
      {"lockCurrentPreset", 0.0f},
      {"presetIndex", 3.0f},
      {"triggerNext", 0.0f},
      {"triggerPrev", 0.0f},
      {"transitionJitterEnabled", 0.0f},
      {"transitionDurationMin", 3.0f},
      {"transitionDurationMax", 15.0f},
      {"hardCutEnabled", 0.0f},
      {"hardCutSensitivity", 0.5f},
      {"softCutDuration", 3.0f},
      {"hardCutDuration", 5.0f},
      {"qualityOverride", 0.0f},
  };
  return v1;
}
} // namespace

TEST_CASE("migrateFromV1 carries every v1 parameter forward unchanged", "[core][StateSchema]") {
  auto v1 = makeV1Defaults();
  auto v2 = migrateFromV1(v1);

  CHECK(v2.schemaVersion == StateSchemaV2::currentSchemaVersion);
  CHECK(v2.presetAbsolutePath == "C:/Presets/favorite.milk");
  CHECK(v2.playlistFolderPath == "C:/Presets");
  CHECK(v2.editorWidth == 800);
  CHECK(v2.editorHeight == 600);

  for (const auto& [id, value] : v1.paramValues) {
    INFO("parameter: " << id);
    REQUIRE(v2.paramValues.count(id) == 1);
    CHECK(v2.paramValues.at(id) == value);
  }
}

TEST_CASE("migrateFromV1 gives new v2-only parameters their ParameterModel default",
          "[core][StateSchema]") {
  auto v2 = migrateFromV1(makeV1Defaults());

  const auto& params = allParameters();
  auto* mode = findParameter(params, "transitionMode");
  auto* bars = findParameter(params, "transitionBars");
  REQUIRE(mode != nullptr);
  REQUIRE(bars != nullptr);

  CHECK(v2.paramValues.at("transitionMode") == mode->defaultValue);
  CHECK(v2.paramValues.at("transitionBars") == bars->defaultValue);
}

TEST_CASE("migrateFromV1 derives presetSelectionPolicy from v1's shuffle flag",
          "[core][StateSchema]") {
  auto v1Off = makeV1Defaults();
  v1Off.paramValues["shuffle"] = 0.0f;
  CHECK(migrateFromV1(v1Off).paramValues.at("presetSelectionPolicy") == 0.0f); // Sequential

  auto v1On = makeV1Defaults();
  v1On.paramValues["shuffle"] = 1.0f;
  CHECK(migrateFromV1(v1On).paramValues.at("presetSelectionPolicy") == 1.0f); // ShuffleNoRepeat
}

TEST_CASE("migrateFromV1 falls back to the v2 default for a missing v1 parameter",
          "[core][StateSchema]") {
  auto v1 = makeV1Defaults();
  v1.paramValues.erase("hardCutSensitivity"); // simulate an incomplete/corrupt v1 state

  auto v2 = migrateFromV1(v1);

  auto* spec = findParameter(allParameters(), "hardCutSensitivity");
  REQUIRE(spec != nullptr);
  CHECK(v2.paramValues.at("hardCutSensitivity") == spec->defaultValue);
}

TEST_CASE("StateSchemaV2 serialize/deserialize round-trips exactly", "[core][StateSchema]") {
  StateSchemaV2 original;
  original.schemaVersion = 2;
  original.presetAbsolutePath = "/library/preset.milk";
  original.playlistFolderPath = "/library";
  original.editorWidth = 1024;
  original.editorHeight = 768;
  original.paramValues = {{"beatSensitivity", 1.25f}, {"presetIndex", 7.0f}, {"shuffle", 1.0f}};

  auto text = serializeStateSchemaV2(original);
  auto roundTripped = deserializeStateSchemaV2(text);

  CHECK(roundTripped.schemaVersion == original.schemaVersion);
  CHECK(roundTripped.presetAbsolutePath == original.presetAbsolutePath);
  CHECK(roundTripped.playlistFolderPath == original.playlistFolderPath);
  CHECK(roundTripped.editorWidth == original.editorWidth);
  CHECK(roundTripped.editorHeight == original.editorHeight);
  REQUIRE(roundTripped.paramValues.size() == original.paramValues.size());
  for (const auto& [id, value] : original.paramValues) {
    REQUIRE(roundTripped.paramValues.count(id) == 1);
    CHECK(roundTripped.paramValues.at(id) == value);
  }
}

TEST_CASE("StateSchemaV2 round-trips a full migrated v1 session", "[core][StateSchema]") {
  auto migrated = migrateFromV1(makeV1Defaults());
  auto roundTripped = deserializeStateSchemaV2(serializeStateSchemaV2(migrated));

  CHECK(roundTripped.presetAbsolutePath == migrated.presetAbsolutePath);
  REQUIRE(roundTripped.paramValues.size() == migrated.paramValues.size());
  for (const auto& [id, value] : migrated.paramValues) {
    CHECK(roundTripped.paramValues.at(id) == value);
  }
}

TEST_CASE("StateSchemaV2 round-trips the window layout", "[core][StateSchema]") {
  StateSchemaV2 original;
  original.windows.outputWindowOpen = true;
  original.windows.outputWindowFullscreen = true;
  original.windows.outputWindowBounds = {-1920, 40, 1280, 720}; // a display left of the primary
  original.windows.controlsFloating = true;
  original.windows.controlsWindowBounds = {100, 900, 640, 76};

  const auto roundTripped = deserializeStateSchemaV2(serializeStateSchemaV2(original));
  CHECK(roundTripped.windows == original.windows);
}

TEST_CASE("StateSchemaV2 round-trips the Output settings", "[core][StateSchema]") {
  StateSchemaV2 original;
  original.windows.outputDefaultFullscreen = true;
  original.windows.outputTargetDisplay = {1920, -120, 2560, 1440}; // a display right of and above the primary

  const auto roundTripped = deserializeStateSchemaV2(serializeStateSchemaV2(original));
  CHECK(roundTripped.windows.outputDefaultFullscreen);
  CHECK(roundTripped.windows.outputTargetDisplay == original.windows.outputTargetDisplay);
}

TEST_CASE("StateSchemaV2 round-trips the instance identity and Output target (Layers)", "[core][StateSchema]") {
  StateSchemaV2 original;
  original.instanceId = "7f1c2f0e-6a64-4d1e-8a5e-0f1b2c3d4e5f";
  original.instanceLabel = "Kick drum";
  original.windows.outputTargetInstance = "0a9b8c7d-1111-2222-3333-444455556666";

  const auto roundTripped = deserializeStateSchemaV2(serializeStateSchemaV2(original));
  CHECK(roundTripped.instanceId == original.instanceId);
  CHECK(roundTripped.instanceLabel == original.instanceLabel);
  CHECK(roundTripped.windows.outputTargetInstance == original.windows.outputTargetInstance);
}

TEST_CASE("StateSchemaV2 round-trips the tag filter, and older states have none (5.2)", "[core][StateSchema]") {
  StateSchemaV2 original;
  original.tagFilter = "calm, dark";
  CHECK(deserializeStateSchemaV2(serializeStateSchemaV2(original)).tagFilter == "calm, dark");
  CHECK(deserializeStateSchemaV2("schemaVersion=2\n").tagFilter.empty());
}

TEST_CASE("StateSchemaV2 keeps a label with a line break from corrupting the lines after it", "[core][StateSchema]") {
  StateSchemaV2 original;
  original.instanceLabel = "Lead\nsynth\r\nbus";
  original.windows.outputDefaultFullscreen = true;
  original.editorWidth = 640;

  const auto roundTripped = deserializeStateSchemaV2(serializeStateSchemaV2(original));
  CHECK(roundTripped.instanceLabel == "Lead synth  bus");
  CHECK(roundTripped.windows.outputDefaultFullscreen); // keys after the label still parse
  CHECK(roundTripped.editorWidth == 640);
}

TEST_CASE("StateSchemaV2 from before Layers has no identity and no target", "[core][StateSchema]") {
  const auto state = deserializeStateSchemaV2("schemaVersion=2\neditorWidth=800\n");
  CHECK(state.instanceId.empty());
  CHECK(state.instanceLabel.empty());
  CHECK(state.windows.outputTargetInstance.empty());
}

TEST_CASE("StateSchemaV2 without Output settings keys defaults to windowed on an automatic display",
          "[core][StateSchema]") {
  const auto state = deserializeStateSchemaV2("schemaVersion=2\noutputWindowOpen=1\n");
  CHECK_FALSE(state.windows.outputDefaultFullscreen);
  CHECK(state.windows.outputTargetDisplay.isEmpty());
}

TEST_CASE("StateSchemaV2 without window keys loads with every window closed", "[core][StateSchema]") {
  const auto state = deserializeStateSchemaV2("schemaVersion=2\neditorWidth=800\n");
  CHECK(state.editorWidth == 800);
  CHECK(state.windows == WindowLayout{});
  CHECK(state.windows.outputWindowBounds.isEmpty());
}

TEST_CASE("StateSchemaV2 skips malformed lines instead of throwing", "[core][StateSchema]") {
  const std::string text = "schemaVersion=two\n"
                           "editorWidth=12abc\n"
                           "editorHeight=600\r\n"
                           "outputWindowBounds=1,2,3\n"
                           "controlsWindowBounds=10,20,300,40\n"
                           "param.beatSensitivity=nan\n"
                           "param.shuffle=1\n"
                           "param.presetIndex=\n"
                           "garbage without equals\n";
  StateSchemaV2 state;
  REQUIRE_NOTHROW(state = deserializeStateSchemaV2(text));
  CHECK(state.schemaVersion == StateSchemaV2::currentSchemaVersion);
  CHECK(state.editorWidth == 0);
  CHECK(state.editorHeight == 600); // CRLF tolerated
  CHECK(state.windows.outputWindowBounds.isEmpty());
  CHECK(state.windows.controlsWindowBounds == WindowBounds{10, 20, 300, 40});
  CHECK(state.paramValues.count("beatSensitivity") == 0);
  CHECK(state.paramValues.count("presetIndex") == 0);
  CHECK(state.paramValues.at("shuffle") == 1.0f);
}
