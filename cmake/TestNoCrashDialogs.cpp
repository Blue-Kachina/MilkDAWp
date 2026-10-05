// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Linked into every test executable (milkdawp_test_no_crash_dialogs in
// cmake/FetchCatch2.cmake). On Windows, a debug CRT assertion, abort() or a
// crash in a test otherwise opens a modal dialog ("Debug Assertion
// Failed!", Windows Error Reporting) that waits for someone to click it, and
// an unattended ctest run stalls on it forever. With this, they are reported
// on stderr and the test process fails like any other crash.

#if defined(_WIN32)

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

[[maybe_unused]] const bool kNoCrashDialogs = [] {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
  for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
  }
#endif
  return true;
}();

} // namespace

#endif
