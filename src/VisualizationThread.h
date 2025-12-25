// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#pragma once

#include <atomic>
#include <thread>
#include <cmath>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include "AudioAnalysisQueue.h"
#include "MessageThreadBridge.h"
#include "Logging.h"
#include "SharedAssetCache.h"
#include "AdaptiveQuality.h"

#if JUCE_WINDOWS
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

namespace milkdawp {

// Minimal stub for a projectM context. In later phases this will wrap real libprojectM.
struct ProjectMContext {
    bool initialised = false;

    // Parameters (stubbed storage)
    std::atomic<float> beatSensitivity{1.0f};
    std::atomic<float> transitionDurationSeconds{5.0f};
    std::atomic<bool> shuffle{false};
    std::atomic<bool> lockCurrentPreset{false};
    std::atomic<int> presetIndex{0};
    std::atomic<int> transitionStyle{0}; // 0=Cut, 1=Crossfade, 2=Blend

    juce::String currentPresetName; // for UI display
    int paletteIndex = 0; // derived visual palette for stub renderer

    bool init()
    {
        // Simulate some setup work; must be called on viz thread
        initialised = true;
        return true;
    }

    void shutdown()
    {
        initialised = false;
    }

    // Setters that would map to libprojectM APIs later
    void setBeatSensitivity(float v) { beatSensitivity.store(v, std::memory_order_relaxed); }
    void setTransitionDurationSeconds(float v) { transitionDurationSeconds.store(v, std::memory_order_relaxed); }
    void setShuffle(bool v) { shuffle.store(v, std::memory_order_relaxed); }
    void setLockCurrentPreset(bool v) { lockCurrentPreset.store(v, std::memory_order_relaxed); }
    void setPresetIndex(int v) { presetIndex.store(v, std::memory_order_relaxed); }
    void setTransitionStyle(int v) { transitionStyle.store(v, std::memory_order_relaxed); }

    bool loadPreset(const juce::String& path, juce::String& outError)
    {
        outError = {};
        juce::File f(path);
        if (! f.existsAsFile()) {
            outError = "Preset file does not exist";
            return false;
        }
        auto ext = f.getFileExtension().toLowerCase();
        if (ext != ".milk") {
            outError = "Unsupported preset file type (expected .milk)";
            return false;
        }
        currentPresetName = f.getFileNameWithoutExtension();
        // Derive a palette index from the preset name to make visual changes obvious in the stub renderer.
        {
            const int hash = currentPresetName.hashCode();
            const int palettes = 5; // small set of distinct palettes
            paletteIndex = (hash == juce::String().hashCode()) ? 0 : (std::abs(hash) % palettes);
        }
        // In a future phase, we will call real libprojectM API here and handle errors.
        return true;
    }

    void renderFrame(const AudioAnalysisSnapshot&)
    {
        // Placeholder: in future, feed spectrum/beat info to projectM
        // We could use the parameters above to influence rendering.
    }
};

// Basic render surface abstraction (placeholder for a GPU surface)
struct RenderSurface {
    int width = 1280;
    int height = 720;
    void resize(int w, int h) { width = w; height = h; }
};

// Visualization thread that renders at a target FPS, independent of the audio thread.
class VisualizationThread {
public:
    // CPU frame buffer interface for embedded canvas
    struct FrameSnapshot {
        juce::Image image; // ARGB
    };
public:
    explicit VisualizationThread(IAudioAnalysisQueue& q)
        : queue(q)
    {
        // Allocate ~1 second of stereo PCM at 48k for inter-thread transport (float, interleaved)
        pcmRing.init(48000);
    }

    ~VisualizationThread() { stop(); }

