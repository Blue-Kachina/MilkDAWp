// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <optional>

#include "milkdawp/core/ParameterModel.h"

namespace milkdawp::core {

/// What a preset says about Macro 1-8 (Phase 8.1, exploration doc §6.3): the
/// default for each slot it uses, nullopt for slots it leaves unused. A plain
/// `.milk` uses none; `.milkdawp` presets (Stage B) declare theirs.
using MacroDefaults = std::array<std::optional<float>, kMacroCount>;

/// The value each Macro moves to when the preset changes, or nullopt to leave
/// it where it is. Lock Macros on: every Macro keeps its value (the lane
/// carries on, and the new preset reads it with its own meaning). Off: each
/// moves to the new preset's default, and unused slots go back to 0. The shell
/// applies these on the message thread as normal parameter gestures, so a host
/// sees them like a mouse move (and an automation lane playing back still wins).
[[nodiscard]] std::array<std::optional<float>, kMacroCount> macrosAfterPresetChange(bool lockMacros,
                                                                                    const MacroDefaults& preset);

} // namespace milkdawp::core
