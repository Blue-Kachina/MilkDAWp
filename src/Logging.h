// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#pragma once

#include <juce_core/juce_core.h>

namespace milkdawp {

// Lightweight logging facade for early phases.
struct Logging {
    static void init(const juce::String& appName, const juce::String& version)
    {
        static std::atomic<bool> initialised{ false };
        bool expected = false;
        if (!initialised.compare_exchange_strong(expected, true))
            return;

        // Route all MDW_LOG_* output to a rolling file log.
        // JUCE trims the file at open time if it exceeds maxInitialFileSizeBytes,
        // so old sessions never bloat the log past ~4 MB.
        auto logDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("MilkDAWp");
        logDir.createDirectory();
        auto logFile = logDir.getChildFile("MilkDAWp.log");

        fileLogger.reset(new juce::FileLogger(
            logFile,
            appName + " " + version + " — log started",
            4 * 1024 * 1024 /* 4 MB rolling cap */));
        juce::Logger::setCurrentLogger(fileLogger.get());

        juce::Logger::writeToLog("[MilkDAWp] Logging initialised");
    }

    // Disable logging at runtime: removes the file logger so no disk I/O occurs.
    // The enabled state is NOT persisted here — callers (e.g. the settings panel)
    // are responsible for saving and restoring the preference.
    static void setEnabled(bool shouldLog)
    {
        if (shouldLog == enabled_)
            return;

        enabled_ = shouldLog;

        if (shouldLog)
        {
            if (fileLogger)
                juce::Logger::setCurrentLogger(fileLogger.get());
        }
        else
        {
            juce::Logger::setCurrentLogger(nullptr);
        }
    }

    static bool isEnabled() { return enabled_; }

    static void shutdown()
    {
        juce::Logger::setCurrentLogger(nullptr);
        fileLogger.reset();
    }

private:
    static inline std::unique_ptr<juce::FileLogger> fileLogger;
    static inline bool enabled_ = true;
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
