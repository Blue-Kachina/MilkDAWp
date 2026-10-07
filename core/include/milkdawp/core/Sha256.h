// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace milkdawp::core {

/// SHA-256 of `data` (FIPS 180-4), as 64 lower-case hex digits. For
/// `mdw_source_sha256` (8.11): the `.milk` a `.milkdawp` was made from.
[[nodiscard]] std::string sha256Hex(std::string_view data);

} // namespace milkdawp::core
