#pragma once
#include <juce_core/juce_core.h>
#include "AudioAnalysisQueue.h"
#include "Logging.h"

namespace milkdawp {

// Phase 1.2 scaffolding: a dedicated visualization thread that consumes
// audio analysis snapshots from the lock-free queue. This does not do any
// rendering yet; it simply simulates a render loop decoupled from the audio
// thread and tracks consumption stats.
class VisualizationThread : private juce::Thread {
public:
    explicit VisualizationThread(AudioAnalysisQueue<64>& q)
        : juce::Thread("MilkDAWpVizThread"), queue(q) {}

    ~VisualizationThread() override { stop(); }

    void start() {
        if (!isThreadRunning()) {
            framesConsumed.store(0);
            lastConsumeTimeMs.store(0);
            startThread();
            MDW_LOG_INFO("Visualization thread started");
        }
    }

    void stop(int timeoutMs = 1000) {
        if (isThreadRunning()) {
            signalThreadShouldExit();
            notify();
            stopThread(timeoutMs);
            MDW_LOG_INFO("Visualization thread stopped");
        }
    }

    bool running() const noexcept { return isThreadRunning(); }

    // Stats:
    uint64_t getFramesConsumed() const noexcept { return framesConsumed.load(); }

    // For testing: retrieve last snapshot (not thread-safe against concurrent write,
    // but good enough for tests where we stop the thread first or accept a race).
    AudioAnalysisSnapshot getLastSnapshot() const noexcept { return lastSnapshot; }

private:
    void run() override {
        juce::int64 lastTime = juce::Time::getMillisecondCounter();
        while (! threadShouldExit()) {
            // Consume whatever is available without blocking
            int consumedThisLoop = 0;
            AudioAnalysisSnapshot snap;
            while (queue.tryPop(snap)) {
                lastSnapshot = snap;
                framesConsumed.fetch_add(1);
                ++consumedThisLoop;
            }

            // Simulate a target frame rate ~60 Hz without tying to audio thread
            const juce::int64 now = juce::Time::getMillisecondCounter();
            lastConsumeTimeMs.store((uint32_t)(now - lastTime));
            lastTime = now;

            if (consumedThisLoop == 0) {
                // Sleep briefly to avoid busy-spin when there's nothing to consume
                wait(5);
            } else {
                // Yield to allow other threads to run
                juce::Thread::yield();
            }
        }
    }

    AudioAnalysisQueue<64>& queue;
    std::atomic<uint64_t> framesConsumed{0};
    std::atomic<uint32_t> lastConsumeTimeMs{0};
    AudioAnalysisSnapshot lastSnapshot{};
};

} // namespace milkdawp
