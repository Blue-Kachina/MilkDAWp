// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

#include "milkdawp/core/AudioRing.h"
#include "milkdawp/core/Messages.h"
#include "milkdawp/core/SeqlockSnapshot.h"
#include "milkdawp/engine/FrameReadbackExchange.h"
#include "milkdawp/engine/LayerChannel.h"
#include "milkdawp/engine/PresetHandoff.h"
#include "milkdawp/engine/ProjectMLibrary.h"
#include "milkdawp/engine/RecentErrors.h"
#include "milkdawp/engine/TransitionExecutor.h"

namespace milkdawp::engine {

class OffscreenGLContext;

/// Render-thread statistics (Phase 2.8), published once per rendered frame
/// through a lock-free snapshot for the UI and diagnostics.
struct RenderStats {
  bool running = false;          // the render thread has a context and a projectM instance
  bool paused = true;            // no visible surface: no GPU work this moment (2.10)
  std::uint64_t framesRendered = 0;
  float framesPerSecond = 0.0f;  // measured over the last second of rendering
  float cpuFrameMs = 0.0f;       // render call + glFinish, wall clock
  float gpuFrameMs = -1.0f;      // GL_TIME_ELAPSED of the render call; -1 when unavailable
  int width = 0;                 // current FBO size
  int height = 0;
  int surfaces = 0;              // attached surfaces (visible or not)
  int layers = 1;                // projectM layers being rendered (Layers; 1 for a solo engine)
  std::uint32_t currentPresetId = 0;
  std::uint32_t presetsLoaded = 0;
  std::uint32_t presetsFailed = 0;
  float lastPresetLoadMs = 0.0f;  // projectM parse + shader compile, on the render thread
  float qualityScale = 1.0f;      // the FBO scale in use (5.3)
  int gpuSharers = 1;             // engines in this process rendering right now, this one included (5.6)
  bool qualityAuto = true;        // chosen adaptively (Auto) rather than fixed
  std::int64_t lastLandingErrorSamples = 0;
};

/// RenderEngine::Config. At namespace scope because `create()` defaults it
/// with `= {}`: GCC and Clang reject that for a nested class with default
/// member initializers (CWG 1397), though MSVC accepts it.
struct RenderEngineConfig {
  std::int32_t fps = 60;
  std::size_t meshWidth = 48;
  std::size_t meshHeight = 32;
  int initialWidth = 1280;  // FBO size before any surface reports its size
  int initialHeight = 720;
  int maxDimension = 4096;  // cap on either FBO dimension
  double sampleRate = 48000.0; // for TransitionExecutor's soft-cut early issue
  /// Given to every projectM instance (6.1): normally
  /// BundledContent::textureSearchPaths(), set by the shell.
  std::vector<std::string> textureSearchPaths;
};

/// Owns the GL context, the render thread and the projectM instance for the
/// lifetime of the processor or app (§4.5, Phases 2.2/2.3/2.10).
///
/// The context is an `OffscreenGLContext` that belongs to no window, created
/// on the engine's own render thread. projectM renders into a triple-buffered
/// set of textures there; `OutputSurface`s are separate JUCE GL contexts that
/// share with it (`sharedContextHandle()`) and draw the most recently
/// published texture (`latestFrame()`). So opening, closing and moving
/// windows never touches projectM's state (§2.4's v1 bug), and several
/// windows show the same frame (the primary-window mirror + Output window).
/// A surface that cannot share (a refusing driver, Linux EGL, Android) gets
/// CPU copies of the frames instead (addReadbackClient()).
///
/// Paused when no surface is visible: the thread keeps the context and the
/// projectM instance (preset, playlist position and visual state all
/// survive), but renders nothing until a surface becomes visible (2.10).
///
/// Threading (§4.2):
///   - render thread (internal): everything GL, projectM, preset loading;
///   - director thread: pushTransition(), presetHandoff() (single producer);
///   - any thread: setBeatSensitivity(), setQualityScale(), stats(),
///     surface registration and size reports, latestFrame().
class RenderEngine {
public:
  using Config = RenderEngineConfig;

  /// Loads projectM and starts the render thread. Never returns null: if
  /// projectM or a GL context is unavailable the engine stays valid and
  /// inert, and unavailableReason() says why (§2.6). `audio` must outlive
  /// the engine; the render thread reads it (its own cursor, PcmFeeder).
  [[nodiscard]] static std::unique_ptr<RenderEngine> create(const core::AudioRing& audio, const Config& config = {},
                                                             const juce::File& bundleDirectoryHint = {});

