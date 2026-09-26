// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/core/Messages.h"
#include "milkdawp/engine/ProjectMLibrary.h"

namespace milkdawp::engine {

/// Owns exactly one projectM instance, and exactly one GL context, for the
/// lifetime of the processor or app (§4.5, Phase 2.2/2.3). The context
/// (glContext()) is a plain `juce::OpenGLContext` member -- constructed once
/// with the engine, destroyed once with the engine -- that an `OutputSurface`
/// (Phase 2.4) attaches itself to and detaches from as it is created and
/// destroyed. This is the actual mechanism meant to fix v1's pop-out bug
/// (§2.4: reparenting the GL component forced JUCE to recreate the context,
/// which reset the visual): the context here does not belong to any
/// Component, so a Component going away should not take it down too.
///
/// Whether a fresh `attachTo()` after a `detach()` actually preserves GL
/// state (textures, FBOs) on every platform, or whether it silently gets a
/// new native context requiring `setNativeSharedContext()` to bridge
/// resources, is exactly Phase 2.3's open spike question -- `OutputSurface`
/// exposes a creation counter for measuring this empirically instead of
/// guessing. renderFrame() assumes the caller already has this context
/// current (the same contract juce::OpenGLContext's renderer callback
/// gives you).
///
/// Threading (§4.2): pushParameterUpdate() and loadPreset() are for the
/// message/preset-IO threads; renderFrame() is for the render (GL) thread
/// only. setPresetSwitchFailedCallback() must be called during setup, before
/// the first renderFrame() -- it is not itself synchronized against a
/// concurrently-running render thread.
class RenderEngine {
public:
  struct Config {
    std::size_t windowWidth = 512;
    std::size_t windowHeight = 512;
    std::size_t meshWidth = 32;
    std::size_t meshHeight = 24;
    std::int32_t fps = 60;
    std::size_t pcmFrameCount = 512; // frames pulled from the AudioRing per renderFrame() call
  };

  /// What a queued ParameterUpdate changes. Deliberately a closed enum, not a
  /// core::ParameterModel id: RenderEngine only understands the handful of
  /// projectM-native knobs it forwards, and plugin/app (Phase 3/4) are
  /// responsible for translating a ParameterModel change into one of these --
  /// that translation is a string-keyed lookup exactly once, off the render
  /// thread, not per frame (the anti-pattern this class replaces, §2.7).
  enum class ParameterTarget : std::uint8_t {
    BeatSensitivity,
    PresetDurationSeconds,
  };

  struct ParameterUpdate {
    ParameterTarget target;
    float value;
  };

  static constexpr std::size_t kParameterQueueCapacity = 64;

  using PresetSwitchFailedCallback = std::function<void(std::string_view filename, std::string_view message)>;

  /// Loads projectM (see ProjectMLibrary::load()) but does *not* create an
  /// instance yet -- that needs a current GL context (see
  /// ensureInstanceCreated()), which does not exist at this point (this
  /// factory typically runs from the processor's constructor, before any
  /// OutputSurface/context exists). Never returns null: an unavailable
  /// projectM (library not found) is a valid RenderEngine in a
  /// permanently-inert state (isAvailable() == false until a surface
  /// attaches), not a construction failure -- callers keep their engine
  /// object and query the reason to show the user (§2.6).
  [[nodiscard]] static std::unique_ptr<RenderEngine> create(const Config& config = {},
                                                             const juce::File& bundleDirectoryHint = {});

  ~RenderEngine();
  RenderEngine(const RenderEngine&) = delete;
  RenderEngine& operator=(const RenderEngine&) = delete;
  RenderEngine(RenderEngine&&) = delete;
  RenderEngine& operator=(RenderEngine&&) = delete;

  [[nodiscard]] bool isAvailable() const noexcept { return instance_ != nullptr; }
  [[nodiscard]] const std::string& unavailableReason() const noexcept { return unavailableReason_; }

  /// Message/UI thread. Never blocks; returns false (and drops the update)
  /// if the queue is momentarily full rather than stalling the caller.
  /// Safe to call even when !isAvailable() (the update is simply never
  /// applied, since renderFrame() -- the only consumer -- no-ops).
  bool pushParameterUpdate(const ParameterUpdate& update) noexcept;

