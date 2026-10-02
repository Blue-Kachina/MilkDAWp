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
