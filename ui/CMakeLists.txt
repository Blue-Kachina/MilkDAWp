add_library(milkdawp_ui STATIC
  include/milkdawp/ui/UiInfo.h
  src/UiInfo.cpp
  include/milkdawp/ui/DrawerState.h
  src/DrawerState.cpp
  include/milkdawp/ui/DrawerScrim.h
  src/DrawerScrim.cpp
  include/milkdawp/ui/ControlDrawer.h
  src/ControlDrawer.cpp
  include/milkdawp/ui/Shortcuts.h
  src/Shortcuts.cpp
  include/milkdawp/ui/TransitionSettings.h
  src/TransitionSettings.cpp
  include/milkdawp/ui/OutputSettings.h
  src/OutputSettings.cpp
  include/milkdawp/ui/DetachedControlsWindow.h
  src/DetachedControlsWindow.cpp
  include/milkdawp/ui/Icons.h
  src/Icons.cpp
  include/milkdawp/ui/IconButton.h
  src/IconButton.cpp
  include/milkdawp/ui/DrawerLookAndFeel.h
  src/DrawerLookAndFeel.cpp
  include/milkdawp/ui/PresetMenu.h
  src/PresetMenu.cpp
  include/milkdawp/ui/PresetSearch.h
  src/PresetSearch.cpp
  include/milkdawp/ui/PresetBrowser.h
  src/PresetBrowser.cpp
  include/milkdawp/ui/PresetInfoMenu.h
  src/PresetInfoMenu.cpp
  include/milkdawp/ui/KeyboardNavigation.h
  src/KeyboardNavigation.cpp
  include/milkdawp/ui/DiagnosticsPanel.h
  src/DiagnosticsPanel.cpp
  include/milkdawp/ui/AboutPanel.h
  src/AboutPanel.cpp
)

target_include_directories(milkdawp_ui PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(milkdawp_ui PUBLIC cxx_std_20)
target_compile_definitions(milkdawp_ui PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)
target_link_libraries(milkdawp_ui
  PUBLIC
    milkdawp_core
  PRIVATE
    milkdawp_warnings
    juce::juce_gui_basics
    juce::juce_gui_extra
)
milkdawp_link_external_zlib_libpng(milkdawp_ui)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
