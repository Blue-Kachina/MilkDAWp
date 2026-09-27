# milkdawp_app: the standalone app shell (Phase 4, D8, ADR-0003). Everything
# but the entry point is a static library so app/tests can link it.
#
# Identity follows the plugin (D1): product "MilkDAWp", bundle ID
# com.otitismedia.MilkDAWp. MILKDAWP_DEV_ALT_IDENTITY (ADR-0007) renames it
# and its settings folder so a dev build never shares a real install's
# preferences.
if(MILKDAWP_DEV_ALT_IDENTITY)
  set(_mdw_app_name      "MilkDAWp2 Dev")
  set(_mdw_app_bundle_id "com.otitismedia.MilkDAWp2Dev.app")
else()
  set(_mdw_app_name      "MilkDAWp")
  set(_mdw_app_bundle_id "com.otitismedia.MilkDAWp.app")
endif()

add_library(milkdawp_app_lib STATIC
  src/AppPreferences.h
  src/AppPreferences.cpp
  src/AudioInput.h
  src/AudioInput.cpp
  src/AudioSettingsPanel.h
  src/AudioSettingsPanel.cpp
  src/MainComponent.h
  src/MainComponent.cpp
  src/MainWindow.h
  src/MainWindow.cpp
  src/ParameterBinding.h
  src/ParameterBinding.cpp
  src/SignalMonitor.h
)
target_include_directories(milkdawp_app_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(milkdawp_app_lib PUBLIC cxx_std_20)
target_compile_definitions(milkdawp_app_lib PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)
target_link_libraries(milkdawp_app_lib
  PUBLIC
    milkdawp_core
    milkdawp_engine
    milkdawp_ui
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
  PRIVATE
    milkdawp_warnings
)
milkdawp_link_external_zlib_libpng(milkdawp_app_lib)

juce_add_gui_app(milkdawp_app
  PRODUCT_NAME "${_mdw_app_name}"
  COMPANY_NAME "Otitis Media"
  BUNDLE_ID    ${_mdw_app_bundle_id}
  # Microphone access (macOS asks for permission on first input).
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "MilkDAWp listens to your audio input to animate the visuals. Nothing is recorded."
)

target_sources(milkdawp_app PRIVATE src/Main.cpp)
target_compile_features(milkdawp_app PRIVATE cxx_std_20)
target_compile_definitions(milkdawp_app PRIVATE
  JUCE_APPLICATION_NAME_STRING="${_mdw_app_name}"
  JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
  MILKDAWP_APP_DATA_FOLDER="${_mdw_app_name}"
)

target_link_libraries(milkdawp_app PRIVATE
  milkdawp_app_lib
  milkdawp_warnings
)
milkdawp_link_external_zlib_libpng(milkdawp_app)
milkdawp_check_single_zlib_libpng(milkdawp_app)

# Both helpers live in cmake/ProjectMDependency.cmake, which is only included
# with projectM.
if(MILKDAWP_WITH_PROJECTM)
  milkdawp_deploy_projectm_runtime(milkdawp_app)
  milkdawp_check_runtime_layout(milkdawp_app)
endif()

unset(_mdw_app_name)
unset(_mdw_app_bundle_id)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
