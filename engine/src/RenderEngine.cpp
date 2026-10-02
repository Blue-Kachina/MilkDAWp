// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/RenderEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/engine/GlFrameTarget.h"
#include "milkdawp/engine/LayerCompositor.h"
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

// projectM calls this synchronously from inside a load call when it rejects
// the preset; the render loop checks the flag right after the call.
struct LoadFailureFlag {
  bool failed = false;
};

void onPresetSwitchFailed(const char*, const char*, void* userData) {
  static_cast<LoadFailureFlag*>(userData)->failed = true;
}

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
  // Where this layer draws before the compositor mixes it. Only exists while
  // there is more than one layer: a lone layer draws straight into the output.
  std::unique_ptr<GlFrameTarget> target;
};

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
  auto loaded = ProjectMLibrary::load(bundleDirectoryHint);
  std::unique_ptr<RenderEngine> engine(
      new RenderEngine(audio, config, std::move(loaded.library), std::move(loaded.unavailableReason)));
  if (engine->library_) {
    engine->thread_ = std::thread([raw = engine.get()] { raw->run(); });
  }
  return engine;
}

RenderEngine::RenderEngine(const core::AudioRing& audio, const Config& config,
                           std::unique_ptr<ProjectMLibrary> library, std::string unavailableReason)
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
  std::string error;
  auto primaryInstance = ProjectMInstance::create(*library_, settings, error);
  if (!primaryInstance) {
    targets = {};
    setUnavailable(error);
    return;
  }
  // The layers this thread renders. Exactly one for now, the primary one a
  // solo Visualizer drives; the loops below already run per layer.
  std::vector<std::unique_ptr<Layer>> layers;
  layers.push_back(std::make_unique<Layer>(primary_, std::move(primaryInstance), settings.beatSensitivity));
  Layer& primary = *layers.front();
  // Mixes the layers once there are several; a lone layer never uses it. If it
  // fails to build, several layers degrade to showing the primary one.
  LayerCompositor compositor;

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
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      nextFrame = Clock::now();
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

    const float beatSensitivity = beatSensitivity_.load();
    for (auto& layer : layers) {
      if (beatSensitivity != layer->appliedBeatSensitivity) {
        layer->instance->setBeatSensitivity(beatSensitivity);
        layer->appliedBeatSensitivity = beatSensitivity;
      }
    }

    const float scale = std::clamp(qualityScale_.load(), 0.25f, 1.0f);
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
              layer->instance->setSoftCutDuration(std::max(static_cast<double>(due.request.blendSeconds), 0.1));
            }
            layer->loadFailure.failed = false;
            const auto loadStart = Clock::now();
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
              handoff.reportFailure(due.request.presetId);
            } else if (isPrimary) {
              ++stats.presetsLoaded;
              stats.currentPresetId = due.request.presetId;
            }
          });
      }

      // A layer that will not be drawn this frame is not fed either: the feeder
      // only ever hands over the newest audio, so it picks up from "now" later.
      if (!multiLayer || (channel.visible() && channel.opacity() > 0.0f)) {
        layer->feeder.feed(channel.audio(), [&](const float* samples, std::size_t frames, int channels) {
          layer->instance->addPcm(samples, frames, channels);
        });
      }
    }

    const std::size_t index = (lastIndex + 1) % kFrameCount;
    const auto renderStart = Clock::now();
    const bool composited = multiLayer && compositor.ok();
    // GL allows one GL_TIME_ELAPSED query at a time: a lone layer is timed as a
    // whole, several are timed layer by layer (and the mix) and summed.
    std::array<Layer*, kMaxLayers> drawn{};
    std::size_t drawnCount = 0;
    if (!composited) {
      // One layer draws straight into the output frame (so does the primary
      // alone if the compositor could not be built).
      glBeginQuery(GL_TIME_ELAPSED, timerQuery);
      primary.instance->renderTo(targets[index]->framebuffer());
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
      std::array<LayerDraw, kMaxLayers> draws{};
      for (std::size_t i = 0; i < drawnCount; ++i) {
        Layer& layer = *drawn[i];
        if (!layer.target) {
          layer.target = std::make_unique<GlFrameTarget>(targets[index]->width(), targets[index]->height());
        } else {
          layer.target->resize(targets[index]->width(), targets[index]->height());
        }
        glBeginQuery(GL_TIME_ELAPSED, layerQueries[i]);
        layer.instance->renderTo(layer.target->framebuffer());
        glEndQuery(GL_TIME_ELAPSED);
        draws[i] = {layer.target->texture(), layer.channel.opacity(), layer.channel.blend()};
      }
      glBeginQuery(GL_TIME_ELAPSED, layerQueries[kMaxLayers]);
      compositor.compose(*targets[index], std::span<const LayerDraw>(draws.data(), drawnCount));
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