  ~RenderEngine();
  RenderEngine(const RenderEngine&) = delete;
  RenderEngine& operator=(const RenderEngine&) = delete;

  /// True once the render thread has a GL context and a projectM instance.
  [[nodiscard]] bool isAvailable() const noexcept { return available_.load(std::memory_order_acquire); }
  /// Empty while starting or when available.
  [[nodiscard]] std::string unavailableReason() const;
  /// GL vendor / renderer / version of the engine's context, once created.
  [[nodiscard]] std::string glDescription() const;
  [[nodiscard]] std::string projectMVersion() const;
  [[nodiscard]] RenderStats stats() const noexcept { return stats_.read(); }
  /// Recent errors (5.9): projectM's error log on this engine's render thread, the
  /// presets it rejected, and the director's preset read failures. Any thread.
  [[nodiscard]] RecentErrors& errors() noexcept { return errors_; }
  [[nodiscard]] const RecentErrors& errors() const noexcept { return errors_; }

  // ---- settings (any thread) ----
  void setBeatSensitivity(float sensitivity) noexcept { beatSensitivity_.store(sensitivity); }
  /// FBO resolution relative to the largest visible surface, clamped to
  /// [0.25, 1]. 0 or less: adaptive (5.3, `core::AdaptiveQuality`), the
  /// render thread picks it from its measured GPU time.
  void setQualityScale(float scale) noexcept { qualityScale_.store(scale); }
  /// Sample rate of the audio clock `dueAtSample` values use.
  void setSampleRate(double sampleRate) noexcept { sampleRate_.store(sampleRate); }

  // ---- director thread (single producer) ----
  /// Both address the engine's primary layer, the one a solo `Visualizer`
  /// drives (layers_like_shrek.md L0).
  bool pushTransition(const core::TransitionRequestMessage& request) noexcept {
    return primary_.pushTransition(request);
  }
  [[nodiscard]] PresetHandoff& presetHandoff() noexcept { return primary_.presetHandoff(); }
  [[nodiscard]] LayerChannel& primaryLayer() noexcept { return primary_; }

  // ---- layers (Layers L0/L2, layers_like_shrek.md §3) ----
  /// The most layers one engine renders, the primary included.
  static constexpr int kMaxLayers = 8;
  /// Adds another projectM instance to this engine's context, fed by `channel`
  /// and drawn over the layers below it (by `channel.order()`). Blocks until
  /// the render thread has created it. False if the engine is not running,
  /// already has `kMaxLayers`, or `channel` is already attached. Not for the
  /// render thread. `channel` and the ring it reads must outlive the layer,
  /// i.e. stay alive until `removeLayer()` returns.
  bool addLayer(LayerChannel& channel);
  /// Detaches `channel`'s layer, blocking until the render thread has destroyed
  /// it and can no longer touch the channel. A no-op if it is not attached (and
  /// the primary layer cannot be removed). Not for the render thread.
  void removeLayer(LayerChannel& channel);
  /// Makes this engine stop rendering and consuming its own primary layer
  /// (it idles like an engine with no visible surface), so that layer's
  /// `primaryLayer()` channel can be attached to *another* engine with
  /// `addLayer()`: a channel's queues have a single consumer, so two render
  /// threads must never drain it. `yield(true)` blocks until the render thread
  /// has stopped touching the channel (or the engine is not running); pair it
  /// with `yield(false)` after the channel has been removed from the other
  /// engine. Not for the render thread.
  void yieldPrimaryLayer(bool yield);
  [[nodiscard]] bool isPrimaryLayerYielded() const noexcept { return primaryYielded_.load(); }
  // With a single layer the output is that layer as is; with several it is
  // mixed by `LayerCompositor` (opacity, blend, order and visibility come from
  // each layer's `LayerChannel`).

  // ---- surfaces (any thread) ----
  static constexpr int kMaxSurfaces = 8;
  /// Returns a slot id, or -1 if all slots are taken.
  int registerSurface() noexcept;
  void unregisterSurface(int slot) noexcept;
  /// Pixel size the surface draws at, and whether it is on screen. The FBO
  /// follows the largest visible surface; with none visible, the engine
  /// pauses.
  void reportSurfaceSize(int slot, int width, int height, bool visible) noexcept;

  /// Native handle of the engine's context; null until the render thread
  /// has created it (surfaces wait for it), or if that failed.
  [[nodiscard]] void* sharedContextHandle() const noexcept {
    return sharedContextHandle_.load(std::memory_order_acquire);
  }

