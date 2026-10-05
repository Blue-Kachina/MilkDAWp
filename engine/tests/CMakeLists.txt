include(${CMAKE_SOURCE_DIR}/cmake/FetchCatch2.cmake)

add_executable(milkdawp_engine_tests
  ProjectMLibraryTests.cpp
  RenderEngineTests.cpp
  PresetLoaderTests.cpp
  TransitionExecutorTests.cpp
  HeadlessRenderTests.cpp
  LayerCompositorTests.cpp
  PcmFeederTests.cpp
  PresetHandoffTests.cpp
  PresetCompileTimeCacheTests.cpp
  PresetMetadataStoreTests.cpp
  FrameReadbackExchangeTests.cpp
  ControlMappingTests.cpp
  DirectorTests.cpp
  RecentErrorsTests.cpp
  BundledContentTests.cpp
  UpdateCheckTests.cpp
)
milkdawp_test_no_crash_dialogs(milkdawp_engine_tests)
target_compile_definitions(milkdawp_engine_tests PRIVATE
  MILKDAWP_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures"
)

target_link_libraries(milkdawp_engine_tests PRIVATE
  milkdawp_engine
  milkdawp_warnings
  milkdawp_sanitizers
  Catch2::Catch2WithMain
)
milkdawp_link_external_zlib_libpng(milkdawp_engine_tests)

# With projectM next to the test binary, the render-thread and headless
# render tests (2.7) exercise the real library instead of only the
# "unavailable" branch.
if(MILKDAWP_WITH_PROJECTM)
  milkdawp_deploy_projectm_runtime(milkdawp_engine_tests)
endif()

include(CTest)
include(Catch)
catch_discover_tests(milkdawp_engine_tests)

# BundledContentTests (6.1): the scale tests use the pack the build fetched.
if(MILKDAWP_BUNDLE_CONTENT)
  target_compile_definitions(milkdawp_engine_tests PRIVATE MILKDAWP_TEST_CONTENT_DIR="${MILKDAWP_CONTENT_DIR}")
endif()
