include(${CMAKE_SOURCE_DIR}/cmake/FetchCatch2.cmake)

add_executable(milkdawp_ui_tests
  DrawerStateTests.cpp
  ShortcutsTests.cpp
  TransitionSettingsTests.cpp
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
