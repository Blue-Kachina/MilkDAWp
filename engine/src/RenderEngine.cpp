// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/RenderEngine.h"

#include <juce_core/juce_core.h>

namespace milkdawp::engine {

namespace {

// projectM links GLEW internally (confirmed via `dumpbin /dependents` on the
// real vcpkg-built projectM-4d.dll: it statically imports glew32d.dll) but
// never calls glewInit() itself -- checked against projectM's own source
// (vcpkg's buildtrees), glewInit() only appears in its unrelated SDL example
// app, never inside the library. JUCE loads its own OpenGL extension
// bindings independently (the juce::gl namespace) which does nothing for
// GLEW's separate global function-pointer table, so without this, any
// extension call projectm_create() makes internally jumps through a null
// pointer -- confirmed by a real crash the first time this ran against an
// actual projectM install. Windows only for now: mac/Linux GL loader
// behaviour here is unverified, no hardware to test against (same caveat
// Phase 2.1/2.3 already carry elsewhere in this file).
bool ensureGlewInitialized() {
#if JUCE_WINDOWS
  static const bool initialized = [] {
    // glew32(d).dll is already resident in the process by this point --
    // projectM's own DLL statically imports it, so Windows loaded it as
    // part of loading that DLL. Opening it again by name just returns a
    // handle to the same loaded module (and bumps its refcount), which is
    // exactly what's needed: glewInit() must populate the *same* global
    // function-pointer table that projectM's statically-linked calls read.
    static juce::DynamicLibrary glewLib; // kept alive: GLEW's populated
                                          // pointers must outlive this call
    for (const char* name : {"glew32.dll", "glew32d.dll"}) {
      if (glewLib.open(name))
        break;
    }
    using GlewInitFn = unsigned int (*)();
    auto* glewInit = reinterpret_cast<GlewInitFn>(glewLib.getFunction("glewInit"));
    return glewInit != nullptr && glewInit() == 0; // GLEW_OK == 0
  }();
  return initialized;
#else
  return true;
#endif
}

// projectm_opengl_render_frame() (real API, render_opengl.h) has no FBO
// parameter -- it renders into whatever framebuffer is currently bound.
// This binds targetFbo for the call and restores whatever was bound before,
// so this doesn't leave JUCE's own OpenGL state (it calls renderFrame() from
// its own render callback) pointed at our FBO afterwards.
class ScopedFramebufferBinding {
public:
  explicit ScopedFramebufferBinding(unsigned int fbo) {
    using namespace ::juce::gl;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  }
  ~ScopedFramebufferBinding() {
    using namespace ::juce::gl;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_));
  }
  ScopedFramebufferBinding(const ScopedFramebufferBinding&) = delete;
  ScopedFramebufferBinding& operator=(const ScopedFramebufferBinding&) = delete;

private:
  GLint previous_ = 0;
};

} // namespace

std::unique_ptr<RenderEngine> RenderEngine::create(const Config& config, const juce::File& bundleDirectoryHint) {
  auto loadResult = ProjectMLibrary::load(bundleDirectoryHint);
  return std::unique_ptr<RenderEngine>(
      new RenderEngine(std::move(loadResult.library), std::move(loadResult.unavailableReason), config));
}

RenderEngine::RenderEngine(std::unique_ptr<ProjectMLibrary> library, std::string unavailableReason, Config config)
    : library_(std::move(library)), unavailableReason_(std::move(unavailableReason)), config_(config) {
  // Deliberately does not create a projectm instance here -- see
  // ensureInstanceCreated()'s doc comment. If library_ loaded,
  // unavailableReason_ stays empty and isAvailable() stays false until a
  // surface's GL context calls ensureInstanceCreated(); that is a normal,
  // expected transient state, not a failure.
}

