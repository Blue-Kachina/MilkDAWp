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

/// `projectm_load_proc` (4.2): resolves a GL function by name.
using ProjectMGlLoadProc = void* (*)(const char* name, void* userData);

/// `projectm_log_level` values (4.2, types.h).
enum class ProjectMLogLevel : int { NotSet = 0, Trace = 1, Debug = 2, Info = 3, Warn = 4, Error = 5, Fatal = 6 };

/// `projectm_log_callback` (4.2).
using ProjectMLogCallback = void (*)(const char* message, int logLevel, void* userData);

/// Typed projectM C API surface actually used by MilkDAWp. Every pointer is
/// resolved at runtime by ProjectMLibrary::load() and is non-null on a
/// successful load; nullptr otherwise. Deliberately hand-declared rather
/// than `#include <projectM-4/projectM.h>`: that keeps this header buildable
/// with no vcpkg/projectM SDK present at all, which is the whole point of
/// runtime loading (see the header comment on ProjectMLibrary).
///
/// Signatures checked against the real 4.2 headers built by the overlay port
/// (vcpkg-overlays/projectm, ADR-0008). projectM 4.2 is the minimum (D15):
/// several entries below (`createWithOpenGlLoadProc`, `openglRenderFrameFbo`,
/// `setFrameTime`, `setLogCallback`) do not exist in 4.1.x, so a 4.1 library
/// fails load() with those symbols named as missing.
///
/// `setPresetVariable` is not upstream at all: it comes from our own patch in
/// the overlay port (ADR-0012), so a stock projectM 4.2 also fails load().
struct ProjectMFunctions {
  // 4.2: resolves GL entry points through `loadProc` (or projectM's own
  // resolver when null) on the first create call in the process; later
  // calls ignore it (one resolver shared by every instance).
  ProjectMHandle (*createWithOpenGlLoadProc)(ProjectMGlLoadProc loadProc, void* userData) = nullptr;
  void (*destroy)(ProjectMHandle) = nullptr;

  void (*loadPresetFile)(ProjectMHandle instance, const char* filename, bool smoothTransition) = nullptr;
  // Loads from an in-memory, NUL-terminated preset text; lets the render
  // thread load a preset the director already read from disk (§4.2: the
  // render thread never blocks on I/O).
  void (*loadPresetData)(ProjectMHandle instance, const char* data, bool smoothTransition) = nullptr;

  void (*setWindowSize)(ProjectMHandle instance, std::size_t width, std::size_t height) = nullptr;
  void (*setMeshSize)(ProjectMHandle instance, std::size_t width, std::size_t height) = nullptr;
  void (*setFps)(ProjectMHandle instance, std::int32_t fps) = nullptr;

  void (*setBeatSensitivity)(ProjectMHandle instance, float sensitivity) = nullptr;
  float (*getBeatSensitivity)(ProjectMHandle instance) = nullptr;
  // Length of projectM's built-in blend when a preset is loaded with
  // smoothTransition == true. We decide *when*; projectM draws the blend.
  void (*setSoftCutDuration)(ProjectMHandle instance, double seconds) = nullptr;
  // projectM's own volume-delta hard-cut detector only ever fires the
  // switch-requested callback (which we never register), but it is switched
  // off explicitly so its timers do no work either.
  void (*setHardCutEnabled)(ProjectMHandle instance, bool enabled) = nullptr;
  void (*setPresetLocked)(ProjectMHandle instance, bool locked) = nullptr;
  // Where presets' textures are looked up (6.1: the bundled texture pack).
  // Clears and reloads every texture, so it is called once per instance.
  void (*setTextureSearchPaths)(ProjectMHandle instance, const char** paths, std::size_t count) = nullptr;

  // `count` is samples *per channel*; channels: 1 = mono, 2 = stereo.
  void (*pcmAddFloat)(ProjectMHandle instance, const float* samples, unsigned int count, std::int32_t channels) =
      nullptr;
  // Largest per-channel sample count projectM keeps; feeding more than this
  // in one call only keeps the newest.
  unsigned int (*pcmGetMaxSamples)() = nullptr;

