// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/core/Version.h"

// Set from the top-level project() version and MILKDAWP_VERSION_LABEL by
// core/CMakeLists.txt.
#if !defined(MILKDAWP_VERSION_MAJOR) || !defined(MILKDAWP_VERSION_MINOR) || !defined(MILKDAWP_VERSION_PATCH) || \
    !defined(MILKDAWP_VERSION_LABEL)
#error "core/CMakeLists.txt must define the MILKDAWP_VERSION_* macros for Version.cpp"
#endif

namespace milkdawp::core {

Version version() noexcept { return Version{MILKDAWP_VERSION_MAJOR, MILKDAWP_VERSION_MINOR, MILKDAWP_VERSION_PATCH}; }

const char* versionString() noexcept { return MILKDAWP_VERSION_LABEL; }

} // namespace milkdawp::core
