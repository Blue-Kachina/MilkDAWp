include(${CMAKE_SOURCE_DIR}/cmake/FetchCatch2.cmake)

add_executable(milkdawp_ui_tests
  DrawerStateTests.cpp
  ShortcutsTests.cpp
  TransitionSettingsTests.cpp
  OutputSettingsTests.cpp
  IconsTests.cpp
  ControlDrawerLayoutTests.cpp
  PresetMenuTests.cpp
  PresetInfoMenuTests.cpp
  DiagnosticsPanelTests.cpp
  AccessibilityTests.cpp
  PresetBrowserTests.cpp
  AboutPanelTests.cpp
)
milkdawp_test_no_crash_dialogs(milkdawp_ui_tests)

# Same as milkdawp_ui: this target compiles JUCE's module sources too, and
# without these Linux links them against libcurl/WebKit.
target_compile_definitions(milkdawp_ui_tests PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)

target_link_libraries(milkdawp_ui_tests PRIVATE
  milkdawp_ui
  milkdawp_warnings
  juce::juce_gui_basics
  Catch2::Catch2WithMain
)
milkdawp_link_external_zlib_libpng(milkdawp_ui_tests)

include(CTest)
include(Catch)
catch_discover_tests(milkdawp_ui_tests)
