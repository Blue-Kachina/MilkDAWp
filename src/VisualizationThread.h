#pragma once

#include <atomic>
#include <thread>
#include <juce_core/juce_core.h>
#include "AudioAnalysisQueue.h"

namespace milkdawp {

// Simple visualization consumer thread for Phase 1.x scaffolding.
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

    uint64_t getFramesConsumed() const { return framesConsumed.load(std::memory_order_acquire); }

private:
    void run()
    {
        // Consume snapshots opportunistically
        AudioAnalysisSnapshot s;
        while (running.load(std::memory_order_relaxed))
        {
            bool any = false;
            while (queue.tryPop(s))
            {
                any = true;
                framesConsumed.fetch_add(1, std::memory_order_relaxed);
            }
            if (!any)
                juce::Thread::sleep(5);
        }
    }

    IAudioAnalysisQueue& queue;
    std::atomic<bool> running{ false };
    std::atomic<uint64_t> framesConsumed{ 0 };
    std::thread worker;
};

} // namespace milkdawp
