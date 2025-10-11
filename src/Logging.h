#pragma once
#include <JuceHeader.h>

namespace milkdawp {

// Simple logging facade over JUCE's Logger/FileLogger.
class Logging {
public:
    // Initialize file logging once; safe to call multiple times.
    static void init(const juce::String& appName, const juce::String& versionString) {
        static std::once_flag once;
        std::call_once(once, [&]() {
            auto logsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile(appName)
                               .getChildFile("Logs");
            logsDir.createDirectory();

            auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
            auto logFile = logsDir.getChildFile(appName + "_" + timestamp + ".log");

            auto fileLogger = std::make_unique<juce::FileLogger>(logFile, appName + " log started", 1024 * 128);
            juce::Logger::setCurrentLogger(fileLogger.release());

            juce::Logger::writeToLog(appName + " v" + versionString + " starting up");
#if MILKDAWP_HAS_PROJECTM
            juce::Logger::writeToLog("Feature: libprojectM enabled");
#else
            juce::Logger::writeToLog("Feature: libprojectM disabled");
#endif
        });
    }

    static void shutdown() {
        juce::Logger::setCurrentLogger(nullptr);
    }
};

// Convenience macros
#define MDW_LOG_INFO(msg)   do { juce::Logger::writeToLog(juce::String("INFO: ") + (msg)); } while(false)
#define MDW_LOG_WARN(msg)   do { juce::Logger::writeToLog(juce::String("WARN: ") + (msg)); } while(false)
#define MDW_LOG_ERROR(msg)  do { juce::Logger::writeToLog(juce::String("ERROR: ") + (msg)); } while(false)

} // namespace milkdawp
