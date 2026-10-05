// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "milkdawp/engine/ProjectMLibrary.h"

namespace milkdawp::engine {

/// One projectM instance and the calls MilkDAWp makes on it (Phase 2.13).
/// Kept as its own object rather than loose `RenderEngine` members so
/// nothing assumes exactly one instance (ADR-0008, "Layers").
///
/// Every method, including create() and the destructor, needs the GL
/// context the instance was created in to be current on the calling thread:
/// projectM allocates GL objects in create and frees them in destroy.
class ProjectMInstance {
public:
  struct Settings {
    std::size_t meshWidth = 48;
    std::size_t meshHeight = 32;
    std::int32_t fps = 60;
    int width = 1280;
    int height = 720;
    float beatSensitivity = 1.0f;
    double softCutSeconds = 3.0;
    /// Folders projectM searches for the textures presets reference (6.1).
    /// Empty leaves projectM's default.
    std::vector<std::string> textureSearchPaths;
  };

  /// Null (with `error` set) if projectM refuses to create an instance,
  /// which in practice means the current GL context is missing or too old.
  [[nodiscard]] static std::unique_ptr<ProjectMInstance> create(const ProjectMLibrary& library,
                                                                  const Settings& settings, std::string& error);

  ~ProjectMInstance();
  ProjectMInstance(const ProjectMInstance&) = delete;
  ProjectMInstance& operator=(const ProjectMInstance&) = delete;

  void setOutputSize(int width, int height);
  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] int height() const noexcept { return height_; }

  /// `frames` samples per channel, interleaved.
  void addPcm(const float* interleaved, std::size_t frames, int channels);
  /// projectm_pcm_get_max_samples(): the most per-channel samples projectM
  /// keeps from one addPcm() call.
  [[nodiscard]] unsigned int maxPcmSamples() const;

  /// `data` must be NUL-terminated preset text. projectM parses and
  /// compiles the preset's shaders synchronously inside this call.
  void loadPresetData(const char* data, bool smoothTransition);
  void loadPresetFile(const std::string& path, bool smoothTransition);

  void setSoftCutDuration(double seconds);
  void setBeatSensitivity(float sensitivity);
  /// Pin preset time to the caller's clock (seconds since the first
  /// frame); negative reverts to projectM's system clock.
  void setFrameTime(double seconds);

  /// Renders one frame into `fbo` (sized to the last setOutputSize()).
  void renderTo(std::uint32_t fbo);

  /// Called synchronously from inside loadPresetData()/loadPresetFile()
  /// when projectM rejects a preset.
  void setPresetSwitchFailedCallback(ProjectMPresetSwitchFailedCallback callback, void* userData);

private:
  ProjectMInstance(const ProjectMLibrary& library, ProjectMHandle handle);

  const ProjectMLibrary& library_;
  ProjectMHandle handle_;
  int width_ = 0;
  int height_ = 0;
};

} // namespace milkdawp::engine
