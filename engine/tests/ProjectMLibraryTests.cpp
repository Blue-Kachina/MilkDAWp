// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "milkdawp/engine/ProjectMLibrary.h"

using namespace milkdawp::engine;

TEST_CASE("ProjectMFunctions defaults to an all-null table", "[engine][ProjectMLibrary]") {
  ProjectMFunctions fn;
  CHECK(fn.createWithOpenGlLoadProc == nullptr);
  CHECK(fn.destroy == nullptr);
  CHECK(fn.loadPresetFile == nullptr);
  CHECK(fn.loadPresetData == nullptr);
  CHECK(fn.setWindowSize == nullptr);
  CHECK(fn.setMeshSize == nullptr);
  CHECK(fn.setFps == nullptr);
  CHECK(fn.setBeatSensitivity == nullptr);
  CHECK(fn.getBeatSensitivity == nullptr);
  CHECK(fn.setSoftCutDuration == nullptr);
  CHECK(fn.setHardCutEnabled == nullptr);
  CHECK(fn.setPresetLocked == nullptr);
  CHECK(fn.pcmAddFloat == nullptr);
  CHECK(fn.pcmGetMaxSamples == nullptr);
  CHECK(fn.setFrameTime == nullptr);
  CHECK(fn.openglRenderFrameFbo == nullptr);
  CHECK(fn.setPresetSwitchFailedEventCallback == nullptr);
  CHECK(fn.setLogCallback == nullptr);
  CHECK(fn.setLogLevel == nullptr);
  CHECK(fn.getVersionString == nullptr);
  CHECK(fn.freeString == nullptr);
}

// These tests never assume projectM's shared library is or isn't present on
// the machine running them (CI, this devcontainer, a bare dev box without
// vcpkg -- all are legitimate). They only pin down the LoadResult contract:
// exactly one of {library, unavailableReason} is populated, and a resolved
// library always exposes a fully-populated function table and a version
// string. The engine test binary gets projectM deployed next to it when the
// SDK is present, so on a dev box the "available" branch runs too.

TEST_CASE("ProjectMLibrary::load with a bogus bundle hint never crashes and honours the LoadResult contract",
          "[engine][ProjectMLibrary]") {
  const auto result = ProjectMLibrary::load(juce::File("Z:/this/path/should/not/exist/on/any/machine"));

  if (result.isAvailable()) {
    CHECK(result.unavailableReason.empty());
    REQUIRE(result.library != nullptr);
    const auto& fn = result.library->functions();
    CHECK(fn.createWithOpenGlLoadProc != nullptr);
    CHECK(fn.destroy != nullptr);
    CHECK(fn.openglRenderFrameFbo != nullptr);
    CHECK(fn.setFrameTime != nullptr);
    CHECK(ProjectMLibrary::isSupportedVersion(ProjectMLibrary::parseVersion(result.library->versionString())));
  } else {
    CHECK(result.library == nullptr);
    CHECK_FALSE(result.unavailableReason.empty());
  }
}

TEST_CASE("ProjectMLibrary::load with no hint falls back to the default search path", "[engine][ProjectMLibrary]") {
  const auto result = ProjectMLibrary::load();

  if (result.isAvailable()) {
    CHECK(result.unavailableReason.empty());
  } else {
    CHECK_FALSE(result.unavailableReason.empty());
  }
}

TEST_CASE("ProjectMLibrary parses projectM version strings", "[engine][ProjectMLibrary]") {
  const auto v420 = ProjectMLibrary::parseVersion("4.2.0");
  CHECK(v420.major == 4);
  CHECK(v420.minor == 2);

  const auto v41 = ProjectMLibrary::parseVersion("4.1");
  CHECK(v41.major == 4);
  CHECK(v41.minor == 1);

  const auto garbage = ProjectMLibrary::parseVersion("not a version");
  CHECK(garbage.major == -1);
  CHECK(ProjectMLibrary::parseVersion("").major == -1);
  CHECK(ProjectMLibrary::parseVersion("4").major == -1);
}

TEST_CASE("ProjectMLibrary accepts 4.2 and later 4.x only (D15)", "[engine][ProjectMLibrary]") {
  CHECK(ProjectMLibrary::isSupportedVersion({4, 2}));
  CHECK(ProjectMLibrary::isSupportedVersion({4, 7}));
  CHECK_FALSE(ProjectMLibrary::isSupportedVersion({4, 1}));
  CHECK_FALSE(ProjectMLibrary::isSupportedVersion({3, 9}));
  CHECK_FALSE(ProjectMLibrary::isSupportedVersion({5, 0}));
  CHECK_FALSE(ProjectMLibrary::isSupportedVersion({-1, -1}));
}
