// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace milkdawp::core {

enum class ParameterType { Float, Bool, Int, Choice };

/// One parameter's canonical definition (Phase 1.13). Shared by the plugin
/// (APVTS layout), the app (preferences/MIDI-learn targets), and
/// MigrateFromV1 (via v1Alias). This is the single source of truth: nothing
/// else in the codebase should hardcode a parameter's id, range, or default.
struct ParameterSpec {
  std::string id;   // v2 canonical id, stable across releases (never rename without an ADR)
  std::string displayName;
  ParameterType type;
  float minValue = 0.0f;   // Float/Int only
  float maxValue = 0.0f;   // Float/Int only
  float defaultValue = 0.0f;
  bool automatable = true;
  std::string v1Alias;                // v1's parameter id, or "" if this parameter is new in v2
  std::vector<std::string> choices;   // Choice only, in index order (defaultValue is the index)
  /// The host's parameter group this belongs to ("" = top level). Groups must
  /// be contiguous runs in `allParameters()`: hosts such as REAPER store
  /// automation by parameter index, so grouping must never reorder (ADR-0011).
  std::string group;
  /// Float only: the value at the middle of the knob's travel, for ranges
  /// that want more resolution at one end (0 = linear).
  float skewCentre = 0.0f;
};

/// The parameter groups `ParameterSpec::group` names, as (id, display name).
struct ParameterGroup {
  std::string id;
  std::string displayName;
};
[[nodiscard]] const std::vector<ParameterGroup>& parameterGroups();

/// The number of Macro slots (Phase 8.1): `macro1` .. `macro8`.
inline constexpr int kMacroCount = 8;

/// The full parameter surface: v1's 15 parameters carried forward unchanged
/// (§2.9: "a good 1.0 surface... basis for state migration"), the v2-only
/// transition-scheduling and Layers parameters, then Phase 8's Visual globals,
/// Macros and layer gate (ADR-0011). New parameters are only ever appended.
[[nodiscard]] const std::vector<ParameterSpec>& allParameters();

/// "macro1" .. "macro8" for slot 0 .. 7.
[[nodiscard]] std::string macroParameterId(int slot);

/// Look up a parameter by its v2 id. Returns nullptr if not found.
[[nodiscard]] const ParameterSpec* findParameter(const std::vector<ParameterSpec>& params,
                                                  const std::string& id);

/// Look up the (single) parameter whose v1Alias matches `v1Id`. Returns
/// nullptr if no v2 parameter carries that v1 alias.
[[nodiscard]] const ParameterSpec* findByV1Alias(const std::vector<ParameterSpec>& params,
                                                  const std::string& v1Id);

/// Renders `params` as a GitHub-flavoured Markdown table (id, name, type,
/// range/choices, default, automatable, v1 alias) -- the "generated docs
/// table" in Phase 1.13. See docs/parameters.md for the committed output.
[[nodiscard]] std::string renderParameterDocsMarkdown(const std::vector<ParameterSpec>& params);

} // namespace milkdawp::core
