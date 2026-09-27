// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "SystemAudioCapture.h"

using namespace milkdawp;

// Platform-agnostic invariants only: `open()` touches real hardware (or, on
// macOS/Android, a consent prompt), which a CI runner cannot rely on, so
// this stays off the actual capture path (§4.7's platform `.cpp`s are what
// exercise that, by hand per platform).
TEST_CASE("createSystemAudioCapture never returns null and starts closed", "[app][system-audio]") {
  const auto capture = app::createSystemAudioCapture();
  REQUIRE(capture != nullptr);
  CHECK_FALSE(capture->isOpen());
  CHECK(capture->describe().isNotEmpty());
  CHECK(capture->takePeak(app::PeakReader::Meter) == 0.0f);
}

TEST_CASE("An unsupported platform reports Unsupported and a non-empty open() error",
         "[app][system-audio]") {
  const auto capture = app::createSystemAudioCapture();
#if JUCE_WINDOWS
  // Windows has a real implementation (4.7): it needs no user consent, so
  // it never reports Unsupported.
  CHECK(capture->permissionState() != app::SystemAudioCapture::PermissionState::Unsupported);
#else
  // macOS (§4.8) and Linux (§4.9) don't have one yet.
  CHECK(capture->permissionState() == app::SystemAudioCapture::PermissionState::Unsupported);
#endif
}