  /// Call from a surface's newOpenGLContextCreated(), with the surface's
  /// fresh context current and no GL objects created in it yet. Briefly
  /// parks the render thread with its context released, links the two
  /// contexts (OffscreenGLContext::shareWithCurrentContext) and resumes.
  /// Returns false if the engine has no context or the driver refused.
  bool shareIntoCurrentContext();

  struct Frame {
    std::uint32_t texture = 0; // a texture name in the shared context
    int width = 0;
    int height = 0;
    std::uint64_t number = 0;  // increments per published frame
  };
  /// The most recently completed frame, or nullopt before the first one.
  /// The texture stays valid (same name, possibly new contents) for the
  /// engine's lifetime; triple buffering keeps it from being overwritten
  /// for at least one further frame.
  [[nodiscard]] std::optional<Frame> latestFrame() const noexcept;

  /// A texture name that exists in the engine's context from startup (0
  /// before). A surface checks glIsTexture() on it right after its own
  /// context is created to confirm the share worked.
  [[nodiscard]] std::uint32_t probeTextureName() const noexcept {
    return sharedContextHandle() != nullptr ? textureNames_[0].load(std::memory_order_relaxed) : 0;
  }

  // ---- readback fallback (any thread; ADR-0009, ADR-0010) ----
  /// A surface whose context cannot see the engine's textures registers as a
  /// readback client. While there is at least one, the render thread also
  /// copies every frame to the CPU (two pixel-pack buffers, one frame of
  /// latency) and publishes it through latestReadbackFrame(). Balanced calls.
  void addReadbackClient() noexcept { readbackClients_.fetch_add(1); }
  void removeReadbackClient() noexcept { readbackClients_.fetch_sub(1); }
  /// The newest CPU copy, pinned until the returned lock goes away; empty
  /// when there are no readback clients or no frame yet.
  [[nodiscard]] FrameReadbackExchange::ReadLock latestReadbackFrame() const { return readback_.acquireLatest(); }

private:
  RenderEngine(const core::AudioRing& audio, const Config& config, std::shared_ptr<const ProjectMLibrary> library,
               std::string unavailableReason);

  void run();
  void setUnavailable(std::string reason);

  static constexpr std::size_t kFrameCount = 3;

  struct SurfaceSlot {
    std::atomic<bool> used{false};
    std::atomic<std::uint32_t> packedSize{0}; // width << 16 | height
    std::atomic<bool> visible{false};
  };

  const Config config_;
  std::shared_ptr<const ProjectMLibrary> library_; // shared by every engine in the process (5.6)

  mutable std::mutex textMutex_;
  std::string unavailableReason_;
  std::string glDescription_;

  std::atomic<bool> available_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> primaryYielded_{false};
  std::atomic<bool> primaryIdle_{false}; // the render thread's acknowledgement of primaryYielded_
  std::atomic<float> beatSensitivity_{1.0f};
  std::atomic<float> qualityScale_{1.0f};
  std::atomic<double> sampleRate_;

  LayerChannel primary_;

  // addLayer()/removeLayer() requests, applied by the render thread between
  // frames. `layerOpsClosed_` is set (under the mutex) when the render thread
  // ends, after which requests are refused instead of queued.
  struct LayerOp {
    enum class Kind { Add, Remove } kind;
    LayerChannel* channel;
    std::promise<bool> result;
  };
  bool submitLayerOp(LayerOp::Kind kind, LayerChannel& channel);
  std::mutex layerOpsMutex_;
  std::vector<LayerOp> layerOps_;
  bool layerOpsClosed_ = false;

  std::array<SurfaceSlot, kMaxSurfaces> surfaces_;
  std::atomic<void*> sharedContextHandle_{nullptr};

  // Published frame: bits 0-1 texture index, 2-17 width, 18-33 height,
  // 34-63 frame number.
  std::atomic<std::uint64_t> publishedFrame_{0};
  std::array<std::atomic<std::uint32_t>, kFrameCount> textureNames_{};

  std::atomic<int> readbackClients_{0};
  FrameReadbackExchange readback_;

  // Share handshake (shareIntoCurrentContext): one requester at a time; the
  // render thread releases its context while shareRequested_ is set.
  void serviceShareRequest(OffscreenGLContext& context);
  std::mutex shareCallerMutex_;
  std::mutex shareMutex_;
  std::condition_variable shareCondition_;
  bool shareRequested_ = false;
  bool contextReleased_ = false;
  bool renderThreadExited_ = false;

  RecentErrors errors_;
  core::SeqlockSnapshot<RenderStats> stats_;
  std::thread thread_;
};

} // namespace milkdawp::engine
