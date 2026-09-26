// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <juce_core/juce_core.h>

namespace milkdawp::engine {

/// Opaque handle to a projectM visualizer instance (`projectm_handle` in the
/// real C API). Never dereferenced by us -- only passed back into the
/// function table.
using ProjectMHandle = void*;

using ProjectMPresetSwitchFailedCallback = void (*)(const char* presetFilename,
                                                     const char* message,
                                                     void* userData);

/// Typed projectM 4 C API surface actually used by MilkDAWp (Phase 2). Every
/// pointer is resolved at runtime by ProjectMLibrary::load() and is non-null
/// on a successful load; nullptr otherwise. Deliberately hand-declared rather
/// than `#include <projectM-4/projectM.h>`: that keeps this header buildable
/// with no vcpkg/projectM SDK present at all, which is the whole point of
/// runtime loading (see the header comment on ProjectMLibrary).
struct ProjectMFunctions {
  ProjectMHandle (*create)() = nullptr;
  void (*destroy)(ProjectMHandle) = nullptr;

  void (*loadPresetFile)(ProjectMHandle instance, const char* filename, bool smoothTransition) = nullptr;

  void (*setWindowSize)(ProjectMHandle instance, std::size_t width, std::size_t height) = nullptr;
  void (*setMeshSize)(ProjectMHandle instance, std::size_t width, std::size_t height) = nullptr;
  void (*setFps)(ProjectMHandle instance, std::int32_t fps) = nullptr;
  void (*setPresetDuration)(ProjectMHandle instance, double seconds) = nullptr;

  void (*setBeatSensitivity)(ProjectMHandle instance, float sensitivity) = nullptr;
  float (*getBeatSensitivity)(ProjectMHandle instance) = nullptr;

  // channels: 1 = mono, 2 = stereo, matching projectm_channels in the real API.
  void (*pcmAddFloat)(ProjectMHandle instance, const float* samples, std::uint32_t count, std::int32_t channels) =
      nullptr;

  // The real projectM 4 C API (checked against the real vcpkg-installed
  // header, render_opengl.h -- the hand-declared name this replaced,
  // "projectm_opengl_render_frame_fbo", does not exist in the real API and
  // made every load fail at symbol resolution) has no FBO parameter at all:
  // it renders into whatever framebuffer is currently bound. The caller
  // must glBindFramebuffer() the target FBO before calling this (§4.5:
  // never bind the default framebuffer).
  void (*openglRenderFrame)(ProjectMHandle instance) = nullptr;

  void (*setPresetSwitchFailedEventCallback)(ProjectMHandle instance,
                                              ProjectMPresetSwitchFailedCallback callback,
                                              void* userData) = nullptr;

  const char* (*getVersionString)() = nullptr;
  void (*freeString)(const char* str) = nullptr;
};

/// RAII wrapper around one dynamically-loaded copy of libprojectM (Phase 2.1,
/// §2.6, §4.5). Replaces v1's two near-identical GetProcAddress/dlsym blocks
/// plus `/DELAYLOAD` with one loading strategy on every platform, built on
/// `juce::DynamicLibrary` (engine/ is already JUCE-dependent per §4.1, so this
/// buys us the platform abstraction for free instead of hand-rolling it
/// again).
///
/// Deliberately does *not* link against the vcpkg-built import library
/// (see cmake/ProjectMDependency.cmake / EngineInfo::hasProjectM(), which
/// answer a different question: "was the SDK present at configure time?").
/// Resolving symbols by name at runtime means a plugin binary built with this
/// class still loads and reports Unavailable{reason} cleanly on a host where
/// projectM's shared library is simply missing, which is exactly the
/// robustness v1 got right for the wrong reasons (§2.6) and this class gets
/// right on purpose.
class ProjectMLibrary {
public:
  /// Result of a load attempt. Exactly one of `library` / `unavailableReason`
  /// is set. Kept as a plain aggregate (no exceptions) so callers on the
  /// render/init path can handle "no projectM here" as an ordinary value.
  struct LoadResult {
    std::unique_ptr<ProjectMLibrary> library; // null when unavailable
    std::string unavailableReason;            // empty when library is non-null

    [[nodiscard]] bool isAvailable() const noexcept { return library != nullptr; }
  };

  /// Attempts to locate and load projectM's shared library and resolve every
  /// function in ProjectMFunctions. Search order:
  ///   1. `bundleDirectoryHint` (when given a valid directory) -- the
  ///      "bundle-relative search" from §2.1, e.g. the plugin/app's own
  ///      binary directory.
  ///   2. The directory containing the current executable/plugin module.
  ///   3. The bare platform library name, so the OS's own search (PATH on
  ///      Windows, rpath/RUNPATH on Linux, @rpath on macOS) gets the last
  ///      word -- this is what makes a system-installed or bundle-adjacent
  ///      copy findable without us hard-coding a full path.
  /// Never throws; every failure mode (library not found, missing symbol,
  /// unsupported version) becomes an `Unavailable{reason}` explaining which.
  [[nodiscard]] static LoadResult load(const juce::File& bundleDirectoryHint = {});

  ~ProjectMLibrary();
  ProjectMLibrary(const ProjectMLibrary&) = delete;
  ProjectMLibrary& operator=(const ProjectMLibrary&) = delete;
  ProjectMLibrary(ProjectMLibrary&&) = delete;
  ProjectMLibrary& operator=(ProjectMLibrary&&) = delete;

  [[nodiscard]] const ProjectMFunctions& functions() const noexcept { return functions_; }
  [[nodiscard]] const std::string& versionString() const noexcept { return version_; }

  /// Lowest projectM major version this class was written against. load()
  /// rejects anything older via Unavailable{reason} rather than risking an
  /// ABI mismatch on an unresolved-but-wrong-shaped symbol.
  static constexpr int kMinimumSupportedMajorVersion = 4;

private:
  ProjectMLibrary() = default;

  juce::DynamicLibrary library_;
  ProjectMFunctions functions_{};
  std::string version_;
};

} // namespace milkdawp::engine
