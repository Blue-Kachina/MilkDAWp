// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#pragma once

#include <atomic>
#include <thread>
#include <cmath>
#include <juce_core/juce_core.h>
#include "AudioAnalysisQueue.h"
#include "MessageThreadBridge.h"
#include "Logging.h"

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
    explicit VisualizationThread(IAudioAnalysisQueue& q)
        : queue(q)
    {
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
    }

    double getTargetFps() const { return targetFps.load(std::memory_order_relaxed); }
    uint64_t getFramesRendered() const { return framesRendered.load(std::memory_order_acquire); }

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
        }
    }

    void applyPendingPresetLoads()
    {
        juce::String path;
        while (presetLoadRequests.tryPop(path))
        {
            juce::String err;
            if (!pm.loadPreset(path, err)) {
                MDW_LOG_ERROR(juce::String("Failed to load preset: ") + path + ": " + err);
            } else {
                MDW_LOG_INFO(juce::String("Loaded preset: ") + path);
            }
        }
    }

    void run()
    {
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
                }
                framesRendered.fetch_add(1, std::memory_order_relaxed);

                // Schedule next frame; avoid drift by stepping in increments
                nextFrameTimeMs += frameDurMs;
                if (nowMs - nextFrameTimeMs > 5 * frameDurMs) {
                    // If we fell behind significantly, reset to now
                    nextFrameTimeMs = nowMs + frameDurMs;
                }
            }

            // Sleep a little to yield CPU; coarse timing is fine for tests
            juce::Thread::sleep(1);
        }

        pm.shutdown();
    }

    IAudioAnalysisQueue& queue;
    std::atomic<bool> running{ false };
    std::atomic<uint64_t> framesConsumed{ 0 };
    std::atomic<uint64_t> framesRendered{ 0 };
    std::atomic<double> targetFps{ 60.0 };
    std::thread worker;
    ProjectMContext pm;
    RenderSurface surface;
    ThreadSafeSPSCQueue<ParameterChange, 64> paramChanges;
    ThreadSafeSPSCQueue<juce::String, 8> presetLoadRequests;
};

} // namespace milkdawp