  /// Message/preset-IO thread. Loads immediately and synchronously through
  /// the function table. Sample-accurate scheduling against a beat/bar
  /// boundary (dueAtSample) is TransitionScheduler's job upstream and Phase
  /// 2.6's job on this side -- this is the direct, unscheduled entry point
  /// that 2.6 will call at the right time. A no-op when !isAvailable().
  void loadPreset(const std::string& filename, bool smoothTransition);

  /// Must be called before the first renderFrame() (see class comment).
  /// A no-op when !isAvailable().
  void setPresetSwitchFailedCallback(PresetSwitchFailedCallback callback);

  /// Render thread, once per video frame. Drains pending parameter updates,
  /// feeds the latest PCM from `audioRing` into projectM via copyLatest()
  /// (AudioRing's own doc comment: this is exactly the "safe third-thread
  /// reader" it was designed for -- the analysis thread's consumeHop()
  /// cursor is untouched), then renders into `targetFbo`. A no-op when
  /// !isAvailable().
  void renderFrame(const core::AudioRing& audioRing, unsigned int targetFbo);

  /// The persistent GL context (see class comment). `OutputSurface`
  /// instances call `glContext().setRenderer(...)` / `attachTo(*this)` on
  /// construction and `glContext().detach()` on destruction; nothing here
  /// ever destroys the `OpenGLContext` object itself except ~RenderEngine.
  [[nodiscard]] juce::OpenGLContext& glContext() noexcept { return glContext_; }

  /// Phase 2.3 spike instrumentation: how many times has a *new* native GL
  /// context actually been created (i.e. `newOpenGLContextCreated()` fired)
  /// since this engine was constructed? `OutputSurface` calls
  /// notifyGlContextCreated() from that callback. If this only ever reaches
  /// 1 across many editor open/close cycles, plain attach/detach preserves
  /// the context; if it climbs with every reopen, it doesn't, and sharing
  /// or a hidden context-owner window (§4.5) is needed instead.
  void notifyGlContextCreated() noexcept { ++glContextCreationCount_; }
  [[nodiscard]] int glContextCreationCount() const noexcept { return glContextCreationCount_; }

  /// Call from a renderer's newOpenGLContextCreated(), i.e. with a GL
  /// context actually current. Creates the projectm instance if the library
  /// loaded and no instance exists yet; a no-op otherwise (idempotent).
  /// Deliberately not done in the constructor: real projectM (confirmed
  /// empirically now that one is actually loadable, not just guessed at per
  /// the Phase 2.3 note this replaces) allocates GL resources inside
  /// projectm_create(), which crashes with no current context -- exactly
  /// what calling this eagerly at processor-construction time did.
  void ensureInstanceCreated();

  /// Call from a renderer's openGLContextClosing(), i.e. with the same GL
  /// context that was current for the matching ensureInstanceCreated() still
  /// current -- destroying GL resources needs a current context too. A
  /// no-op if there is no instance. Leaves the RenderEngine in the same
  /// permanently-valid, isAvailable()==false state a load failure would.
  void releaseInstance();

private:
  RenderEngine(std::unique_ptr<ProjectMLibrary> library, std::string unavailableReason, Config config);

  void drainParameterUpdates();
  void applyParameterUpdate(const ParameterUpdate& update);

  static void presetSwitchFailedTrampoline(const char* filename, const char* message, void* userData);

  std::unique_ptr<ProjectMLibrary> library_;
  std::string unavailableReason_;
  ProjectMHandle instance_ = nullptr;
  Config config_;
  core::SpscQueue<ParameterUpdate, kParameterQueueCapacity> parameterQueue_;
  std::vector<float> pcmScratch_;
  PresetSwitchFailedCallback presetSwitchFailedCallback_;

  juce::OpenGLContext glContext_;
  int glContextCreationCount_ = 0;
};

static_assert(std::is_trivially_copyable_v<RenderEngine::ParameterUpdate>);

} // namespace milkdawp::engine
