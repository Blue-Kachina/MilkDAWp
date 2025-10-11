#pragma once

#include <atomic>
#include <array>

namespace milkdawp {

// Generic single-producer single-consumer lock-free ring buffer.
// Capacity must be a power of two for masking to work correctly.
template <typename T, int CapacityPow2>
class ThreadSafeSPSCQueue {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "Capacity must be power of two");
public:
    ThreadSafeSPSCQueue() : head(0), tail(0) {}

    bool tryPush(const T& v)
    {
        const size_t h = head.load(std::memory_order_relaxed);
        const size_t t = tail.load(std::memory_order_acquire);
        if (((h + 1) & mask) == (t & mask))
            return false; // full
        buffer[h & mask] = v;
        head.store(h + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(T& out)
    {
        const size_t t = tail.load(std::memory_order_relaxed);
        const size_t h = head.load(std::memory_order_acquire);
        if ((t & mask) == (h & mask))
            return false; // empty
        out = buffer[t & mask];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }

    int getNumAvailable() const
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
    std::array<T, CapacityPow2> buffer{};
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};

} // namespace milkdawp
