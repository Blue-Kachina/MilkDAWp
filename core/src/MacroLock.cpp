// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/MacroLock.h"

namespace milkdawp::core {

std::array<std::optional<float>, kMacroCount> macrosAfterPresetChange(bool lockMacros, const MacroDefaults& preset) {
  std::array<std::optional<float>, kMacroCount> moves{};
  if (lockMacros) {
    return moves;
  }
  for (std::size_t slot = 0; slot < moves.size(); ++slot) {
    moves[slot] = preset[slot].value_or(0.0f);
  }
  return moves;
}

} // namespace milkdawp::core
