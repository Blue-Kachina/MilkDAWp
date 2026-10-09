// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/ProjectMLibrary.h"

#include <mutex>
#include <string>

namespace milkdawp::engine {

namespace {

// vcpkg builds this port with a "d" debug postfix on Windows (a CMake
// DEBUG_POSTFIX convention this specific port applies, not universal), so a
// Debug MilkDAWp build sitting next to a Debug vcpkg install needs the
// "-4d" name; a Release build needs the plain name. Rather than hard-code
// one and break the other config, try both -- same reasoning extended to
// mac/Linux on the (untested, no hardware to confirm) assumption that any
// debug postfix convention there would follow the same pattern.
#if JUCE_WINDOWS
constexpr const char* kLibraryFileNames[] = {"projectM-4.dll", "projectM-4d.dll"};
#elif JUCE_MAC
constexpr const char* kLibraryFileNames[] = {"libprojectM-4.dylib", "libprojectM-4d.dylib"};
#else
constexpr const char* kLibraryFileNames[] = {"libprojectM-4.so", "libprojectM-4d.so"};
#endif

template <typename Fn>
bool resolveSymbol(juce::DynamicLibrary& lib, const char* name, Fn& outFn) {
  outFn = reinterpret_cast<Fn>(lib.getFunction(name));
  return outFn != nullptr;
}

juce::File currentModuleDirectory() {
  return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
}

bool tryOpen(juce::DynamicLibrary& lib, const juce::File& directory) {
  if (!directory.isDirectory())
    return false;
  for (const char* name : kLibraryFileNames) {
    const auto candidate = directory.getChildFile(name);
    if (candidate.existsAsFile() && lib.open(candidate.getFullPathName()))
      return true;
  }
  return false;
}

bool tryOpenBareName(juce::DynamicLibrary& lib) {
  for (const char* name : kLibraryFileNames) {
    if (lib.open(name))
      return true;
  }
  return false;
}

} // namespace

ProjectMLibrary::~ProjectMLibrary() = default;

ProjectMLibrary::LoadResult ProjectMLibrary::load(const juce::File& bundleDirectoryHint) {
  LoadResult result;
  auto instance = std::unique_ptr<ProjectMLibrary>(new ProjectMLibrary());

  bool opened = false;
  if (bundleDirectoryHint != juce::File())
    opened = tryOpen(instance->library_, bundleDirectoryHint);
  if (!opened)
    opened = tryOpen(instance->library_, currentModuleDirectory());
  if (!opened)
    opened = tryOpenBareName(instance->library_);

  if (!opened) {
    result.unavailableReason = std::string("could not locate or load '") + kLibraryFileNames[0] +
                                "' (checked the bundle directory, the current module's directory, "
                                "and the platform's default library search path)";
    return result;
  }

  auto& fn = instance->functions_;
  bool allResolved = true;
  std::string missing;
  auto require = [&](const char* name, auto& target) {
    if (!resolveSymbol(instance->library_, name, target)) {
      allResolved = false;
      if (!missing.empty())
        missing += ", ";
      missing += name;
    }
  };

  require("projectm_create_with_opengl_load_proc", fn.createWithOpenGlLoadProc);
  require("projectm_destroy", fn.destroy);
  require("projectm_load_preset_file", fn.loadPresetFile);
  require("projectm_load_preset_data", fn.loadPresetData);
  require("projectm_set_window_size", fn.setWindowSize);
  require("projectm_set_mesh_size", fn.setMeshSize);
  require("projectm_set_fps", fn.setFps);
  require("projectm_set_beat_sensitivity", fn.setBeatSensitivity);
  require("projectm_get_beat_sensitivity", fn.getBeatSensitivity);
  require("projectm_set_soft_cut_duration", fn.setSoftCutDuration);
  require("projectm_set_hard_cut_enabled", fn.setHardCutEnabled);
  require("projectm_set_preset_locked", fn.setPresetLocked);
  require("projectm_set_texture_search_paths", fn.setTextureSearchPaths);
  require("projectm_pcm_add_float", fn.pcmAddFloat);
  require("projectm_pcm_get_max_samples", fn.pcmGetMaxSamples);
  require("projectm_set_frame_time", fn.setFrameTime);
  require("projectm_opengl_render_frame_fbo", fn.openglRenderFrameFbo);
  require("projectm_opengl_burn_texture", fn.openglBurnTexture);
  require("projectm_set_texture_load_event_callback", fn.setTextureLoadEventCallback);
  require("projectm_set_preset_switch_failed_event_callback", fn.setPresetSwitchFailedEventCallback);
  require("projectm_set_log_callback", fn.setLogCallback);
  require("projectm_set_log_level", fn.setLogLevel);
  require("projectm_set_preset_variable", fn.setPresetVariable);
  require("projectm_get_version_string", fn.getVersionString);
  require("projectm_free_string", fn.freeString);

  if (!allResolved) {
    result.unavailableReason = "loaded '" + std::string(kLibraryFileNames[0]) +
                                "' but it is missing expected symbol(s): " + missing +
                                " (projectM older than 4.2, or without MilkDAWp's patches? MilkDAWp needs the "
                                "patched 4.2 from vcpkg-overlays/projectm, see ADR-0008 and ADR-0012)";
    return result;
  }

  const char* rawVersion = fn.getVersionString();
  if (rawVersion == nullptr) {
    result.unavailableReason = "projectm_get_version_string() returned null";
    return result;
  }
  instance->version_ = rawVersion;
  fn.freeString(rawVersion);

  if (!isSupportedVersion(parseVersion(instance->version_))) {
    result.unavailableReason = "projectM version " + instance->version_ + " is older than the minimum supported (" +
                                std::to_string(kMinimumSupportedMajorVersion) + "." +
                                std::to_string(kMinimumSupportedMinorVersion) + ")";
    return result;
  }

  result.library = std::move(instance);
  return result;
}

ProjectMLibrary::SharedLoadResult ProjectMLibrary::acquireShared(const juce::File& bundleDirectoryHint) {
  // The weak_ptr owns nothing: the library lives exactly as long as the
  // engines holding it.
  static std::mutex mutex;
  static std::weak_ptr<const ProjectMLibrary> shared;

  const std::lock_guard lock(mutex);
  SharedLoadResult result;
  if (auto existing = shared.lock()) {
    result.library = std::move(existing);
    return result;
  }
  auto loaded = load(bundleDirectoryHint);
  if (!loaded.isAvailable()) {
    result.unavailableReason = std::move(loaded.unavailableReason);
    return result;
  }
  result.library = std::shared_ptr<const ProjectMLibrary>(std::move(loaded.library));
  shared = result.library;
  return result;
}

ProjectMLibrary::Version ProjectMLibrary::parseVersion(const std::string& text) {
  Version version;
  const auto firstDot = text.find('.');
  if (firstDot == std::string::npos || firstDot == 0) {
    return version;
  }
  const auto secondDot = text.find('.', firstDot + 1);
  const auto minorText = text.substr(firstDot + 1, secondDot == std::string::npos ? std::string::npos
                                                                                   : secondDot - firstDot - 1);
  try {
    std::size_t consumed = 0;
    const int major = std::stoi(text.substr(0, firstDot), &consumed);
    if (consumed != firstDot) {
      return version;
    }
    const int minor = std::stoi(minorText);
    version.major = major;
    version.minor = minor;
  } catch (...) {
    return Version{};
  }
  return version;
}

bool ProjectMLibrary::isSupportedVersion(const Version& version) noexcept {
  if (version.major != kMinimumSupportedMajorVersion) {
    // A future 5.x may break the C API; treat anything but 4.x as unsupported
    // until someone checks it.
    return false;
  }
  return version.minor >= kMinimumSupportedMinorVersion;
}

} // namespace milkdawp::engine
