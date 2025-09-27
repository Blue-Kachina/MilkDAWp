
#pragma once
#include <JuceHeader.h>

namespace om::milkdawp
{
    class FileLoggerGuard
    {
    public:
        FileLoggerGuard()
        {
            using namespace juce;
            auto appData = File::getSpecialLocation(File::userApplicationDataDirectory);
            auto logDir  = appData.getChildFile("MilkDAWp").getChildFile("logs");
            if (! logDir.exists())
                logDir.createDirectory();
            logger.reset(FileLogger::createDateStampedLogger(logDir.getFullPathName(), "milkdawp", ".log", "MilkDAWp log start"));
            if (logger)
                Logger::setCurrentLogger(logger.get());
        }
        ~FileLoggerGuard()
        {
            juce::Logger::setCurrentLogger(nullptr);
            logger.reset();
        }
    private:
        std::unique_ptr<juce::FileLogger> logger;
    };
}

// Logging macro: enabled only if MILKDAWP_ENABLE_LOGGING is defined (typically in Debug builds)
#if defined(MILKDAWP_ENABLE_LOGGING)
  #define MDW_LOG(tag, msg) do { juce::Logger::writeToLog(juce::String("[" tag "] ") + msg); } while(0)
#else
  #define MDW_LOG(tag, msg) do { } while(0)
#endif
