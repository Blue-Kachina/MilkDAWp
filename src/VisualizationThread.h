#pragma once

#include <atomic>
#include <thread>
#include <cmath>
#include <juce_core/juce_core.h>
#include "AudioAnalysisQueue.h"

namespace milkdawp {

// Minimal stub for a projectM context. In later phases this will wrap real libprojectM.
struct ProjectMContext {
    bool initialised = false;

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

    void renderFrame(const AudioAnalysisSnapshot&)
    {
        // Placeholder: in future, feed spectrum/beat info to projectM
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

private:
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
};

} // namespace milkdawp
