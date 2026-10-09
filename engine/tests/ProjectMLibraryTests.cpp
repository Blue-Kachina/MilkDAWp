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
  CHECK(fn.openglBurnTexture == nullptr);
  CHECK(fn.setPresetVariable == nullptr);
  CHECK(fn.setTextureLoadEventCallback == nullptr);
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
    CHECK(fn.openglBurnTexture != nullptr);
    CHECK(fn.setFrameTime != nullptr);
    CHECK(fn.setPresetVariable != nullptr); // our patch (ADR-0012) is in the loaded library
    CHECK(fn.setTextureLoadEventCallback != nullptr);
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

TEST_CASE("ProjectMLibrary::acquireShared hands every caller the same library while any holder lives (5.6)",
          "[engine][ProjectMLibrary]") {
  auto first = ProjectMLibrary::acquireShared();
  if (!first.isAvailable()) {
    CHECK_FALSE(first.unavailableReason.empty());
    return;
  }
  CHECK(first.unavailableReason.empty());
  // A second plugin instance gets the same copy and function table, whatever
  // hint it passes.
  const auto second = ProjectMLibrary::acquireShared(juce::File("Z:/this/path/should/not/exist/on/any/machine"));
  REQUIRE(second.isAvailable());
  CHECK(second.library == first.library);
  CHECK(second.library->functions().createWithOpenGlLoadProc ==
        first.library->functions().createWithOpenGlLoadProc);

  // The copy lives while anyone holds it...
  first.library.reset();
  const auto third = ProjectMLibrary::acquireShared();
  REQUIRE(third.isAvailable());
  CHECK(third.library == second.library);
}

TEST_CASE("ProjectMLibrary::acquireShared loads afresh once every holder is gone", "[engine][ProjectMLibrary]") {
  {
    const auto held = ProjectMLibrary::acquireShared();
    if (!held.isAvailable()) {
      SUCCEED("projectM is unavailable here: " + held.unavailableReason);
      return;
    }
  }
  // ...and once it is released, the next caller still gets a working one.
  const auto again = ProjectMLibrary::acquireShared();
  REQUIRE(again.isAvailable());
  CHECK(again.library->functions().createWithOpenGlLoadProc != nullptr);
  CHECK_FALSE(again.library->versionString().empty());
}
