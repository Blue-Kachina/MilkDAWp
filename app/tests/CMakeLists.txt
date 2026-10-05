include(${CMAKE_SOURCE_DIR}/cmake/FetchCatch2.cmake)

add_executable(milkdawp_app_tests
  AppPreferencesTests.cpp
  CrashReporterTests.cpp
  LogBundleTests.cpp
  MidiLearnTests.cpp
  ParameterBindingTests.cpp
  RecentLogTests.cpp
  SignalMonitorTests.cpp
  SystemAudioCaptureTests.cpp
)
milkdawp_test_no_crash_dialogs(milkdawp_app_tests)

target_link_libraries(milkdawp_app_tests PRIVATE
  milkdawp_app_lib
  milkdawp_warnings
  Catch2::Catch2WithMain
)
milkdawp_link_external_zlib_libpng(milkdawp_app_tests)

include(CTest)
include(Catch)
catch_discover_tests(milkdawp_app_tests)
