include(${CMAKE_SOURCE_DIR}/cmake/FetchCatch2.cmake)

add_executable(milkdawp_plugin_tests
  PluginProcessorTests.cpp
  LayersTests.cpp
  JuceEnvironment.cpp
)
milkdawp_test_no_crash_dialogs(milkdawp_plugin_tests)

target_include_directories(milkdawp_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_definitions(milkdawp_plugin_tests PRIVATE
  MILKDAWP_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures"
)

target_link_libraries(milkdawp_plugin_tests PRIVATE
  milkdawp_plugin
  milkdawp_core
  milkdawp_engine
  milkdawp_warnings
  juce::juce_audio_processors
  juce::juce_audio_utils
  Catch2::Catch2WithMain
)
milkdawp_link_external_zlib_libpng(milkdawp_plugin_tests)

# With projectM next to the binary, the Layers tests drive real engines and
# check real attach/detach; without it they only see inert ones and skip.
if(MILKDAWP_WITH_PROJECTM)
  milkdawp_deploy_projectm_runtime(milkdawp_plugin_tests)
endif()

include(CTest)
include(Catch)
catch_discover_tests(milkdawp_plugin_tests)