    void start()
    {
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true))
            return; // already running
        worker = std::thread([this]{ this->run(); });
    }

    void stop()
    {
        bool expected = true;
        if (!running.compare_exchange_strong(expected, false))
            return; // not running
        if (worker.joinable())
            worker.join();
    }

    // Queue consumption stats from tests (legacy)
    uint64_t getFramesConsumed() const { return framesConsumed.load(std::memory_order_acquire); }

    // New: rendering stats and configuration
    void setTargetFps(double fps)
    {
        if (fps < 1.0) fps = 1.0;
        if (fps > 240.0) fps = 240.0;
        targetFps.store(fps, std::memory_order_relaxed);
#if defined(MDW_ENABLE_ADAPTIVE_QUALITY)
        if (MDW_ENABLE_ADAPTIVE_QUALITY)
            aqController.setTargetFps(fps);
#endif
    }

    double getTargetFps() const { return targetFps.load(std::memory_order_relaxed); }
    uint64_t getFramesRendered() const { return framesRendered.load(std::memory_order_acquire); }
    double getInstantFps() const { return fpsInstant.load(std::memory_order_relaxed); }
    double getAverageFps() const { return fpsAverage.load(std::memory_order_relaxed); }
    double getCacheHitRate() const {
        const uint64_t hits = cacheHits.load(std::memory_order_relaxed);
        const uint64_t misses = cacheMisses.load(std::memory_order_relaxed);
        const uint64_t total = hits + misses;
        if (total == 0) return 0.0;
        return (double)hits / (double)total;
    }
    // CPU / frame-time metrics getters
    double getVizThreadCpuPercent() const { return vizCpuPercent.load(std::memory_order_relaxed); }
    double getInstantFrameMs() const { return frameMsInstant.load(std::memory_order_relaxed); }
    double getAverageFrameMs() const { return frameMsAverage.load(std::memory_order_relaxed); }

    // Surface/resize API (message thread calls via editor)
    void setSurfaceSize(int w, int h)
    {
        if (w < 2) w = 2; if (h < 2) h = 2;
        juce::ScopedLock sl(backBufferLock);
        surface.resize(w, h);
        backBuffer = juce::Image(juce::Image::ARGB, w, h, true);
    }

    // Snapshot API for UI to copy most recent frame
    bool getFrameSnapshot(FrameSnapshot& out)
    {
        juce::ScopedLock sl(backBufferLock);
        if (! backBuffer.isValid()) return false;
        out.image = backBuffer.createCopy();
        return true;
    }

    // Snapshot API for GL thread to fetch latest analysis (non-blocking)
    bool getLatestAnalysisSnapshot(AudioAnalysisSnapshot& out)
    {
        juce::ScopedLock sl(latestLock);
        if (! latestHave) return false;
        out = latestSnapshot;
        return true;
    }

    // Audio PCM posting API (audio thread → viz/GL thread)
    bool postAudioBlockInterleaved(const float* interleavedStereo, int numFrames, double sampleRate)
    {
        if (interleavedStereo == nullptr || numFrames <= 0)
            return false;
        pcmSampleRate.store(sampleRate, std::memory_order_relaxed);
        pcmRing.pushInterleaved(interleavedStereo, numFrames);
        lastPcmWriteMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
        return true;
    }

    // Fetch latest PCM window for GL thread consumption (interleaved stereo float)
    bool getLatestPcmWindow(std::vector<float>& outInterleaved, int desiredFrames, double& outSampleRate) const
    {
        outSampleRate = pcmSampleRate.load(std::memory_order_relaxed);
        if (desiredFrames <= 0) desiredFrames = defaultPcmWindowFrames;
        pcmRing.copyLatest(desiredFrames, outInterleaved);
        return !outInterleaved.empty();
    }

    // Whether PCM was posted recently within the provided age threshold (ms)
    bool hasRecentPcm(double maxAgeMs) const
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double last = lastPcmWriteMs.load(std::memory_order_relaxed);
        return (now - last) <= maxAgeMs;
    }

    // Thread-safe parameter posting API (can be called from audio or message thread)
    bool postParameterChange(const juce::String& id, float value)
    {
        return paramChanges.tryPush(ParameterChange{ id, value, 0 });
    }

    // Thread-safe preset load request
    bool postLoadPreset(const juce::String& path)
    {
        return presetLoadRequests.tryPush(path);
    }

    // Accessor for current preset name (for UI polling if needed)
    juce::String getCurrentPresetName() const { return pm.currentPresetName; }

