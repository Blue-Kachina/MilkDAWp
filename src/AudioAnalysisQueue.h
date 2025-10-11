#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

namespace milkdawp {

// Snapshot of audio analysis data produced on the audio thread.
struct AudioAnalysisSnapshot {
    static constexpr int fftOrder = 10; // 2^10 = 1024
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int fftBins  = fftSize / 2; // real FFT positive bins

    std::array<float, fftBins> magnitudes{}; // magnitude spectrum (linear)
    float shortTimeEnergy = 0.0f;            // window energy
    bool beatDetected = false;               // simple onset/beat flag
    uint64_t samplePosition = 0;             // position when captured
};

// A minimal single-producer single-consumer lock-free queue for snapshots.
// Uses juce::AbstractFifo for thread-safe indices, with a fixed-capacity
// ring buffer of snapshots to avoid dynamic allocations on the audio thread.
template <size_t Capacity>
class AudioAnalysisQueue {
public:
    static_assert(Capacity > 1, "Capacity must be > 1");

    AudioAnalysisQueue() : fifo(static_cast<int>(Capacity)) {}

    // Producer (audio thread): attempts to push a snapshot. Returns false if full.
    bool tryPush(const AudioAnalysisSnapshot& s) noexcept {
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 <= 0) {
            // drop if full to avoid blocking audio thread
            return false;
        }
        buffer[static_cast<size_t>(start1)] = s; // POD copy
        fifo.finishedWrite(size1);
        return true;
    }

    // Consumer (viz thread): attempts to pop a snapshot. Returns false if empty.
    bool tryPop(AudioAnalysisSnapshot& out) noexcept {
        int start1, size1, start2, size2;
        fifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 <= 0) return false;
        out = buffer[static_cast<size_t>(start1)];
        fifo.finishedRead(size1);
        return true;
    }

    void clear() noexcept {
        // Drain the fifo
        AudioAnalysisSnapshot tmp;
        while (true) {
            int start1, size1, start2, size2;
            fifo.prepareToRead(1, start1, size1, start2, size2);
            if (size1 <= 0) break;
            (void)buffer[static_cast<size_t>(start1)];
            fifo.finishedRead(size1);
        }
    }

    int getNumAvailable() const noexcept { return fifo.getNumReady(); }
    int getFreeSpace() const noexcept { return static_cast<int>(Capacity) - fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo;
    std::array<AudioAnalysisSnapshot, Capacity> buffer{};
};

} // namespace milkdawp
