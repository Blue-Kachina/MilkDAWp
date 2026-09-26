# mdw-view (Phase 2.9): developer viewer. WAV in -> speakers + engine ->
# video-first window with the shared ControlDrawer. Not shipped.
juce_add_gui_app(mdw-view
  PRODUCT_NAME "mdw-view"
  COMPANY_NAME "Otitis Media"
  BUNDLE_ID    com.otitismedia.mdw-view
)

target_sources(mdw-view PRIVATE src/Main.cpp)
target_compile_features(mdw-view PRIVATE cxx_std_20)
target_compile_definitions(mdw-view PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_APPLICATION_NAME_STRING="mdw-view"
  JUCE_APPLICATION_VERSION_STRING="0.1"
  # Default preset folder: the repo's own test presets.
  MILKDAWP_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures"
)

target_link_libraries(mdw-view PRIVATE
  milkdawp_core
  milkdawp_engine
  milkdawp_ui
  milkdawp_warnings
  juce::juce_audio_utils
  juce::juce_audio_devices
  juce::juce_audio_formats
  juce::juce_gui_extra
)
milkdawp_link_external_zlib_libpng(mdw-view)
milkdawp_check_single_zlib_libpng(mdw-view)

if(MILKDAWP_WITH_PROJECTM)
  milkdawp_deploy_projectm_runtime(mdw-view)
endif()
