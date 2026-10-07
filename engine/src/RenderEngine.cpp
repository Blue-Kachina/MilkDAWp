// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/RenderEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/core/AdaptiveQuality.h"
#include "milkdawp/core/LayerGate.h"
#include "milkdawp/core/MilkdawpPreset.h"
#include "milkdawp/core/PresetClock.h"
#include "milkdawp/engine/EffectsChain.h"
#include "milkdawp/engine/GlFrameTarget.h"
#include "milkdawp/engine/LayerCompositor.h"
#include "milkdawp/engine/MediaSource.h"
#include "milkdawp/engine/OffscreenGLContext.h"
#include "milkdawp/engine/PcmFeeder.h"
#include "milkdawp/engine/ProjectMInstance.h"

namespace milkdawp::engine {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t packFrame(std::size_t index, int width, int height, std::uint64_t number) noexcept {
  return (static_cast<std::uint64_t>(index) & 0x3U) | ((static_cast<std::uint64_t>(width) & 0xFFFFU) << 2U) |
         ((static_cast<std::uint64_t>(height) & 0xFFFFU) << 18U) | (number << 34U);
}

constexpr std::uint32_t packSize(int width, int height) noexcept {
  return (static_cast<std::uint32_t>(std::clamp(width, 0, 0xFFFF)) << 16U) |
         static_cast<std::uint32_t>(std::clamp(height, 0, 0xFFFF));
}

double millisecondsBetween(Clock::time_point from, Clock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

// 5.6: what each engine in the process costs the GPU per frame right now, so
// several plugin instances rendering at once budget against their combined
// load (AdaptiveQuality::sharedBudgetMs) instead of each assuming the whole
// GPU is its own. One slot per running render thread; an engine that is
// paused (no visible surface, yielded primary) is not rendering and costs 0.
// Plain atomics: each thread writes only its own slot and reads the others.
class GpuShare {
public:
  static constexpr std::size_t kSlots = 32;

  /// Claims a slot for the calling render thread; none (an engine past the
  /// 32nd) means it budgets as if alone.
  GpuShare() {
    for (std::size_t i = 0; i < kSlots; ++i) {
      bool expected = false;
      if (slots()[i].claimed.compare_exchange_strong(expected, true)) {
        slot_ = &slots()[i];
        return;
      }
    }
  }
  ~GpuShare() {
    if (slot_ != nullptr) {
      setIdle();
      slot_->claimed.store(false);
    }
  }
  GpuShare(const GpuShare&) = delete;
  GpuShare& operator=(const GpuShare&) = delete;

  void setIdle() noexcept {
    if (slot_ != nullptr) {
      slot_->rendering.store(false);
      slot_->costMs.store(0.0f);
    }
  }
  void publish(float costMs) noexcept {
    if (slot_ != nullptr) {
      slot_->costMs.store(costMs);
      slot_->rendering.store(true);
    }
  }

  struct Others {
    float costMs = 0.0f;
    int sharers = 1; // rendering engines, the caller included
  };
  [[nodiscard]] Others others() const noexcept {
    Others result;
    for (const auto& slot : slots()) {
      if (&slot != slot_ && slot.claimed.load() && slot.rendering.load()) {
        result.costMs += slot.costMs.load();
        ++result.sharers;
      }
    }
    return result;
  }

private:
  struct Slot {
    std::atomic<bool> claimed{false};
    std::atomic<bool> rendering{false};
    std::atomic<float> costMs{0.0f};
  };
  static std::array<Slot, kSlots>& slots() noexcept {
    static std::array<Slot, kSlots> table;
    return table;
  }

  Slot* slot_ = nullptr;
};

// projectM calls this synchronously from inside a load call when it rejects
// the preset; the render loop checks the flag right after the call.
struct LoadFailureFlag {
  bool failed = false;
  std::string message; // projectM's reason, for the recent-errors log (5.9)
};

void onPresetSwitchFailed(const char*, const char* message, void* userData) {
  auto& flag = *static_cast<LoadFailureFlag*>(userData);
  flag.failed = true;
  flag.message = message != nullptr ? message : "";
}

// projectM's own log, error level only, registered per render thread (5.9):
// shader compile errors and the like land in that engine's recent errors.
void onProjectMLog(const char* message, int, void* userData) {
  if (message != nullptr) {
    static_cast<RecentErrors*>(userData)->add("projectM", message);
  }
}

// A media source's newest frame as a GL texture (8.6), uploaded only when the
// source hands over a new one. Render thread only, with the context current.
class MediaTexture {
public:
  MediaTexture() = default;
  ~MediaTexture() { release(); }
  MediaTexture(const MediaTexture&) = delete;
  MediaTexture& operator=(const MediaTexture&) = delete;

  /// Follows `source` (null: none). False while there is nothing to show.
  bool update(const std::shared_ptr<MediaSource>& source) {
    using namespace ::juce::gl;
    if (source == nullptr) {
      release();
      return false;
    }
    std::uint64_t serial = 0;
    const auto frame = source->latestFrame(serial);
    if (frame == nullptr || serial == 0 || frame->width <= 0 || frame->height <= 0) {
      return false;
    }
    if (source == source_ && serial == serial_) {
      return true;
    }
    if (texture_ == 0) {
      glGenTextures(1, &texture_);
      glBindTexture(GL_TEXTURE_2D, texture_);
      // Mipmapped: Displace reads a smoothed copy, and big images shrink cleanly.
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glBindTexture(GL_TEXTURE_2D, texture_);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (frame->width == width_ && frame->height == height_) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, frame->rgba.data());
    } else {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame->width, frame->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   frame->rgba.data());
      width_ = frame->width;
      height_ = frame->height;
    }
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    source_ = source;
    serial_ = serial;
    return true;
  }

