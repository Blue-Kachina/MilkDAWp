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

# SystemAudioCapture (§4.7): one implementation file per platform, chosen
# here at build time so a platform's audio API never leaks into another
# platform's build. Android's (Phase 7, ADR-0010) plugs in the same way.
if(WIN32)
  set(_mdw_system_audio_capture_src src/SystemAudioCaptureWindows.cpp)
else()
  set(_mdw_system_audio_capture_src src/SystemAudioCaptureUnsupported.cpp)
endif()

add_library(milkdawp_app_lib STATIC
  src/AppPreferences.h
  src/AppPreferences.cpp
  src/AudioInput.h
  src/AudioInput.cpp
  src/AudioPeakReader.h
  src/AudioSettingsPanel.h
  src/AudioSettingsPanel.cpp
  src/AudioSourceRouter.h
  src/CrashReporter.h
  src/CrashReporter.cpp
  src/LogBundle.h
  src/LogBundle.cpp
  src/MainComponent.h
  src/MainComponent.cpp
  src/MainWindow.h
  src/MainWindow.cpp
  src/MidiLearn.h
  src/MidiLearn.cpp
  src/ParameterBinding.h
  src/ParameterBinding.cpp
  src/PresetBrowserPanel.h
  src/PresetBrowserPanel.cpp
  src/RecentLog.h
  src/RecentLog.cpp
  src/SignalMonitor.h
  src/SystemAudioCapture.h
  ${_mdw_system_audio_capture_src}
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
if(WIN32)
  # SystemAudioCaptureWindows.cpp: CoCreateInstance/CoInitializeEx/
  # PropVariantClear for WASAPI loopback capture. CrashReporter.cpp:
  # MiniDumpWriteDump.
  target_link_libraries(milkdawp_app_lib PRIVATE ole32 dbghelp)
endif()

juce_add_gui_app(milkdawp_app
  PRODUCT_NAME "${_mdw_app_name}"
  COMPANY_NAME "Otitis Media"
  BUNDLE_ID    ${_mdw_app_bundle_id}
  # Microphone access (macOS asks for permission on first input).
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "MilkDAWp listens to your audio input to animate the visuals. Nothing is recorded."
  # §4.6: macOS Info.plist document-type metadata for double-clicking a
  # .milk file. Windows/Linux registry/desktop-file registration is
  # installer work (§9, not started); Main.cpp already handles the file or
  # folder this hands it either way.
  DOCUMENT_EXTENSIONS "milk"
)

target_sources(milkdawp_app PRIVATE src/Main.cpp)
target_compile_features(milkdawp_app PRIVATE cxx_std_20)
target_compile_definitions(milkdawp_app PRIVATE
  JUCE_APPLICATION_NAME_STRING="${_mdw_app_name}"
  JUCE_APPLICATION_VERSION_STRING="${MILKDAWP_VERSION_LABEL}"
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
unset(_mdw_system_audio_capture_src)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
