#pragma once

#include <atomic>
#include <cstdint>
#include <array>
#include <vector>
#include <juce_core/juce_core.h>

namespace milkdawp {

// Analysis snapshot produced by audio thread and consumed by visualization thread.
struct AudioAnalysisSnapshot {
    // FFT configuration used for windowing in processor/tests
    static constexpr int fftOrder = 10; // 2^10 = 1024
    static constexpr int fftSize  = 1 << fftOrder;

    uint64_t samplePosition = 0;    // position in samples of start of window
    float shortTimeEnergy   = 0.0f; // simple energy metric for tests and basic visualization
    // Additional fields (spectrum bins, beat flags, etc.) will be added in later phases.
};

// Minimal interface to allow VisualizationThread to operate on any queue instance
struct IAudioAnalysisQueue {
    virtual bool tryPop(AudioAnalysisSnapshot& out) = 0;
    virtual int  getNumAvailable() const = 0;
    virtual ~IAudioAnalysisQueue() = default;
};

// Single-producer single-consumer lock-free ring buffer.
// Capacity must be a power of two for masking to work correctly.
template <int CapacityPow2>
class AudioAnalysisQueue : public IAudioAnalysisQueue {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "Capacity must be power of two");
public:
    AudioAnalysisQueue()
        : head(0), tail(0)
    {
        // Default-initialised buffer
    }

    bool tryPush(const AudioAnalysisSnapshot& s)
    {
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        if (((h + 1) & mask) == (t & mask))
            return false; // full
        buffer[h & mask] = s;
        head.store(h + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(AudioAnalysisSnapshot& out) override
    {
        const size_t t = tail.load(std::memory_order_relaxed);
        const size_t h = head.load(std::memory_order_acquire);
        if ((t & mask) == (h & mask))
            return false; // empty
        out = buffer[t & mask];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }

    int getNumAvailable() const override
    {
        const size_t h = head.load(std::memory_order_acquire);
        const size_t t = tail.load(std::memory_order_acquire);
        return (int)((h - t) & mask);
    }

    void clear()
    {
        tail.store(head.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

private:
    static constexpr size_t mask = (size_t)CapacityPow2 - 1;
    std::array<AudioAnalysisSnapshot, CapacityPow2> buffer{};
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};

} // namespace milkdawp
