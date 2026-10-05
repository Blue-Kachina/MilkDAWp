// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <set>

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/core/ParameterModel.h"

using namespace milkdawp::core;

TEST_CASE("allParameters has no duplicate ids", "[core][ParameterModel]") {
  const auto& params = allParameters();
  std::set<std::string> ids;
  for (const auto& p : params) {
    CHECK(ids.insert(p.id).second);
  }
}

TEST_CASE("allParameters has no two parameters sharing a v1 alias", "[core][ParameterModel]") {
  const auto& params = allParameters();
  std::set<std::string> aliases;
  for (const auto& p : params) {
    if (!p.v1Alias.empty()) {
      CHECK(aliases.insert(p.v1Alias).second);
    }
  }
}

TEST_CASE("every v1 parameter carries forward with a matching alias", "[core][ParameterModel]") {
  const auto& params = allParameters();
  const std::vector<std::string> v1Ids{
      "beatSensitivity",         "transitionDurationSeconds", "shuffle",
      "lockCurrentPreset",       "presetIndex",               "triggerNext",
      "triggerPrev",             "transitionJitterEnabled",   "transitionDurationMin",
      "transitionDurationMax",   "hardCutEnabled",            "hardCutSensitivity",
      "softCutDuration",         "hardCutDuration",           "qualityOverride"};
  for (const auto& id : v1Ids) {
    INFO("v1 parameter: " << id);
    CHECK(findByV1Alias(params, id) != nullptr);
  }
}

TEST_CASE("findParameter finds an existing id and returns null for an unknown one",
          "[core][ParameterModel]") {
  const auto& params = allParameters();
  CHECK(findParameter(params, "beatSensitivity") != nullptr);
  CHECK(findParameter(params, "doesNotExist") == nullptr);
}

TEST_CASE("v2-only parameters have an empty v1 alias", "[core][ParameterModel]") {
  const auto& params = allParameters();
  for (const auto& newId : {"transitionMode", "transitionBars", "presetSelectionPolicy", "energyThreshold",
                           "useHostTempo", "layerOpacity", "layerBlend", "layerMute", "layerOrder"}) {
    auto* p = findParameter(params, newId);
    REQUIRE(p != nullptr);
    CHECK(p->v1Alias.empty());
  }
}

TEST_CASE("the 1.0 parameters keep their indexes", "[core][ParameterModel]") {
  // REAPER (and other hosts) store automation envelopes by parameter index, so
  // new parameters are appended and the 1.0 list never moves (ADR-0011).
  const std::vector<std::string> v1_0{
      "beatSensitivity",      "transitionDurationSeconds", "shuffle",           "lockCurrentPreset",
      "presetIndex",          "triggerNext",               "triggerPrev",       "transitionJitterEnabled",
      "transitionDurationMin", "transitionDurationMax",    "hardCutEnabled",    "hardCutSensitivity",
      "softCutDuration",      "hardCutDuration",           "qualityOverride",   "transitionMode",
      "transitionBars",       "presetSelectionPolicy",     "energyThreshold",   "useHostTempo",
      "layerOpacity",         "layerBlend",                "layerMute",         "layerOrder",
      "transitionGridSync",   "transitionGridOffset"};
  const auto& params = allParameters();
  REQUIRE(params.size() >= v1_0.size());
  for (std::size_t i = 0; i < v1_0.size(); ++i) {
    INFO("index " << i);
    CHECK(params[i].id == v1_0[i]);
    CHECK(params[i].group.empty()); // grouping them now would change nothing for hosts, but isn't needed
  }
}

TEST_CASE("parameter groups are known and contiguous", "[core][ParameterModel]") {
  const auto& params = allParameters();
  std::set<std::string> known;
  for (const auto& group : parameterGroups()) {
    CHECK(known.insert(group.id).second);
  }
  std::set<std::string> finished;
  std::string current;
  for (const auto& p : params) {
    INFO("parameter " << p.id);
    if (!p.group.empty()) {
      CHECK(known.contains(p.group));
    }
    if (p.group != current) {
      if (!current.empty()) {
        finished.insert(current);
      }
      // A group that already ended must not start again: JUCE would build two groups.
      CHECK_FALSE(finished.contains(p.group));
      current = p.group;
    }
  }
}

TEST_CASE("Phase 8 adds 16 Visual globals, 8 Macros, Lock Macros and the gate", "[core][ParameterModel]") {
  const auto& params = allParameters();
  int visual = 0;
  int macros = 0;
  int gate = 0;
  for (const auto& p : params) {
    visual += p.group == "visual" ? 1 : 0;
    macros += p.group == "macros" ? 1 : 0;
    gate += p.group == "layerGate" ? 1 : 0;
  }
  CHECK(visual == 16);
  CHECK(macros == kMacroCount + 1);
  CHECK(gate == 3);
  for (int slot = 0; slot < kMacroCount; ++slot) {
    const auto* macro = findParameter(params, macroParameterId(slot));
    REQUIRE(macro != nullptr);
    CHECK(macro->group == "macros");
  }
  const auto* lock = findParameter(params, "lockMacros");
  REQUIRE(lock != nullptr);
  CHECK(lock->defaultValue == 0.0f); // Off by default (§6.3)
  const auto* threshold = findParameter(params, "layerGateThreshold");
  REQUIRE(threshold != nullptr);
  CHECK(threshold->defaultValue == -80.0f);
  const auto* release = findParameter(params, "layerGateRelease");
  REQUIRE(release != nullptr);
  CHECK(release->defaultValue == 80.0f);
}

TEST_CASE("skew centres sit inside their ranges", "[core][ParameterModel]") {
  for (const auto& p : allParameters()) {
    if (p.skewCentre != 0.0f) {
      INFO("parameter " << p.id);
      CHECK(p.type == ParameterType::Float);
      CHECK(p.skewCentre > p.minValue);
      CHECK(p.skewCentre < p.maxValue);
    }
  }
}

TEST_CASE("renderParameterDocsMarkdown produces one row per parameter plus a header",
          "[core][ParameterModel]") {
  const auto& params = allParameters();
  auto markdown = renderParameterDocsMarkdown(params);

  CHECK(markdown.find("| ID | Name |") != std::string::npos);

  std::size_t rowCount = 0;
  std::size_t pos = 0;
  while ((pos = markdown.find('\n', pos)) != std::string::npos) {
    ++rowCount;
    ++pos;
  }
  // header line + separator line + one line per parameter
  CHECK(rowCount == params.size() + 2);
}