  // 4.2: pins preset animation time to the caller's clock (seconds since
  // the first frame). Negative values revert to projectM's system clock.
  void (*setFrameTime)(ProjectMHandle instance, double secondsSinceFirstFrame) = nullptr;

  // 4.2: renders into the given FBO. (4.1.7's projectm_opengl_render_frame
  // always drew its final composite to framebuffer 0; see ADR-0008.)
  void (*openglRenderFrameFbo)(ProjectMHandle instance, std::uint32_t framebufferObjectId) = nullptr;
  // 4.2: draws a texture into the active preset(s)' main texture, so it feeds
  // the preset's own warp and decay (8.6f, media "Burn in").
  void (*openglBurnTexture)(ProjectMHandle instance, std::uint32_t texture, int left, int top, int width,
                            int height) = nullptr;

  // MilkDAWp patch (ADR-0012, 8.7): sets a variable in the active preset(s)'
  // per-frame and per-vertex code, written before per-frame code every frame;
  // both presets get it during a soft cut, and later presets before their
  // init code. Case-insensitive name; render thread only.
  void (*setPresetVariable)(ProjectMHandle instance, const char* name, double value) = nullptr;

  void (*setPresetSwitchFailedEventCallback)(ProjectMHandle instance,
                                              ProjectMPresetSwitchFailedCallback callback,
                                              void* userData) = nullptr;

  // 4.2: process-wide (currentThreadOnly == false) or per-thread logging.
  void (*setLogCallback)(ProjectMLogCallback callback, bool currentThreadOnly, void* userData) = nullptr;
  void (*setLogLevel)(int logLevel, bool currentThreadOnly) = nullptr;

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

  /// What every RenderEngine in the process uses (5.6): one loaded copy and
  /// one function table, shared by all plugin instances in a DAW, kept
  /// while any holder lives. The first successful load wins and later hints
  /// are ignored until every holder is gone (then the next call loads
  /// afresh). That matters beyond saving a load: projectM 4.2 keeps one GL
  /// resolver per loaded library, initialized by the first create call, so
  /// all instances must share the library *and* pass the same load proc
  /// (ProjectMInstance::create always passes null, i.e. projectM's own).
  /// A failed load is not remembered; the next call tries again.
  struct SharedLoadResult {
    std::shared_ptr<const ProjectMLibrary> library;
    std::string unavailableReason;

    [[nodiscard]] bool isAvailable() const noexcept { return library != nullptr; }
  };
  [[nodiscard]] static SharedLoadResult acquireShared(const juce::File& bundleDirectoryHint = {});

  ~ProjectMLibrary();
  ProjectMLibrary(const ProjectMLibrary&) = delete;
  ProjectMLibrary& operator=(const ProjectMLibrary&) = delete;
  ProjectMLibrary(ProjectMLibrary&&) = delete;
  ProjectMLibrary& operator=(ProjectMLibrary&&) = delete;

  [[nodiscard]] const ProjectMFunctions& functions() const noexcept { return functions_; }
  [[nodiscard]] const std::string& versionString() const noexcept { return version_; }

  /// Lowest projectM version this class was written against (D15). The
  /// required 4.2 symbols already reject 4.1.x at symbol resolution; the
  /// version check is a second, clearer message for a library that somehow
  /// has the symbols but reports an older version.
  static constexpr int kMinimumSupportedMajorVersion = 4;
  static constexpr int kMinimumSupportedMinorVersion = 2;

  /// Parses "major.minor[.patch...]" into {major, minor}; {-1, -1} on
  /// garbage. Exposed for tests.
  struct Version {
    int major = -1;
    int minor = -1;
  };
  [[nodiscard]] static Version parseVersion(const std::string& text);
  [[nodiscard]] static bool isSupportedVersion(const Version& version) noexcept;

private:
  ProjectMLibrary() = default;

  juce::DynamicLibrary library_;
  ProjectMFunctions functions_{};
  std::string version_;
};

} // namespace milkdawp::engine