  /// How it goes over a `width` x `height` picture at `opacity` with `blend`: filling it,
  /// centred, cropped rather than stretched, its own transparency kept.
  [[nodiscard]] LayerDraw draw(float opacity, LayerBlend blend, int width, int height) const {
    const auto uv = coverUvScale(width_, height_, width, height);
    return {texture_, opacity, blend, true, uv.x, uv.y};
  }
  /// For Burn in (8.6f), which hands the texture to projectM as is.
  [[nodiscard]] std::uint32_t texture() const noexcept { return texture_; }

private:
  void release() {
    if (texture_ != 0) {
      ::juce::gl::glDeleteTextures(1, &texture_);
    }
    texture_ = 0;
    width_ = 0;
    height_ = 0;
    source_.reset();
    serial_ = 0;
  }

  std::uint32_t texture_ = 0; // a GLuint
  int width_ = 0;
  int height_ = 0;
  std::shared_ptr<MediaSource> source_;
  std::uint64_t serial_ = 0;
};

// Render-thread state for one layer: its projectM instance, the cursor it
// reads its channel's audio with, and its load-failure flag. Always held by
// unique_ptr: projectM keeps a pointer to `loadFailure`, so a Layer must never
// move. The channel is the director-side half (LayerChannel.h) and outlives it.
struct Layer {
  Layer(LayerChannel& channelRef, std::unique_ptr<ProjectMInstance> instanceRef, float beatSensitivity)
      : channel(channelRef), instance(std::move(instanceRef)),
        feeder(std::max(instance->maxPcmSamples(), 1U), channelRef.audio().numChannels()),
        appliedBeatSensitivity(beatSensitivity) {
    instance->setPresetSwitchFailedCallback(&onPresetSwitchFailed, &loadFailure);
  }
  Layer(const Layer&) = delete;
  Layer& operator=(const Layer&) = delete;

  LayerChannel& channel;
  std::unique_ptr<ProjectMInstance> instance;
  PcmFeeder feeder;
  LoadFailureFlag loadFailure;
  float appliedBeatSensitivity;
  // Where this layer draws before the compositor mixes it or effects run over
  // it. A lone layer with neutral effects and an open gate never makes one: it
  // draws straight into the output.
  std::unique_ptr<GlFrameTarget> target;
  // Phase 8: this frame's Visual globals, the effects over this layer (or, for
  // the primary layer, over the whole canvas), and where they draw when the
  // result still has to be mixed.
  core::VisualControls visual;
  EffectsState effects;
  std::unique_ptr<GlFrameTarget> fxTarget;
  // 8.3: the clock this layer's preset runs on, so Speed never jumps it.
  core::PresetClock clock;
  // 8.10: whether the preset showing was compiled from a .milkdawp (it then
  // takes Zoom and Rotation itself, not as post effects), and the smoothed
  // values every preset is given as mdw_* variables each frame.
  bool milkdawp = false;
  core::VisualControls presetVisual;
  std::array<float, core::kMacroCount> macros{};
  // 8.2b: the gate, and the level it listens to. Hosts with large buffers
  // deliver audio less often than once a frame, so a frame with no new audio
  // keeps the last level for a while instead of counting as silence.
  core::LayerGate gate;
  float lastPeakDb = -200.0f;
  float secondsWithoutAudio = 0.0f;
  float gateEnvelope = 1.0f;
  // 8.6: the layer's media source on the GPU, and whether Media Mix shows it this frame.
  MediaTexture media;
  bool mediaShown = false;
  // Burn in (8.6f): drawn into the preset rather than over it, through urnTarget.
  bool mediaBurns = false;
  std::unique_ptr<GlFrameTarget> burnTarget;
};

// How long a layer's level holds when no new audio arrives, before it counts as
// silence (the transport stopped, or the host stopped calling).
constexpr float kAudioGapSeconds = 0.15f;

// The readback fallback's GPU half (ADR-0009): two pixel-pack buffers used in
// turn. Each frame's glReadPixels goes into one while the other, filled a
// frame earlier and completed by that frame's glFinish, is mapped and copied
// into the exchange. So readback never stalls on the frame just rendered, at
// the cost of one frame of latency on readback surfaces. All calls on the
// render thread with its context current; also valid in OpenGL ES 3.0.
class PixelPackReadback {
public:
  PixelPackReadback() {
    using namespace ::juce::gl;
    std::array<GLuint, 2> names{};
    glGenBuffers(2, names.data());
    slots_[0].buffer = names[0];
    slots_[1].buffer = names[1];
  }

  ~PixelPackReadback() {
    using namespace ::juce::gl;
    std::array<GLuint, 2> names{slots_[0].buffer, slots_[1].buffer};
    glDeleteBuffers(2, names.data());
  }

  PixelPackReadback(const PixelPackReadback&) = delete;
  PixelPackReadback& operator=(const PixelPackReadback&) = delete;

  void readAndPublish(const GlFrameTarget& target, std::uint64_t frameNumber, FrameReadbackExchange& exchange) {
    using namespace ::juce::gl;
    auto& write = slots_[writeIndex_];
    write.width = target.width();
    write.height = target.height();
    write.number = frameNumber;
    const auto bytes = byteSize(write);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, write.buffer);
    if (write.capacity < bytes) {
      glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_READ);
      write.capacity = bytes;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.framebuffer());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, write.width, write.height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    write.pending = true;

    writeIndex_ ^= 1U;
    auto& read = slots_[writeIndex_];
    if (read.pending) {
      read.pending = false;
      glBindBuffer(GL_PIXEL_PACK_BUFFER, read.buffer);
      const auto readBytes = byteSize(read);
      const auto* pixels = static_cast<const std::uint8_t*>(
          glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(readBytes), GL_MAP_READ_BIT));
      if (pixels != nullptr) {
        if (auto* destination = exchange.beginWrite(read.width, read.height)) {
          std::memcpy(destination, pixels, readBytes);
          exchange.commitWrite(read.number);
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
      }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  }

