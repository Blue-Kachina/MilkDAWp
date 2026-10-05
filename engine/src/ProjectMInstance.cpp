// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/ProjectMInstance.h"

#include <algorithm>

namespace milkdawp::engine {

std::unique_ptr<ProjectMInstance> ProjectMInstance::create(const ProjectMLibrary& library, const Settings& settings,
                                                           std::string& error) {
  const auto& fn = library.functions();
  // Null load proc: projectM's own resolver (glad + wglGetProcAddress /
  // eglGetProcAddress) resolves against whichever context is current now.
  // Only the first create in the process initializes the resolver.
  ProjectMHandle handle = fn.createWithOpenGlLoadProc(nullptr, nullptr);
  if (handle == nullptr) {
    error = "projectm_create_with_opengl_load_proc() returned null (no current GL context, or GL older than 3.3)";
    return nullptr;
  }

  std::unique_ptr<ProjectMInstance> instance(new ProjectMInstance(library, handle));
  fn.setMeshSize(handle, settings.meshWidth, settings.meshHeight);
  fn.setFps(handle, settings.fps);
  fn.setBeatSensitivity(handle, settings.beatSensitivity);
  fn.setSoftCutDuration(handle, settings.softCutSeconds);
  // We decide when presets change (§2.1): projectM's own timer and
  // hard-cut detector stay off, and nothing registers its switch-requested
  // callback anyway.
  fn.setHardCutEnabled(handle, false);
  fn.setPresetLocked(handle, true);
  if (!settings.textureSearchPaths.empty()) {
    std::vector<const char*> paths;
    paths.reserve(settings.textureSearchPaths.size());
    for (const auto& path : settings.textureSearchPaths) {
      paths.push_back(path.c_str());
    }
    fn.setTextureSearchPaths(handle, paths.data(), paths.size());
  }
  instance->setOutputSize(settings.width, settings.height);
  return instance;
}

ProjectMInstance::ProjectMInstance(const ProjectMLibrary& library, ProjectMHandle handle)
    : library_(library), handle_(handle) {}

ProjectMInstance::~ProjectMInstance() { library_.functions().destroy(handle_); }

void ProjectMInstance::setOutputSize(int width, int height) {
  width = std::max(width, 1);
  height = std::max(height, 1);
  if (width == width_ && height == height_) {
    return;
  }
  width_ = width;
  height_ = height;
  library_.functions().setWindowSize(handle_, static_cast<std::size_t>(width), static_cast<std::size_t>(height));
}

void ProjectMInstance::addPcm(const float* interleaved, std::size_t frames, int channels) {
  if (frames == 0) {
    return;
  }
  // projectM accepts mono or stereo; anything wider is fed as its first
  // two channels would need de-interleaving, which the engine never asks
  // for (the ring is always stereo).
  library_.functions().pcmAddFloat(handle_, interleaved, static_cast<unsigned int>(frames),
                                   channels >= 2 ? 2 : 1);
}

unsigned int ProjectMInstance::maxPcmSamples() const { return library_.functions().pcmGetMaxSamples(); }

void ProjectMInstance::loadPresetData(const char* data, bool smoothTransition) {
  library_.functions().loadPresetData(handle_, data, smoothTransition);
}

void ProjectMInstance::loadPresetFile(const std::string& path, bool smoothTransition) {
  library_.functions().loadPresetFile(handle_, path.c_str(), smoothTransition);
}

void ProjectMInstance::setSoftCutDuration(double seconds) {
  library_.functions().setSoftCutDuration(handle_, seconds);
}

void ProjectMInstance::setBeatSensitivity(float sensitivity) {
  library_.functions().setBeatSensitivity(handle_, sensitivity);
}

void ProjectMInstance::setFrameTime(double seconds) { library_.functions().setFrameTime(handle_, seconds); }

void ProjectMInstance::renderTo(std::uint32_t fbo) { library_.functions().openglRenderFrameFbo(handle_, fbo); }

void ProjectMInstance::burnTexture(std::uint32_t texture) {
  library_.functions().openglBurnTexture(handle_, texture, 0, 0, width_, height_);
}

void ProjectMInstance::setPresetSwitchFailedCallback(ProjectMPresetSwitchFailedCallback callback, void* userData) {
  library_.functions().setPresetSwitchFailedEventCallback(handle_, callback, userData);
}

} // namespace milkdawp::engine
