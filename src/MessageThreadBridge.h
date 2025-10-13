// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#pragma once

#include <functional>
#include <atomic>
#include <juce_core/juce_core.h>
#include "ThreadSafeQueue.h"

namespace milkdawp {

struct ParameterChange {
    juce::String paramID;
    float value = 0.0f;
    uint64_t sequence = 0;
};

// Minimal bridge that allows thread-safe posting of ParameterChange from the
// audio thread to the message thread, and forwarding from message to viz.
// For tests, we provide an explicit drainOnMessageThread() that should be
// called on the message thread (or simulated message thread in tests).
class MessageThreadBridge {
public:
    using Listener = std::function<void(const ParameterChange&)>;

    void setMessageListener(Listener cb) { messageListener = std::move(cb); }
    void setVisualizationListener(Listener cb) { vizListener = std::move(cb); }

    // Audio thread API: enqueue a change and request a message-thread drain.
    bool postFromAudioToMessage(const juce::String& id, float value)
    {
        ParameterChange pc{ id, value, nextSeq.fetch_add(1, std::memory_order_relaxed) };
        return audioToMessage.tryPush(pc);
    }

    // Message thread API: process pending items, notifying message listener and
    // forwarding to visualization listener (message→viz communication path).
    void drainOnMessageThread()
    {
        ParameterChange pc;
        while (audioToMessage.tryPop(pc))
        {
            if (messageListener)
                messageListener(pc);
            if (vizListener)
                vizListener(pc);
        }
    }

private:
    ThreadSafeSPSCQueue<ParameterChange, 64> audioToMessage; // capacity small is fine for tests
    std::atomic<uint64_t> nextSeq{0};
    Listener messageListener;
    Listener vizListener;
};

} // namespace milkdawp