private:
  struct Slot {
    GLuint buffer = 0;
    std::size_t capacity = 0;
    int width = 0;
    int height = 0;
    std::uint64_t number = 0;
    bool pending = false;
  };

  static std::size_t byteSize(const Slot& slot) noexcept {
    return static_cast<std::size_t>(slot.width) * static_cast<std::size_t>(slot.height) * 4;
  }

  std::array<Slot, 2> slots_{};
  unsigned writeIndex_ = 0;
};

} // namespace

std::unique_ptr<RenderEngine> RenderEngine::create(const core::AudioRing& audio, const Config& config,
                                                   const juce::File& bundleDirectoryHint) {
  auto loaded = ProjectMLibrary::acquireShared(bundleDirectoryHint);
  std::unique_ptr<RenderEngine> engine(
      new RenderEngine(audio, config, std::move(loaded.library), std::move(loaded.unavailableReason)));
  if (engine->library_) {
    engine->thread_ = std::thread([raw = engine.get()] { raw->run(); });
  }
  return engine;
}

RenderEngine::RenderEngine(const core::AudioRing& audio, const Config& config,
                           std::shared_ptr<const ProjectMLibrary> library, std::string unavailableReason)
    : config_(config), library_(std::move(library)), unavailableReason_(std::move(unavailableReason)),
      sampleRate_(config.sampleRate), primary_(audio, config.sampleRate) {}

