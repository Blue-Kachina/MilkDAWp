// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#pragma once

#include <juce_core/juce_core.h>

namespace milkdawp {

// Lightweight logging facade for early phases.
struct Logging {
    static void init(const juce::String& appName, const juce::String& version)
    {
        juce::ignoreUnused(appName, version);
        // JUCE's default Logger writes to OS debug output / stdout depending on platform.
        // We just emit a banner once; guard against duplicate banners using a static flag.
        static std::atomic<bool> initialised{ false };
        bool expected = false;
        if (initialised.compare_exchange_strong(expected, true))
        {
            juce::Logger::writeToLog("[MilkDAWp] Logging initialised");
        }
    }

    static void shutdown()
    {
        // Nothing to clean up for now; placeholder for future log sinks.
    }
};

} // namespace milkdawp

// Simple macros that map to JUCE Logger for now.
#ifndef MDW_LOG_INFO
#define MDW_LOG_INFO(msg)  do { juce::Logger::writeToLog(juce::String("[INFO] ") + (msg)); } while(false)
#endif
#ifndef MDW_LOG_WARN
#define MDW_LOG_WARN(msg)  do { juce::Logger::writeToLog(juce::String("[WARN] ") + (msg)); } while(false)
#endif
#ifndef MDW_LOG_ERROR
#define MDW_LOG_ERROR(msg) do { juce::Logger::writeToLog(juce::String("[ERROR] ") + (msg)); } while(false)
#endif
