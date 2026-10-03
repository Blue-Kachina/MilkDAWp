// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CrashReporter.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <utility>

#include "RecentLog.h"

#if JUCE_WINDOWS
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>
#else
#include <csignal>
#endif

namespace milkdawp::app {

namespace {

// The installed reporter. The handler runs on whatever thread crashed, so
// these are atomics; null means no reporter is installed.
std::atomic<const CrashReporter*> gReporter{nullptr};
std::atomic<const RecentLog*> gLog{nullptr};
// Set by the first crash: a crash while reporting one (or std::terminate's
// abort reaching the signal handler) doesn't write a second report.
std::atomic_flag gReporting = ATOMIC_FLAG_INIT;
std::terminate_handler gPreviousTerminate = nullptr;

juce::String describeCrash(void* crashInfo) {
  if (crashInfo == nullptr) {
    return "unknown";
  }
#if JUCE_WINDOWS
  const auto* pointers = static_cast<const EXCEPTION_POINTERS*>(crashInfo);
  const auto* record = pointers->ExceptionRecord;
  if (record == nullptr) {
    return "unhandled exception";
  }
  juce::String text = "unhandled exception 0x" +
                      juce::String::toHexString(static_cast<juce::int64>(record->ExceptionCode));
  switch (record->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
      text << " (access violation)";
      break;
    case EXCEPTION_STACK_OVERFLOW:
      text << " (stack overflow)";
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      text << " (illegal instruction)";
      break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      text << " (integer divide by zero)";
      break;
    default:
      break;
  }
  text << " at 0x"
       << juce::String::toHexString(
              reinterpret_cast<juce::pointer_sized_int>(record->ExceptionAddress));
  return text;
#else
  const auto signal = static_cast<int>(reinterpret_cast<juce::pointer_sized_int>(crashInfo));
  juce::String name;
  switch (signal) {
    case SIGSEGV:
      name = "SIGSEGV";
      break;
    case SIGBUS:
      name = "SIGBUS";
      break;
    case SIGILL:
      name = "SIGILL";
      break;
    case SIGFPE:
      name = "SIGFPE";
      break;
    case SIGABRT:
      name = "SIGABRT";
      break;
    default:
      name = "signal";
      break;
  }
  return name + " (" + juce::String(signal) + ")";
#endif
}

#if JUCE_WINDOWS
bool writeMinidump(const juce::File& file, void* crashInfo) {
  const HANDLE handle = CreateFileW(file.getFullPathName().toWideCharPointer(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  MINIDUMP_EXCEPTION_INFORMATION exception{};
  exception.ThreadId = GetCurrentThreadId();
  exception.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(crashInfo);
  exception.ClientPointers = FALSE;
  const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo |
                                               MiniDumpWithUnloadedModules);
  const BOOL written = MiniDumpWriteDump(GetCurrentProcess(),
                                         GetCurrentProcessId(),
                                         handle,
                                         type,
                                         crashInfo != nullptr ? &exception : nullptr,
                                         nullptr,
                                         nullptr);
  CloseHandle(handle);
  return written != FALSE;
}
#endif

void report(const juce::String& reason, void* crashInfo) {
  if (gReporting.test_and_set()) {
    return;
  }
  const auto* reporter = gReporter.load();
  if (reporter == nullptr) {
    return;
  }
  const auto* log = gLog.load();
  reporter->writeReport(juce::Time::getCurrentTime(),
                        reason,
                        log != nullptr ? log->tryGetLines() : juce::StringArray{},
                        crashInfo);
}

void handleCrash(void* crashInfo) {
  report(describeCrash(crashInfo), crashInfo);
}

[[noreturn]] void handleTerminate() {
  juce::String reason = "std::terminate";
  if (const auto exception = std::current_exception()) {
    try {
      std::rethrow_exception(exception);
    } catch (const std::exception& e) {
      reason << ": uncaught exception: " << e.what();
    } catch (...) {
      reason << ": uncaught exception of unknown type";
    }
  }
  report(reason, nullptr);
  std::abort();
}

} // namespace

CrashReporter::CrashReporter(juce::File folder, juce::String appDescription)
    : folder_(std::move(folder)), appDescription_(std::move(appDescription)) {}

CrashReporter::~CrashReporter() {
  const CrashReporter* expected = this;
  if (gReporter.compare_exchange_strong(expected, nullptr)) {
    // JUCE's handler stays installed (it can't be removed) but now does
    // nothing; std::terminate gets its old handler back.
    gLog = nullptr;
    std::set_terminate(gPreviousTerminate);
  }
}

CrashReporter::PreviousSession CrashReporter::beginSession(int keepReports) {
  PreviousSession previous;
  const auto marker = folder_.getChildFile(kMarkerFileName);
  if (marker.existsAsFile()) {
    previous.endedCleanly = false;
    const auto started = marker.getLastModificationTime();
    for (const auto& file : reports()) {
      if (file.getLastModificationTime() >= started) {
        previous.crashReport = file;
        break;
      }
    }
  }

  folder_.createDirectory();
  marker.replaceWithText(juce::Time::getCurrentTime().toISO8601(true));

  // Oldest last: everything past `keepReports`, with its minidump.
  const auto all = reports();
  for (int i = std::max(0, keepReports); i < all.size(); ++i) {
    all[i].withFileExtension("dmp").deleteFile();
    all[i].deleteFile();
  }
  return previous;
}

void CrashReporter::endSession() {
  folder_.getChildFile(kMarkerFileName).deleteFile();
}

void CrashReporter::install(const RecentLog& log) {
  gLog = &log;
  const CrashReporter* expected = nullptr;
  if (!gReporter.compare_exchange_strong(expected, this)) {
    jassertfalse; // one reporter per process
    return;
  }
  juce::SystemStats::setApplicationCrashHandler(&handleCrash);
  gPreviousTerminate = std::set_terminate(&handleTerminate);
}

juce::File CrashReporter::writeReport(juce::Time time,
                                      const juce::String& reason,
                                      const juce::StringArray& recentLines,
                                      void* crashInfo) const {
  folder_.createDirectory();
  const auto stem = reportStem(time);
  const auto textFile = folder_.getChildFile(stem + ".txt");

  juce::String dumpLine;
#if JUCE_WINDOWS
  // The minidump first: it's the most useful part and needs the least of
  // a process that may have a corrupt heap.
  const auto dumpFile = textFile.withFileExtension("dmp");
  dumpLine = writeMinidump(dumpFile, crashInfo) ? dumpFile.getFileName()
                                                : juce::String("(could not be written)");
#else
  juce::ignoreUnused(crashInfo);
#endif

  juce::String text;
  text << appDescription_ << " crashed\n";
  text << "Time: " << time.toString(true, true, true, true) << " (" << time.toISO8601(true)
       << ")\n";
  text << "Reason: " << reason << "\n";
  if (dumpLine.isNotEmpty()) {
    text << "Minidump: " << dumpLine << "\n";
  }
  text << "\nSystem:\n" << describeSystem() << "\n";
  text << "\nStack:\n" << juce::SystemStats::getStackBacktrace() << "\n";
  text << "Recent log (" << recentLines.size() << " lines):\n"
       << recentLines.joinIntoString("\n") << "\n";
  textFile.replaceWithText(text, false, false, "\n");
  return textFile;
}

juce::Array<juce::File> CrashReporter::reports() const {
  auto files = folder_.findChildFiles(juce::File::findFiles, false, "crash-*.txt");
  // The names are timestamps, so name order is time order.
  std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
    return a.getFileName() > b.getFileName();
  });
  return files;
}

juce::String CrashReporter::reportStem(juce::Time time) {
  return "crash-" + time.formatted("%Y-%m-%d_%H-%M-%S");
}

juce::String CrashReporter::describeSystem() {
  using Stats = juce::SystemStats;
  juce::String text;
  text << "OS: " << Stats::getOperatingSystemName()
       << (Stats::isOperatingSystem64Bit() ? " (64-bit)" : "") << "\n";
  text << "CPU: " << Stats::getCpuModel().trim() << ", " << Stats::getNumPhysicalCpus()
       << " cores / " << Stats::getNumCpus() << " threads\n";
  text << "Memory: " << Stats::getMemorySizeInMegabytes() << " MB\n";
  text << "Language: " << Stats::getUserLanguage() << "-" << Stats::getUserRegion();
  return text;
}

} // namespace milkdawp::app