RenderEngine::~RenderEngine() {
  stopRequested_.store(true);
  {
    const std::lock_guard lock(shareMutex_);
    shareCondition_.notify_all();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::string RenderEngine::unavailableReason() const {
  const std::lock_guard lock(textMutex_);
  return unavailableReason_;
}

std::string RenderEngine::glDescription() const {
  const std::lock_guard lock(textMutex_);
  return glDescription_;
}

std::string RenderEngine::projectMVersion() const { return library_ ? library_->versionString() : std::string{}; }

void RenderEngine::setUnavailable(std::string reason) {
  const std::lock_guard lock(textMutex_);
  unavailableReason_ = std::move(reason);
}

bool RenderEngine::submitLayerOp(LayerOp::Kind kind, LayerChannel& channel) {
  std::future<bool> outcome;
  {
    const std::lock_guard lock(layerOpsMutex_);
    if (layerOpsClosed_ || !thread_.joinable()) {
      return false;
    }
    layerOps_.push_back({kind, &channel, {}});
    outcome = layerOps_.back().result.get_future();
  }
  // The render thread answers every queued request, including on its way out.
  return outcome.get();
}

bool RenderEngine::addLayer(LayerChannel& channel) {
  if (!isAvailable()) {
    return false;
  }
  return submitLayerOp(LayerOp::Kind::Add, channel);
}

void RenderEngine::removeLayer(LayerChannel& channel) {
  if (&channel == &primary_) {
    return;
  }
  submitLayerOp(LayerOp::Kind::Remove, channel);
}

void RenderEngine::yieldPrimaryLayer(bool yield) {
  primaryYielded_.store(yield);
  if (!yield || !isAvailable()) {
    return;
  }
  // The render thread checks the flag at the top of every iteration (at worst
  // a slow frame or a 20 ms idle sleep apart). Give up if it exits instead.
  for (int i = 0; i < 500 && !primaryIdle_.load() && isAvailable(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

int RenderEngine::registerSurface() noexcept {
  for (int i = 0; i < kMaxSurfaces; ++i) {
    bool expected = false;
    auto& slot = surfaces_[static_cast<std::size_t>(i)];
    if (slot.used.compare_exchange_strong(expected, true)) {
      slot.packedSize.store(0);
      slot.visible.store(false);
      return i;
    }
  }
  return -1;
}

void RenderEngine::unregisterSurface(int slot) noexcept {
  if (slot < 0 || slot >= kMaxSurfaces) {
    return;
  }
  auto& entry = surfaces_[static_cast<std::size_t>(slot)];
  entry.visible.store(false);
  entry.used.store(false);
}

void RenderEngine::reportSurfaceSize(int slot, int width, int height, bool visible) noexcept {
  if (slot < 0 || slot >= kMaxSurfaces) {
    return;
  }
  auto& entry = surfaces_[static_cast<std::size_t>(slot)];
  entry.packedSize.store(packSize(width, height));
  entry.visible.store(visible && width > 0 && height > 0);
}

bool RenderEngine::shareIntoCurrentContext() {
  void* handle = sharedContextHandle();
  if (handle == nullptr) {
    return false;
  }
  const std::lock_guard caller(shareCallerMutex_);
  std::unique_lock lock(shareMutex_);
  shareRequested_ = true;
  contextReleased_ = false;
  shareCondition_.notify_all();
  const bool released = shareCondition_.wait_for(lock, std::chrono::seconds(2), [this] {
    return contextReleased_ || renderThreadExited_;
  });
  bool shared = false;
  if (released && contextReleased_) {
    shared = OffscreenGLContext::shareWithCurrentContext(handle);
  }
  shareRequested_ = false;
  shareCondition_.notify_all();
  return shared;
}

void RenderEngine::serviceShareRequest(OffscreenGLContext& context) {
  std::unique_lock lock(shareMutex_);
  if (!shareRequested_) {
    return;
  }
  context.doneCurrent();
  contextReleased_ = true;
  shareCondition_.notify_all();
  shareCondition_.wait(lock, [this] { return !shareRequested_ || stopRequested_.load(); });
  contextReleased_ = false;
  context.makeCurrent();
}

std::optional<RenderEngine::Frame> RenderEngine::latestFrame() const noexcept {
  const auto packed = publishedFrame_.load(std::memory_order_acquire);
  const auto number = packed >> 34U;
  if (number == 0) {
    return std::nullopt;
  }
  Frame frame;
  frame.texture = textureNames_[static_cast<std::size_t>(packed & 0x3U)].load(std::memory_order_relaxed);
  frame.width = static_cast<int>((packed >> 2U) & 0xFFFFU);
  frame.height = static_cast<int>((packed >> 18U) & 0xFFFFU);
  frame.number = number;
  return frame;
}

void RenderEngine::run() {
  using namespace ::juce::gl;
  juce::Thread::setCurrentThreadName("MilkDAWp render");
  // Wakes any surface waiting in shareIntoCurrentContext() when this thread
  // ends, whichever way it ends.
  struct ExitSignal {
    RenderEngine& engine;
    ~ExitSignal() {
      {
        const std::lock_guard lock(engine.shareMutex_);
        engine.renderThreadExited_ = true;
        engine.shareCondition_.notify_all();
      }
      // Anyone blocked in addLayer()/removeLayer() gets an answer, and later
      // requests are refused rather than queued for a thread that is gone.
      const std::lock_guard lock(engine.layerOpsMutex_);
      engine.layerOpsClosed_ = true;
      for (auto& op : engine.layerOps_) {
        op.result.set_value(false);
      }
      engine.layerOps_.clear();
    }
  } exitSignal{*this};

  auto created = OffscreenGLContext::create();
  if (!created.context) {
    setUnavailable("no render context: " + created.error);
    return;
  }
  auto& context = *created.context;
  {
    const std::lock_guard lock(textMutex_);
    glDescription_ = context.description();
  }
  // juce::gl's entry points are process-wide; loading them here resolves
  // the same driver functions JUCE resolves for its own contexts.
  juce::gl::loadFunctions();

  std::array<std::unique_ptr<GlFrameTarget>, kFrameCount> targets;
  for (std::size_t i = 0; i < kFrameCount; ++i) {
    targets[i] = std::make_unique<GlFrameTarget>(config_.initialWidth, config_.initialHeight);
    textureNames_[i].store(targets[i]->texture(), std::memory_order_relaxed);
  }

  ProjectMInstance::Settings settings;
  settings.meshWidth = config_.meshWidth;
  settings.meshHeight = config_.meshHeight;
  settings.fps = config_.fps;
  settings.width = config_.initialWidth;
  settings.height = config_.initialHeight;
  settings.beatSensitivity = beatSensitivity_.load();
  settings.textureSearchPaths = config_.textureSearchPaths;
  std::string error;
  auto primaryInstance = ProjectMInstance::create(*library_, settings, error);
  if (!primaryInstance) {
    targets = {};
    setUnavailable(error);
    return;
  }
  // This thread's projectM log goes to this engine's recent errors. Per
  // thread, so each plugin instance hears only its own instances (5.6/5.9).
  library_->functions().setLogCallback(&onProjectMLog, /*currentThreadOnly=*/true, &errors_);
  library_->functions().setLogLevel(static_cast<int>(ProjectMLogLevel::Error), /*currentThreadOnly=*/true);
  struct LogUnregister {
    const ProjectMFunctions& fn;
    ~LogUnregister() { fn.setLogCallback(nullptr, true, nullptr); }
  } logUnregister{library_->functions()};
  // The layers this thread renders. Exactly one for now, the primary one a
  // solo Visualizer drives; the loops below already run per layer.
  std::vector<std::unique_ptr<Layer>> layers;
  layers.push_back(std::make_unique<Layer>(primary_, std::move(primaryInstance), settings.beatSensitivity));
  Layer& primary = *layers.front();
  // Mixes the layers once there are several; a lone layer never uses it. If it
  // fails to build, several layers degrade to showing the primary one.
  LayerCompositor compositor;
  // Phase 8.2: the Visual globals' post effects. If its shaders don't build,
  // pictures go out without effects and the reason is in the recent errors.
  EffectsChain effectsChain;
  if (!effectsChain.ok()) {
    errors_.add("effects", effectsChain.error());
  }
  // The mixed canvas, when the primary layer's effects run over it.
  std::unique_ptr<GlFrameTarget> canvasTarget;

  // addLayer()/removeLayer() requests, answered between frames. Creating and
  // destroying a projectM instance needs this thread's context, hence here.
  const auto applyLayerOps = [&] {
    std::vector<LayerOp> ops;
    {
      const std::lock_guard lock(layerOpsMutex_);
      if (layerOps_.empty()) {
        return;
      }
      ops.swap(layerOps_);
    }
    for (auto& op : ops) {
      bool done = false;
      const auto existing = std::find_if(layers.begin(), layers.end(),
                                         [&](const auto& layer) { return &layer->channel == op.channel; });
      if (op.kind == LayerOp::Kind::Add) {
        if (existing == layers.end() && layers.size() < static_cast<std::size_t>(kMaxLayers)) {
          ProjectMInstance::Settings added = settings;
          added.width = primary.instance->width();
          added.height = primary.instance->height();
          added.beatSensitivity = beatSensitivity_.load();
          std::string layerError;
          if (auto instance = ProjectMInstance::create(*library_, added, layerError)) {
            layers.push_back(std::make_unique<Layer>(*op.channel, std::move(instance), added.beatSensitivity));
            done = true;
          }
        }
      } else if (existing != layers.end() && existing->get() != &primary) {
        layers.erase(existing); // its instance and target go with this thread's context current
        done = true;
      }
      op.result.set_value(done);
    }
  };

  GLuint timerQuery = 0;
  glGenQueries(1, &timerQuery);
  // One per layer plus one for the mix: see the render section.
  std::array<GLuint, kMaxLayers + 1> layerQueries{};
  glGenQueries(static_cast<GLsizei>(layerQueries.size()), layerQueries.data());
  std::unique_ptr<PixelPackReadback> pixelPack; // only while a surface needs readback

  RenderStats stats;
  stats.running = true;
  stats.width = primary.instance->width();
  stats.height = primary.instance->height();
  stats_.publish(stats);

  // Surfaces may attach from here on: textures exist and the handle is live.
  sharedContextHandle_.store(context.nativeShareHandle(), std::memory_order_release);
  available_.store(true, std::memory_order_release);

  const auto frameInterval = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(1.0 / std::max(config_.fps, 1)));
  auto nextFrame = Clock::now();
  auto fpsWindowStart = Clock::now();
  std::uint64_t framesInWindow = 0;
  std::size_t lastIndex = 0;
  std::uint64_t frameNumber = 0;

  // Adaptive quality (5.3): drives the FBO scale while `qualityScale_` is Auto.
  core::AdaptiveQuality adaptive;
  const float frameBudgetMs = 1000.0f / static_cast<float>(std::max(config_.fps, 1));
  adaptive.setTargetFps(static_cast<float>(std::max(config_.fps, 1)));
  bool wasAutoQuality = false;
  auto lastFrameEnd = Clock::now();
  // Phase 8: the step every layer's preset clock, gate and effects advance by.
  auto lastFrameStart = Clock::now() - frameInterval;
  // 5.6: this engine's part of the process-wide GPU load, and its own cost
  // smoothed for the others to read (same time constant as AdaptiveQuality).
  GpuShare gpuShare;
  float sharedCostMs = 0.0f;

  while (!stopRequested_.load()) {
    context.pumpPlatformEvents();
    serviceShareRequest(context);
    applyLayerOps();

    int surfaces = 0;
    int widest = 0;
    int tallest = 0;
    bool anyVisible = false;
    for (const auto& slot : surfaces_) {
      if (!slot.used.load()) {
        continue;
      }
      ++surfaces;
      if (!slot.visible.load()) {
        continue;
      }
      anyVisible = true;
      const auto packed = slot.packedSize.load();
      widest = std::max(widest, static_cast<int>(packed >> 16U));
      tallest = std::max(tallest, static_cast<int>(packed & 0xFFFFU));
    }
    stats.surfaces = surfaces;
    stats.layers = static_cast<int>(layers.size());
    const bool multiLayer = layers.size() > 1;

    const bool yielded = primaryYielded_.load();
    primaryIdle_.store(yielded);
    if (!anyVisible || yielded) {
      // 2.10: nothing to show, so no GPU work. Context, instance, preset and
      // visual state all stay as they are until a surface is visible again.
      // (A yielded primary layer idles the same way: see yieldPrimaryLayer.)
      if (!stats.paused) {
        stats.paused = true;
        stats.framesPerSecond = 0.0f;
        stats_.publish(stats);
        gpuShare.setIdle();
        sharedCostMs = 0.0f;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      nextFrame = Clock::now();
      lastFrameStart = nextFrame - frameInterval; // a pause is not time the presets lived through
      fpsWindowStart = nextFrame;
      framesInWindow = 0;
      continue;
    }
    stats.paused = false;

    std::this_thread::sleep_until(nextFrame);
    nextFrame += frameInterval;
    if (Clock::now() - nextFrame > frameInterval * 4) {
      nextFrame = Clock::now(); // fell far behind (a slow preset load): don't try to catch up
    }
    const auto frameStart = Clock::now();
    const float frameDt = std::clamp(static_cast<float>(millisecondsBetween(lastFrameStart, frameStart) / 1000.0),
                                     0.0f, static_cast<float>(core::PresetClock::kMaxStepSeconds));
    lastFrameStart = frameStart;

    const float beatSensitivity = beatSensitivity_.load();
    for (auto& layer : layers) {
      if (beatSensitivity != layer->appliedBeatSensitivity) {
        layer->instance->setBeatSensitivity(beatSensitivity);
        layer->appliedBeatSensitivity = beatSensitivity;
      }
    }

    const float requestedScale = qualityScale_.load();
    const bool autoQuality = requestedScale <= 0.0f;
    if (autoQuality && !wasAutoQuality) {
      adaptive.reset(); // Auto chosen again: start from full scale
    }
    wasAutoQuality = autoQuality;
    const float scale = autoQuality ? adaptive.scale() : std::clamp(requestedScale, 0.25f, 1.0f);
    const int width = std::clamp(static_cast<int>(std::lround(widest * scale)), 16, config_.maxDimension);
    const int height = std::clamp(static_cast<int>(std::lround(tallest * scale)), 16, config_.maxDimension);
    if (width != primary.instance->width() || height != primary.instance->height()) {
      for (auto& target : targets) {
        target->resize(width, height);
      }
      for (auto& layer : layers) {
        layer->instance->setOutputSize(width, height);
      }
    }

    const double sampleRate = sampleRate_.load();
    // Loading a preset parses and compiles shaders synchronously on this thread,
    // so with several layers at most one does it per frame: the others' due
    // transitions simply wait a frame. The layer serviced first rotates, so no
    // layer is starved. (With one layer nothing is ever deferred.)
    bool loadedThisFrame = false;
    const std::size_t layerTotal = layers.size();
    for (std::size_t visit = 0; visit < layerTotal; ++visit) {
      auto& layer = layers[(visit + static_cast<std::size_t>(frameNumber)) % layerTotal];
      auto& channel = layer->channel;
      auto& handoff = channel.presetHandoff();
      handoff.receive();
      channel.executor().setSampleRate(sampleRate);
      if (!loadedThisFrame) {
        channel.executor().onTick(
          static_cast<std::int64_t>(channel.audio().samplePosition()),
          [&](const TransitionExecutor::DueTransition& due) {
            const auto slot = handoff.find(due.request.presetId);
            if (!slot) {
              return; // superseded, or never delivered
            }
            const bool soft = due.request.cutStyle == core::CutStyle::Soft;
            if (soft) {
              const double blend = std::max(static_cast<double>(due.request.blendSeconds), 0.1);
              layer->instance->setSoftCutDuration(blend);
              layer->clock.onSoftCut(blend); // a slow Speed must not stall the blend
            }
            layer->loadFailure.failed = false;
            const auto loadStart = Clock::now();
            const bool milkdawp = handoff.milkdawp(*slot);
            layer->instance->loadPresetData(handoff.text(*slot), soft);
            const auto loadMs = static_cast<float>(millisecondsBetween(loadStart, Clock::now()));
            handoff.release(*slot);
            loadedThisFrame = true;
            channel.reportPresetLoaded(loadMs);
            // RenderStats describe the primary layer until per-layer stats exist.
            const bool isPrimary = layer.get() == &primary;
            if (isPrimary) {
              stats.lastPresetLoadMs = loadMs;
              stats.lastLandingErrorSamples = due.landingErrorSamples;
            }
            if (layer->loadFailure.failed) {
              if (isPrimary) {
                ++stats.presetsFailed;
              }
              // The director adds which file it was when it hears of the failure.
              if (!layer->loadFailure.message.empty()) {
                errors_.add("projectM", layer->loadFailure.message);
              }
              handoff.reportFailure(due.request.presetId);
            } else {
              // On a failed load projectM keeps the old preset, and so do we.
              layer->milkdawp = milkdawp;
              if (isPrimary) {
                ++stats.presetsLoaded;
                stats.currentPresetId = due.request.presetId;
              }
            }
          });
      }

      // A layer that will not be drawn this frame is not fed either: the feeder
      // only ever hands over the newest audio, so it picks up from "now" later.
      // A gated layer is still fed: closing the gate hides it, it doesn't stop it.
      std::size_t fed = 0;
      float peakDb = -200.0f;
      if (!multiLayer || (channel.visible() && channel.opacity() > 0.0f)) {
        fed = layer->feeder.feed(channel.audio(), [&](const float* samples, std::size_t frames, int channels) {
          layer->instance->addPcm(samples, frames, channels);
          peakDb = core::peakDbfs(samples, frames * static_cast<std::size_t>(std::max(channels, 1)));
        });
      }
      if (fed > 0) {
        layer->lastPeakDb = peakDb;
        layer->secondsWithoutAudio = 0.0f;
      } else {
        layer->secondsWithoutAudio += frameDt;
        if (layer->secondsWithoutAudio > kAudioGapSeconds) {
          layer->lastPeakDb = -200.0f;
        }
      }
      layer->gateEnvelope = layer->gate.process(layer->lastPeakDb, frameDt, channel.gate());
      channel.reportGate(layer->lastPeakDb, layer->gate.isOpen(), layer->gateEnvelope);

      layer->visual = channel.visual();
      // 8.10: a .milkdawp preset turns its own zoom and rot (compounding
      // through its feedback, a real tunnel and swirl), so the post effects
      // leave those two out for it. Everything else stays a post effect.
      auto postVisual = layer->visual;
      if (layer->milkdawp) {
        postVisual.zoom = 0.0f;
        postVisual.rotation = 0.0f;
      }
      layer->effects.advance(postVisual, frameDt);
      layer->presetVisual = core::smoothVisualControls(layer->presetVisual, layer->visual, frameDt);
      const auto macroTargets = channel.macros();
      const float macroStep = std::clamp(1.0f - std::exp(-frameDt / 0.05f), 0.0f, 1.0f);
      for (std::size_t k = 0; k < layer->macros.size(); ++k) {
        auto& macro = layer->macros[k];
        macro += (macroTargets[k] - macro) * macroStep;
        if (std::abs(macroTargets[k] - macro) < 1.0e-4f) {
          macro = macroTargets[k]; // exactly: a Macro on its default leaves the preset exactly as it was
        }
      }
      // The texture follows the source even at Media Mix 0, so turning it up is instant.
      const auto mediaSource = channel.mediaSource();
      if (mediaSource != nullptr) {
        mediaSource->setTimeline(channel.mediaTimeline()); // a video follows the host (8.6c)
      }
      layer->mediaShown = layer->media.update(mediaSource) && layer->visual.mediaMix > 0.0f;
      layer->mediaBurns = layer->mediaShown && channel.mediaBlend() == LayerBlend::BurnIn;
      if (layer->mediaBurns) {
        layer->mediaShown = false; // never drawn over the picture
      }
    }

    // Renders one layer's preset into `framebuffer`, on its own clock (8.3).
    const auto renderLayer = [&](Layer& layer, std::uint32_t framebuffer) {
      const double before = layer.clock.time();
      const double now = layer.clock.advance(frameDt, layer.visual.speed);
      layer.instance->setFrameTime(now);
      // 8.10: what compiled .milkdawp code reads (core::MilkdawpPreset.h). Set
      // in every preset: a plain .milk never names them, and the next preset
      // must already have them when its init code runs.
      auto& pm = *layer.instance;
      for (std::size_t k = 0; k < layer.macros.size(); ++k) {
        pm.setPresetVariable(core::kMacroVariables[k], layer.macros[k]);
      }
      pm.setPresetVariable(core::kZoomVariable, layer.presetVisual.zoom);
      pm.setPresetVariable(core::kRotationVariable, layer.presetVisual.rotation);
      pm.setPresetVariable(core::kWarpVariable, layer.presetVisual.warp);
      pm.setPresetVariable(core::kTrailsVariable, layer.presetVisual.trails);
      pm.setPresetVariable(core::kDtVariable, now - before);
      if (layer.mediaBurns) {
        // Every frame, with Media Mix as the burn's alpha. (Burning only every
        // few frames at low Media Mix strobed: the preset faded it in between.)
        // Through a frame-sized copy, so it is cropped to fit like the other modes.
        const int w = layer.instance->width();
        const int h = layer.instance->height();
        if (!layer.burnTarget) {
          layer.burnTarget = std::make_unique<GlFrameTarget>(w, h);
        } else {
          layer.burnTarget->resize(w, h);
        }
        compositor.stamp(*layer.burnTarget, layer.media.draw(layer.visual.mediaMix, LayerBlend::Normal, w, h));
        layer.instance->burnTexture(layer.burnTarget->texture());
      }
      layer.instance->renderTo(framebuffer);
    };

    const std::size_t index = (lastIndex + 1) % kFrameCount;
    const auto renderStart = Clock::now();
    const bool composited = multiLayer && compositor.ok();
    // GL allows one GL_TIME_ELAPSED query at a time: a lone layer is timed as a
    // whole, several are timed layer by layer (and the mix) and summed.
    std::array<Layer*, kMaxLayers> drawn{};
    std::size_t drawnCount = 0;
    const int frameWidth = targets[index]->width();
    const int frameHeight = targets[index]->height();
    const auto frameSized = [&](std::unique_ptr<GlFrameTarget>& target) -> GlFrameTarget& {
      if (!target) {
        target = std::make_unique<GlFrameTarget>(frameWidth, frameHeight);
      } else {
        target->resize(frameWidth, frameHeight);
      }
      return *target;
    };
    if (!composited) {
      // One layer (or the primary alone if the compositor could not be built).
      // With neutral effects and an open gate it draws straight into the
      // output frame (media, if shown, over it there); otherwise through its
      // own target, its media, its effects, then a fade from black by the gate.
      glBeginQuery(GL_TIME_ELAPSED, timerQuery);
      const bool effects = effectsChain.ok() && primary.effects.active();
      const bool fade = compositor.ok() && primary.gateEnvelope < 1.0f;
      const auto overlayMedia = [&](const GlFrameTarget& target) {
        if (primary.mediaShown) {
          const auto draw = primary.media.draw(primary.visual.mediaMix, primary.channel.mediaBlend(), target.width(), target.height());
          compositor.overlay(target, std::span<const LayerDraw>(&draw, 1));
        }
      };
      if (!effects && !fade) {
        renderLayer(primary, targets[index]->framebuffer());
        overlayMedia(*targets[index]);
      } else {
        auto& raw = frameSized(primary.target);
        renderLayer(primary, raw.framebuffer());
        overlayMedia(raw);
        std::uint32_t texture = raw.texture();
        if (effects) {
          const auto& out = fade ? frameSized(primary.fxTarget) : *targets[index];
          effectsChain.apply(primary.effects, texture, out);
          texture = out.texture();
        }
        if (fade) {
          const LayerDraw draw{texture, primary.gateEnvelope, LayerBlend::Normal};
          compositor.compose(*targets[index], std::span<const LayerDraw>(&draw, 1));
        }
      }
      glEndQuery(GL_TIME_ELAPSED);
    } else {
      // Several: each drawn layer renders into its own target, then the
      // compositor mixes them bottom to top into the output frame.
      for (auto& layer : layers) {
        if (layer->channel.visible() && layer->channel.opacity() > 0.0f) {
          // Stable insertion by order(): equal orders keep the order added.
          std::size_t at = drawnCount++;
          while (at > 0 && drawn[at - 1]->channel.order() > layer->channel.order()) {
            drawn[at] = drawn[at - 1];
            --at;
          }
          drawn[at] = layer.get();
        }
      }
      // A sender's effects run over its own layer; the primary layer's (this
      // instance's, which owns the window) run over the mixed canvas below. A
      // gated-out layer still renders, so it is where it should be on reopening,
      // but is left out of the mix.
      std::array<LayerDraw, kMaxLayers> draws{};
      std::size_t drawCount = 0;
      for (std::size_t i = 0; i < drawnCount; ++i) {
        Layer& layer = *drawn[i];
        auto& raw = frameSized(layer.target);
        glBeginQuery(GL_TIME_ELAPSED, layerQueries[i]);
        renderLayer(layer, raw.framebuffer());
        if (&layer != &primary && layer.mediaShown) {
          const auto draw = layer.media.draw(layer.visual.mediaMix, layer.channel.mediaBlend(), raw.width(), raw.height());
          compositor.overlay(raw, std::span<const LayerDraw>(&draw, 1));
        }
        std::uint32_t texture = raw.texture();
        if (&layer != &primary && effectsChain.ok() && layer.effects.active()) {
          auto& out = frameSized(layer.fxTarget);
          effectsChain.apply(layer.effects, texture, out);
          texture = out.texture();
        }
        glEndQuery(GL_TIME_ELAPSED);
        const float opacity = layer.channel.opacity() * layer.gateEnvelope;
        if (opacity > 0.0f) {
          draws[drawCount++] = {texture, opacity, layer.channel.blend()};
        }
      }
      glBeginQuery(GL_TIME_ELAPSED, layerQueries[kMaxLayers]);
      const std::span<const LayerDraw> mix(draws.data(), drawCount);
      // The window owner's media and effects go over the whole mix.
      const bool canvasEffects = effectsChain.ok() && primary.effects.active();
      if (!canvasEffects) {
        canvasTarget.reset();
      }
      const auto& canvas = canvasEffects ? frameSized(canvasTarget) : *targets[index];
      compositor.compose(canvas, mix);
      if (primary.mediaShown) {
        const auto draw = primary.media.draw(primary.visual.mediaMix, primary.channel.mediaBlend(), canvas.width(), canvas.height());
        compositor.overlay(canvas, std::span<const LayerDraw>(&draw, 1));
      }
      if (canvasEffects) {
        effectsChain.apply(primary.effects, canvas.texture(), *targets[index]);
      }
      glEndQuery(GL_TIME_ELAPSED);
    }
    // Other contexts sample this texture next; it must be complete first.
    glFinish();
    const auto renderEnd = Clock::now();

    GLuint64 gpuNanoseconds = 0;
    if (!composited) {
      glGetQueryObjectui64v(timerQuery, GL_QUERY_RESULT, &gpuNanoseconds);
    } else {
      for (auto& layer : layers) {
        layer->channel.reportGpuMs(0.0f); // hidden layers cost nothing; drawn ones are set below
      }
      for (std::size_t i = 0; i < drawnCount; ++i) {
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(layerQueries[i], GL_QUERY_RESULT, &nanoseconds);
        drawn[i]->channel.reportGpuMs(static_cast<float>(static_cast<double>(nanoseconds) / 1.0e6));
        gpuNanoseconds += nanoseconds;
      }
      GLuint64 mixNanoseconds = 0;
      glGetQueryObjectui64v(layerQueries[kMaxLayers], GL_QUERY_RESULT, &mixNanoseconds);
      gpuNanoseconds += mixNanoseconds;
    }

    ++frameNumber;
    lastIndex = index;
    publishedFrame_.store(packFrame(index, targets[index]->width(), targets[index]->height(), frameNumber),
                          std::memory_order_release);

    if (readbackClients_.load() > 0) {
      if (!pixelPack) {
        pixelPack = std::make_unique<PixelPackReadback>();
      }
      pixelPack->readAndPublish(*targets[index], frameNumber, readback_);
    } else if (pixelPack) {
      // Last readback surface went away: free the buffers, and don't offer
      // a stale frame to the next one.
      pixelPack.reset();
      readback_.clear();
    }

    ++framesInWindow;
    const double windowMs = millisecondsBetween(fpsWindowStart, renderEnd);
    if (windowMs >= 1000.0) {
      stats.framesPerSecond = static_cast<float>(framesInWindow * 1000.0 / windowMs);
      fpsWindowStart = renderEnd;
      framesInWindow = 0;
    }
    stats.framesRendered = frameNumber;
    stats.cpuFrameMs = static_cast<float>(millisecondsBetween(renderStart, renderEnd));
    stats.gpuFrameMs = static_cast<float>(static_cast<double>(gpuNanoseconds) / 1.0e6);
    // The time since the last frame, capped: after a pause or a stall the
    // smoothing shouldn't treat one frame as seconds of evidence.
    const auto dt = std::min(static_cast<float>(millisecondsBetween(lastFrameEnd, renderEnd) / 1000.0), 0.1f);
    // Published whether or not this engine is in Auto: a fixed-quality
    // instance still uses the GPU the others share.
    const float costMs = stats.gpuFrameMs > 0.0f ? stats.gpuFrameMs : stats.cpuFrameMs;
    sharedCostMs = sharedCostMs <= 0.0f ? costMs
                                        : sharedCostMs + (costMs - sharedCostMs) *
                                                             std::min(dt / adaptive.config().smoothingSeconds, 1.0f);
    gpuShare.publish(sharedCostMs);
    const auto others = gpuShare.others();
    stats.gpuSharers = others.sharers;
    if (autoQuality) {
      adaptive.setTargetFps(1000.0f / core::AdaptiveQuality::sharedBudgetMs(frameBudgetMs, others.costMs,
                                                                             others.sharers));
      adaptive.onFrame(stats.gpuFrameMs, stats.cpuFrameMs, dt);
    }
    lastFrameEnd = renderEnd;
    stats.qualityScale = scale;
    stats.qualityAuto = autoQuality;
    stats.width = primary.instance->width();
    stats.height = primary.instance->height();
    stats_.publish(stats);
  }

  available_.store(false, std::memory_order_release);
  sharedContextHandle_.store(nullptr, std::memory_order_release);
  glDeleteQueries(1, &timerQuery);
  glDeleteQueries(static_cast<GLsizei>(layerQueries.size()), layerQueries.data());
  pixelPack.reset();
  readback_.clear();
  layers.clear(); // projectM instances go before the context, on this thread
  targets = {};
  stats.running = false;
  stats_.publish(stats);
  // `context` (created.context) is destroyed here, on the thread it belongs to.
}

} // namespace milkdawp::engine
