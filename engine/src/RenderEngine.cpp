// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "milkdawp/engine/RenderEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <juce_opengl/juce_opengl.h>

#include "milkdawp/engine/GlFrameTarget.h"
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
    : audio_(audio), config_(config), library_(std::move(library)), unavailableReason_(std::move(unavailableReason)),
      sampleRate_(config.sampleRate), executor_(config.sampleRate) {}

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
      const std::lock_guard lock(engine.shareMutex_);
      engine.renderThreadExited_ = true;
      engine.shareCondition_.notify_all();
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
  auto instance = ProjectMInstance::create(*library_, settings, error);
  if (!instance) {
    targets = {};
    setUnavailable(error);
    return;
  }
  LoadFailureFlag loadFailure;
  instance->setPresetSwitchFailedCallback(&onPresetSwitchFailed, &loadFailure);
  float appliedBeatSensitivity = settings.beatSensitivity;

  PcmFeeder feeder(std::max(instance->maxPcmSamples(), 1U), audio_.numChannels());

  GLuint timerQuery = 0;
  glGenQueries(1, &timerQuery);

  RenderStats stats;
  stats.running = true;
  stats.width = instance->width();
  stats.height = instance->height();
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

    if (!anyVisible) {
      // 2.10: nothing to show, so no GPU work. Context, instance, preset and
      // visual state all stay as they are until a surface is visible again.
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
    if (beatSensitivity != appliedBeatSensitivity) {
      instance->setBeatSensitivity(beatSensitivity);
      appliedBeatSensitivity = beatSensitivity;
    }

    const float scale = std::clamp(qualityScale_.load(), 0.25f, 1.0f);
    const int width = std::clamp(static_cast<int>(std::lround(widest * scale)), 16, config_.maxDimension);
    const int height = std::clamp(static_cast<int>(std::lround(tallest * scale)), 16, config_.maxDimension);
    if (width != instance->width() || height != instance->height()) {
      for (auto& target : targets) {
        target->resize(width, height);
      }
      instance->setOutputSize(width, height);
    }

    presetHandoff_.receive();
    executor_.setSampleRate(sampleRate_.load());
    executor_.onTick(static_cast<std::int64_t>(audio_.samplePosition()),
                     [&](const TransitionExecutor::DueTransition& due) {
                       const auto slot = presetHandoff_.find(due.request.presetId);
                       if (!slot) {
                         return; // superseded, or never delivered
                       }
                       const bool soft = due.request.cutStyle == core::CutStyle::Soft;
                       if (soft) {
                         instance->setSoftCutDuration(std::max(static_cast<double>(due.request.blendSeconds), 0.1));
                       }
                       loadFailure.failed = false;
                       const auto loadStart = Clock::now();
                       instance->loadPresetData(presetHandoff_.text(*slot), soft);
                       stats.lastPresetLoadMs = static_cast<float>(millisecondsBetween(loadStart, Clock::now()));
                       presetHandoff_.release(*slot);
                       stats.lastLandingErrorSamples = due.landingErrorSamples;
                       if (loadFailure.failed) {
                         ++stats.presetsFailed;
                         presetHandoff_.reportFailure(due.request.presetId);
                       } else {
                         ++stats.presetsLoaded;
                         stats.currentPresetId = due.request.presetId;
                       }
                     });

    feeder.feed(audio_, [&](const float* samples, std::size_t frames, int channels) {
      instance->addPcm(samples, frames, channels);
    });

    const std::size_t index = (lastIndex + 1) % kFrameCount;
    const auto renderStart = Clock::now();
    glBeginQuery(GL_TIME_ELAPSED, timerQuery);
    instance->renderTo(targets[index]->framebuffer());
    glEndQuery(GL_TIME_ELAPSED);
    // Other contexts sample this texture next; it must be complete first.
    glFinish();
    const auto renderEnd = Clock::now();

    GLuint64 gpuNanoseconds = 0;
    glGetQueryObjectui64v(timerQuery, GL_QUERY_RESULT, &gpuNanoseconds);

    ++frameNumber;
    lastIndex = index;
    publishedFrame_.store(packFrame(index, targets[index]->width(), targets[index]->height(), frameNumber),
                          std::memory_order_release);

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
    stats.width = instance->width();
    stats.height = instance->height();
    stats_.publish(stats);
  }

  available_.store(false, std::memory_order_release);
  sharedContextHandle_.store(nullptr, std::memory_order_release);
  glDeleteQueries(1, &timerQuery);
  instance.reset();
  targets = {};
  stats.running = false;
  stats_.publish(stats);
  // `context` (created.context) is destroyed here, on the thread it belongs to.
}

} // namespace milkdawp::engine
