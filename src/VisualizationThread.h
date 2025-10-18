// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#pragma once

#include <atomic>
#include <thread>
#include <cmath>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
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
        // Drain queue and keep only the most recent unique path
        juce::String pendingPath;
        juce::String tmp;
        while (presetLoadRequests.tryPop(tmp)) {
            if (tmp.isNotEmpty()) pendingPath = tmp;
        }
        if (pendingPath.isEmpty()) return;
        if (pendingPath == lastAppliedPreset) return; // de-dup same preset
        juce::String err;
        if (!pm.loadPreset(pendingPath, err)) {
            MDW_LOG_ERROR(juce::String("Failed to load preset: ") + pendingPath + ": " + err);
        } else {
            MDW_LOG_INFO(juce::String("Loaded preset: ") + pendingPath);
            lastAppliedPreset = pendingPath;
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
                            g.setFont(juce::Font(18.0f, juce::Font::bold));
                            g.drawFittedText(pm.currentPresetName, textBounds, juce::Justification::centredRight, 1);
                        }
                    }
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
    juce::Image backBuffer;
    juce::CriticalSection backBufferLock;

    // Latest analysis snapshot for GL thread consumption
    juce::CriticalSection latestLock;
    AudioAnalysisSnapshot latestSnapshot{};
    bool latestHave { false };

    ThreadSafeSPSCQueue<ParameterChange, 64> paramChanges;
    ThreadSafeSPSCQueue<juce::String, 8> presetLoadRequests;
    juce::String lastAppliedPreset;
};

} // namespace milkdawp
