// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

namespace milkdawp::core {

/// The milkdawp_core semantic version. Tracks the top-level project version
/// (see the top-level CMakeLists.txt project() call), not any shell's.
struct Version {
  int major;
  int minor;
  int patch;
};

[[nodiscard]] Version version() noexcept;

/// "major.minor.patch", plus a pre-release suffix on a pre-release build
/// ("2.0.0-beta.1"; MILKDAWP_VERSION_LABEL in the top-level CMakeLists.txt).
[[nodiscard]] const char* versionString() noexcept;

} // namespace milkdawp::core