RenderEngine::~RenderEngine() {
  // Normally already null: whichever OutputSurface last held the instance
  // calls releaseInstance() from openGLContextClosing() (context still
  // current) before it is destroyed. This is a safety net for the case
  // where that never happened (e.g. no surface ever attached), not the
  // expected path -- there is no GL context guaranteed current here, so
  // this destroy() call carries the same risk the constructor's eager
  // create() used to, just for the much rarer "instance outlived every
  // surface" case instead of the common "every plugin load" case.
  if (instance_ != nullptr && library_) {
    library_->functions().destroy(instance_);
  }
}

void RenderEngine::ensureInstanceCreated() {
  if (instance_ != nullptr || !library_) {
    return;
  }

  if (!ensureGlewInitialized()) {
    unavailableReason_ = "glewInit() failed or glewInit symbol not found (projectM's GL extension "
                          "entry points would be null pointers)";
    return;
  }

  instance_ = library_->functions().create();
  if (instance_ == nullptr) {
    unavailableReason_ = "projectm_create() returned null";
    return;
  }

  const auto& fn = library_->functions();
  fn.setWindowSize(instance_, config_.windowWidth, config_.windowHeight);
  fn.setMeshSize(instance_, config_.meshWidth, config_.meshHeight);
  fn.setFps(instance_, config_.fps);
  if (presetSwitchFailedCallback_) {
    fn.setPresetSwitchFailedEventCallback(instance_, &RenderEngine::presetSwitchFailedTrampoline, this);
  }
}

void RenderEngine::releaseInstance() {
  if (instance_ == nullptr || !library_) {
    return;
  }
  library_->functions().destroy(instance_);
  instance_ = nullptr;
}

bool RenderEngine::pushParameterUpdate(const ParameterUpdate& update) noexcept {
  return parameterQueue_.push(update);
}

void RenderEngine::loadPreset(const std::string& filename, bool smoothTransition) {
  if (!isAvailable()) {
    return;
  }
  library_->functions().loadPresetFile(instance_, filename.c_str(), smoothTransition);
}

void RenderEngine::setPresetSwitchFailedCallback(PresetSwitchFailedCallback callback) {
  presetSwitchFailedCallback_ = std::move(callback);
  if (!isAvailable()) {
    return;
  }
  library_->functions().setPresetSwitchFailedEventCallback(instance_, &RenderEngine::presetSwitchFailedTrampoline,
                                                             this);
}

void RenderEngine::renderFrame(const core::AudioRing& audioRing, unsigned int targetFbo) {
  if (!isAvailable()) {
    return;
  }

  drainParameterUpdates();

  const auto frames = config_.pcmFrameCount;
  const auto channels = audioRing.numChannels();
  pcmScratch_.resize(frames * static_cast<std::size_t>(channels));
  audioRing.copyLatest(pcmScratch_.data(), frames);

  const auto& fn = library_->functions();
  fn.pcmAddFloat(instance_, pcmScratch_.data(), static_cast<std::uint32_t>(frames), channels);

  const ScopedFramebufferBinding fboBinding(targetFbo);
  fn.openglRenderFrame(instance_);
}

void RenderEngine::drainParameterUpdates() {
  while (const auto update = parameterQueue_.pop()) {
    applyParameterUpdate(*update);
  }
}

void RenderEngine::applyParameterUpdate(const ParameterUpdate& update) {
  const auto& fn = library_->functions();
  switch (update.target) {
  case ParameterTarget::BeatSensitivity:
    fn.setBeatSensitivity(instance_, update.value);
    return;
  case ParameterTarget::PresetDurationSeconds:
    fn.setPresetDuration(instance_, static_cast<double>(update.value));
    return;
  }
}

void RenderEngine::presetSwitchFailedTrampoline(const char* filename, const char* message, void* userData) {
  auto* self = static_cast<RenderEngine*>(userData);
  if (self->presetSwitchFailedCallback_) {
    self->presetSwitchFailedCallback_(filename != nullptr ? filename : "", message != nullptr ? message : "");
  }
}

} // namespace milkdawp::engine