private:
    void applyPendingParameterChanges()
    {
        ParameterChange pc;
        while (paramChanges.tryPop(pc))
        {
            if (pc.paramID == "beatSensitivity")
                pm.setBeatSensitivity(pc.value);
            else if (pc.paramID == "transitionDurationSeconds")
                pm.setTransitionDurationSeconds(pc.value);
            else if (pc.paramID == "shuffle")
                pm.setShuffle(pc.value >= 0.5f);
            else if (pc.paramID == "lockCurrentPreset")
                pm.setLockCurrentPreset(pc.value >= 0.5f);
            else if (pc.paramID == "presetIndex")
                pm.setPresetIndex((int)std::lround(pc.value));
            else if (pc.paramID == "transitionStyle")
                pm.setTransitionStyle((int)std::lround(pc.value));
            else if (pc.paramID == "qualityOverride")
            {
            #if defined(MDW_ENABLE_ADAPTIVE_QUALITY)
                if (MDW_ENABLE_ADAPTIVE_QUALITY)
                {
                    int mode = (int)std::lround(pc.value);
                    aqController.setQualityMode(static_cast<QualityMode>(mode));
                }
            #endif
            }
        }
    }

    void applyPendingPresetLoads()
    {
        const double tStartMs_global = juce::Time::getMillisecondCounterHiRes();
        // Drain queue and keep only the most recent unique path
        juce::String pendingPath;
        juce::String tmp;
        while (presetLoadRequests.tryPop(tmp)) {
            if (tmp.isNotEmpty()) pendingPath = tmp;
        }
        if (pendingPath.isEmpty()) return;
        if (pendingPath == lastAppliedPreset) return; // de-dup same preset

        // Use shared cache for preset metadata to avoid redundant disk/parse work
        auto& cache = SharedAssetCache::instance();
        SharedAssetCache::PresetMeta meta;
        bool hit = cache.getPresetMeta(pendingPath, meta);

        // Validate cache entry by timestamp; if file has changed, recompute
        const juce::File f(pendingPath);
        const juce::Time ts = f.getLastModificationTime();
        if (!hit || !(ts == meta.lastModified)) {
            // Recompute meta using same logic as ProjectMContext::loadPreset
            juce::String err;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            if (!pm.loadPreset(pendingPath, err)) {
                MDW_LOG_ERROR(juce::String("Failed to load preset: ") + pendingPath + ": " + err);
                return;
            }
            const double dt = juce::Time::getMillisecondCounterHiRes() - t0;
            meta.name = pm.currentPresetName;
            meta.paletteIndex = pm.paletteIndex;
            meta.lastModified = ts;
            cache.upsertPresetMeta(pendingPath, meta);
            cacheMisses.fetch_add(1, std::memory_order_relaxed);
            // Update EMA for miss time (viz thread only)
            const double alpha = 0.2;
            avgCacheMissMs = (1.0 - alpha) * avgCacheMissMs + alpha * dt;
            MDW_LOG_INFO(juce::String("Loaded preset (cache miss): ") + pendingPath + juce::String(" in ") + juce::String(dt, 2) + " ms");
        } else {
            // Apply cached meta to projectM context quickly
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            pm.currentPresetName = meta.name;
            pm.paletteIndex = meta.paletteIndex;
            const double dt = juce::Time::getMillisecondCounterHiRes() - t0;
            cacheHits.fetch_add(1, std::memory_order_relaxed);
            const double alpha = 0.2;
            avgCacheHitMs = (1.0 - alpha) * avgCacheHitMs + alpha * dt;
            MDW_LOG_INFO(juce::String("Loaded preset (cache hit): ") + pendingPath + juce::String(" in ") + juce::String(dt, 2) + " ms");
        }

        // Refcount management: add ref for new, release previous
        cache.addRef(pendingPath);
        if (lastAppliedPreset.isNotEmpty() && lastAppliedPreset != pendingPath)
            cache.release(lastAppliedPreset);

        lastAppliedPreset = pendingPath;
    }

    void run()
    {
        // Initialize metrics timers on this thread
        lastFrameEndMs = 0.0;
        double metricsLogIntervalMs = 2000.0;
        nextMetricsLogMs = juce::Time::getMillisecondCounterHiRes() + metricsLogIntervalMs;
        // Initialize CPU sampling state
        lastCpuSampleWallMs = juce::Time::getMillisecondCounterHiRes();
        #if JUCE_WINDOWS
        {
            FILETIME createTime{}, exitTime{}, kernelTime{}, userTime{};
            if (GetThreadTimes(GetCurrentThread(), &createTime, &exitTime, &kernelTime, &userTime))
            {
                lastThreadKernel100ns = fileTimeTo100ns(kernelTime);
                lastThreadUser100ns = fileTimeTo100ns(userTime);
            }
        }
        #endif
        // Initialize projectM stub and surface on this thread
        pm.init();
        surface.resize(1280, 720);

        AudioAnalysisSnapshot latest{};
        bool haveLatest = false;

        double nextFrameTimeMs = juce::Time::getMillisecondCounterHiRes();
        
        while (running.load(std::memory_order_relaxed))
        {
            // Drain queue quickly; keep the most recent snapshot
            AudioAnalysisSnapshot s;
            bool any = false;
            while (queue.tryPop(s))
            {
                any = true;
                latest = s;
                haveLatest = true;
                {
                    juce::ScopedLock sl(latestLock);
                    latestSnapshot = latest;
                    latestHave = true;
                }
                framesConsumed.fetch_add(1, std::memory_order_relaxed);
            }

            // Apply any pending parameter changes
            applyPendingParameterChanges();
            // Apply any pending preset loads
            applyPendingPresetLoads();

            const double fps = targetFps.load(std::memory_order_relaxed);
            const double frameDurMs = 1000.0 / fps;
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            
            if (nowMs >= nextFrameTimeMs)
            {
                // Render a frame independent of producer cadence
                if (pm.initialised)
                {
                    pm.renderFrame(latest);
                    // CPU render into backBuffer for embedded canvas
                    {
                        juce::ScopedLock sl(backBufferLock);
                        if (! backBuffer.isValid() || backBuffer.getWidth() != surface.width || backBuffer.getHeight() != surface.height)
                            backBuffer = juce::Image(juce::Image::ARGB, surface.width, surface.height, true);
                        juce::Graphics g(backBuffer);
                        // Background gradient animated by time and parameters
                        const float bs = pm.beatSensitivity.load(std::memory_order_relaxed);
                        const float t = (float)(0.001 * juce::Time::getMillisecondCounterHiRes());
                        // Choose palette based on preset-derived paletteIndex
                        juce::Colour c1, c2;
                        const int pal = pm.paletteIndex;
                        switch (pal) {
                            case 1:
                                c1 = juce::Colour::fromFloatRGBA(0.05f + 0.10f * std::sin(t*0.6f), 0.08f, 0.18f, 1.0f);
                                c2 = juce::Colour::fromFloatRGBA(0.12f, 0.14f + 0.10f * std::sin(t*0.5f + 1.1f), 0.30f, 1.0f);
                                break;
                            case 2:
                                c1 = juce::Colour::fromFloatRGBA(0.10f, 0.06f + 0.10f * std::sin(t*0.8f), 0.12f, 1.0f);
                                c2 = juce::Colour::fromFloatRGBA(0.22f, 0.10f, 0.16f + 0.12f * std::sin(t*0.9f + 0.7f), 1.0f);
                                break;
                            case 3:
                                c1 = juce::Colour::fromFloatRGBA(0.06f, 0.12f, 0.08f + 0.10f * std::sin(t*0.7f), 1.0f);
                                c2 = juce::Colour::fromFloatRGBA(0.10f, 0.24f, 0.14f + 0.10f * std::sin(t*0.4f + 0.9f), 1.0f);
                                break;
                            case 4:
                                c1 = juce::Colour::fromFloatRGBA(0.12f + 0.10f * std::sin(t*0.3f), 0.10f, 0.06f, 1.0f);
                                c2 = juce::Colour::fromFloatRGBA(0.26f, 0.22f, 0.10f + 0.08f * std::sin(t*0.6f + 1.5f), 1.0f);
                                break;
                            default:
                                c1 = juce::Colour::fromFloatRGBA(0.08f + 0.06f * std::sin(t*0.5f), 0.10f, 0.12f, 1.0f);
                                c2 = juce::Colour::fromFloatRGBA(0.12f, 0.16f + 0.08f * std::sin(t*0.7f + 1.3f), 0.20f, 1.0f);
                                break;
                        }
                        g.setGradientFill(juce::ColourGradient(c1, 0.0f, 0.0f, c2, (float)surface.width, (float)surface.height, false));
                        g.fillAll();

                        // Optional subtle vignette and preset name overlay (no bar-chart here)
                        const float w = (float)surface.width;
                        const float h = (float)surface.height;
                        // Soft vignette based on energy to show audio reactivity without bars
                        const float energy = latest.shortTimeEnergy;
                        const float amp = juce::jlimit(0.0f, 1.0f, std::sqrt(energy) * (0.4f + 0.6f * bs));
                        juce::Colour vignette = juce::Colours::black.withAlpha(0.15f + 0.25f * amp);
                        g.setGradientFill(juce::ColourGradient(vignette, w*0.5f, h*0.5f, juce::Colours::transparentBlack, 0.0f, 0.0f, true));
                        g.fillAll();

                        // Draw current preset name for user confirmation
                        if (pm.currentPresetName.isNotEmpty()) {
                            auto textBounds = juce::Rectangle<int>(8, (int)h - 32, (int)w - 16, 24);
                            // Backdrop for readability
                            g.setColour(juce::Colours::black.withAlpha(0.35f));
                            g.fillRoundedRectangle(textBounds.reduced(2).toFloat(), 4.0f);
                            // Text
                            g.setColour(juce::Colours::white.withAlpha(0.92f));
                            g.setFont(juce::FontOptions(18.0f).withStyle(juce::Font::bold));
                            g.drawFittedText(pm.currentPresetName, textBounds, juce::Justification::centredRight, 1);
                        }
                    }
                }
                framesRendered.fetch_add(1, std::memory_order_relaxed);

                // Update FPS metrics
                const double frameEnd = juce::Time::getMillisecondCounterHiRes();
                if (lastFrameEndMs > 0.0) {
                    const double frameDt = frameEnd - lastFrameEndMs;
                    if (frameDt > 0.0001) {
                        // Frame time metrics (ms)
                        const double frameMs = frameDt;
                        frameMsInstant.store(frameMs, std::memory_order_relaxed);
                        const double alphaMs = 0.1;
                        const double prevMs = frameMsAverage.load(std::memory_order_relaxed);
                        const double emaMs = (prevMs <= 0.0) ? frameMs : (1.0 - alphaMs) * prevMs + alphaMs * frameMs;
                        frameMsAverage.store(emaMs, std::memory_order_relaxed);
                        // FPS metrics
                        const double inst = 1000.0 / frameDt;
                        fpsInstant.store(inst, std::memory_order_relaxed);
                        const double alpha = 0.1;
                        const double prev = fpsAverage.load(std::memory_order_relaxed);
                        const double ema = (prev <= 0.0) ? inst : (1.0 - alpha) * prev + alpha * inst;
                        fpsAverage.store(ema, std::memory_order_relaxed);
                    }
                }
                lastFrameEndMs = frameEnd;

                // Schedule next frame; avoid drift by stepping in increments
                nextFrameTimeMs += frameDurMs;
                if (nowMs - nextFrameTimeMs > 5 * frameDurMs) {
                    // If we fell behind significantly, reset to now
                    nextFrameTimeMs = nowMs + frameDurMs;
                }
            }

            // Periodic metrics log
            const double tnow = juce::Time::getMillisecondCounterHiRes();

            // Update CPU usage (Windows)
            #if JUCE_WINDOWS
            if (tnow - lastCpuSampleWallMs >= 250.0) // sample every 250ms
            {
                FILETIME createTime{}, exitTime{}, kernelTime{}, userTime{};
                if (GetThreadTimes(GetCurrentThread(), &createTime, &exitTime, &kernelTime, &userTime))
                {
                    const uint64_t k = fileTimeTo100ns(kernelTime);
                    const uint64_t u = fileTimeTo100ns(userTime);
                    const uint64_t dk = (lastThreadKernel100ns == 0) ? 0ULL : (k - lastThreadKernel100ns);
                    const uint64_t du = (lastThreadUser100ns == 0) ? 0ULL : (u - lastThreadUser100ns);
                    const uint64_t dCpu100ns = dk + du; // 100ns units
                    const double dWallMs = (tnow - lastCpuSampleWallMs); // ms
                    if (dWallMs > 0.0)
                    {
                        const double dWall100ns = dWallMs * 10000.0; // 1 ms = 10,000 * 100ns
                        const double pct = juce::jlimit(0.0, 100.0, (dCpu100ns / dWall100ns) * 100.0);
                        vizCpuPercent.store(pct, std::memory_order_relaxed);
                    }
                    lastThreadKernel100ns = k;
                    lastThreadUser100ns = u;
                }
                lastCpuSampleWallMs = tnow;
            }
            #endif

            if (tnow >= nextMetricsLogMs) {
                const double inst = fpsInstant.load(std::memory_order_relaxed);
                const double avg = fpsAverage.load(std::memory_order_relaxed);
                const double fMsInst = frameMsInstant.load(std::memory_order_relaxed);
                const double fMsAvg = frameMsAverage.load(std::memory_order_relaxed);
                const double cpuPct = vizCpuPercent.load(std::memory_order_relaxed);
                const uint64_t fr = framesRendered.load(std::memory_order_relaxed);
                const uint64_t ch = cacheHits.load(std::memory_order_relaxed);
                const uint64_t cm = cacheMisses.load(std::memory_order_relaxed);
                const uint64_t total = ch + cm;
                const double hitRate = (total > 0) ? (double)ch / (double)total : 0.0;

                juce::String aqSuffix;
            #if defined(MDW_ENABLE_ADAPTIVE_QUALITY)
                if (MDW_ENABLE_ADAPTIVE_QUALITY) {
                    auto decision = aqController.evaluate(avg, fMsAvg, cpuPct);
                    // Apply the resolution scaling decision
                    const double prevScale = currentResolutionScale;
                    currentResolutionScale = decision.suggestedScale;
                    // If scale changed significantly, resize backbuffer on next frame
                    if (std::abs(currentResolutionScale - prevScale) > 0.01) {
                        juce::ScopedLock sl(backBufferLock);
                        const int targetW = juce::jmax(2, (int)(surface.width * currentResolutionScale));
                        const int targetH = juce::jmax(2, (int)(surface.height * currentResolutionScale));
                        if (backBuffer.getWidth() != targetW || backBuffer.getHeight() != targetH) {
                            backBuffer = juce::Image(juce::Image::ARGB, targetW, targetH, true);
                            MDW_LOG_INFO(juce::String("Adaptive Quality: resized backbuffer to ") +
                                       juce::String(targetW) + "x" + juce::String(targetH) +
                                       " (scale=" + juce::String(currentResolutionScale, 2) + ")");
                        }
                    }
                #if MDW_VERBOSE_ADAPTIVE_QUALITY
                    aqSuffix = juce::String(", AQ scale=") + juce::String(decision.suggestedScale, 2) +
                               ", reason=" + decision.reason;
                #else
                    aqSuffix = juce::String(", AQ scale=") + juce::String(decision.suggestedScale, 2);
                    juce::ignoreUnused(decision);
                #endif
                }
            #endif

                MDW_LOG_INFO(juce::String("Viz perf: fps inst=") + juce::String(inst, 1) + 
                             ", avg=" + juce::String(avg, 1) +
                             ", frameMs inst=" + juce::String(fMsInst, 2) +
                             ", avg=" + juce::String(fMsAvg, 2) +
                             ", CPU%=" + juce::String(cpuPct, 1) +
                             ", framesRendered=" + juce::String((double)fr, 0) +
                             ", cache: hits=" + juce::String((double)ch, 0) + 
                             ", misses=" + juce::String((double)cm, 0) + 
                             ", hitRate=" + juce::String(hitRate * 100.0, 1) + "%" +
                             ", avgHitMs=" + juce::String(avgCacheHitMs, 2) + 
                             ", avgMissMs=" + juce::String(avgCacheMissMs, 2) + aqSuffix);
                nextMetricsLogMs = tnow + metricsLogIntervalMs;
            }

            // Sleep a little to yield CPU; coarse timing is fine for tests
            juce::Thread::sleep(1);
        }

        pm.shutdown();
    }

    // Lock-free PCM ring buffer for stereo float (interleaved) samples
    struct PcmRing {
        std::vector<float> data; // length = capacityFrames * 2
        size_t capacityFrames = 0;
        std::atomic<size_t> writePosFrames{ 0 };
        void init(size_t framesCapacity)
        {
            capacityFrames = framesCapacity > 0 ? framesCapacity : 1;
            data.assign(capacityFrames * 2, 0.0f);
            writePosFrames.store(0, std::memory_order_relaxed);
        }
        void pushInterleaved(const float* interleavedStereo, int frames)
        {
            if (!interleavedStereo || frames <= 0 || capacityFrames == 0) return;
            size_t w = writePosFrames.load(std::memory_order_relaxed);
            const size_t cap = capacityFrames;
            for (int i = 0; i < frames; ++i)
            {
                const size_t pos = (w % cap) * 2;
                data[pos + 0] = interleavedStereo[(size_t)i * 2 + 0];
                data[pos + 1] = interleavedStereo[(size_t)i * 2 + 1];
                w = (w + 1) % cap;
            }
            writePosFrames.store(w, std::memory_order_release);
        }
        void copyLatest(int desiredFrames, std::vector<float>& out) const
        {
            out.resize((size_t)juce::jmax(0, desiredFrames) * 2);
            if (capacityFrames == 0 || desiredFrames <= 0) { std::fill(out.begin(), out.end(), 0.0f); return; }
            int framesToCopy = juce::jmin((int)capacityFrames, desiredFrames);
            const size_t cap = capacityFrames;
            const size_t w = writePosFrames.load(std::memory_order_acquire);
            for (int i = 0; i < framesToCopy; ++i)
            {
                const size_t pos = (w + cap - (size_t)framesToCopy + (size_t)i) % cap;
                const size_t idx = pos * 2;
                out[(size_t)i * 2 + 0] = data[idx + 0];
                out[(size_t)i * 2 + 1] = data[idx + 1];
            }
        }
    };

    PcmRing pcmRing;
    std::atomic<double> pcmSampleRate{ 44100.0 };
    std::atomic<double> lastPcmWriteMs{ 0.0 };
    static constexpr int defaultPcmWindowFrames = 2048;

    IAudioAnalysisQueue& queue;
    std::atomic<bool> running{ false };
    std::atomic<uint64_t> framesConsumed{ 0 };
    std::atomic<uint64_t> framesRendered{ 0 };
    std::atomic<double> targetFps{ 60.0 };
    std::thread worker;
    ProjectMContext pm;
    RenderSurface surface;
    juce::Image backBuffer;
    juce::CriticalSection backBufferLock;

    // Latest analysis snapshot for GL thread consumption
    juce::CriticalSection latestLock;
    AudioAnalysisSnapshot latestSnapshot{};
    bool latestHave { false };

    ThreadSafeSPSCQueue<ParameterChange, 64> paramChanges;
    ThreadSafeSPSCQueue<juce::String, 8> presetLoadRequests;
    juce::String lastAppliedPreset;

    // Performance metrics
    std::atomic<double> fpsInstant{ 0.0 };
    std::atomic<double> fpsAverage{ 0.0 }; // EMA of FPS
    // New: frame-time metrics (ms)
    std::atomic<double> frameMsInstant{ 0.0 };
    std::atomic<double> frameMsAverage{ 0.0 };
    // New: viz thread CPU usage percent (Windows implementation)
    std::atomic<double> vizCpuPercent{ 0.0 };
    double lastFrameEndMs { 0.0 }; // viz thread only
    double nextMetricsLogMs { 0.0 }; // viz thread only
    static constexpr double metricsLogIntervalMs = 2000.0;

    // Adaptive Quality (skeleton)
#if defined(MDW_ENABLE_ADAPTIVE_QUALITY)
    AdaptiveQualityController aqController;
    double currentResolutionScale { 1.0 }; // viz thread only, applied from aqController decision
#endif

    // CPU sampling state (viz thread only)
    double lastCpuSampleWallMs { 0.0 };
    #if JUCE_WINDOWS
    static uint64_t fileTimeTo100ns(const FILETIME& ft) { return ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime; }
    uint64_t lastThreadKernel100ns { 0 };
    uint64_t lastThreadUser100ns { 0 };
    #endif

    // Preset cache metrics
    std::atomic<uint64_t> cacheHits{ 0 };
    std::atomic<uint64_t> cacheMisses{ 0 };
    double avgCacheHitMs { 0.0 }; // EMA, viz thread only
    double avgCacheMissMs { 0.0 }; // EMA, viz thread only
};

} // namespace milkdawp
