add_library(milkdawp_engine STATIC
  include/milkdawp/engine/EngineInfo.h
  src/EngineInfo.cpp
  include/milkdawp/engine/ProjectMLibrary.h
  src/ProjectMLibrary.cpp
  include/milkdawp/engine/RenderEngine.h
  src/RenderEngine.cpp
  include/milkdawp/engine/PresetLoader.h
  src/PresetLoader.cpp
  include/milkdawp/engine/TransitionExecutor.h
  src/TransitionExecutor.cpp
  include/milkdawp/engine/OutputSurface.h
  src/OutputSurface.cpp
)

target_include_directories(milkdawp_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(milkdawp_engine PUBLIC cxx_std_20)
target_link_libraries(milkdawp_engine
  PUBLIC
    milkdawp_core
    milkdawp_sanitizers
    juce::juce_core   # ProjectMLibrary.h is a public header exposing juce::File/DynamicLibrary
    juce::juce_opengl # RenderEngine.h/OutputSurface.h are public headers exposing juce::OpenGLContext
  PRIVATE
    milkdawp_warnings
)
milkdawp_link_external_zlib_libpng(milkdawp_engine)

# ProjectMLibrary (Phase 2.1) deliberately never links MILKDAWP_PROJECTM_TARGET
# -- it resolves every symbol at runtime via juce::DynamicLibrary/GetProcAddress
# (see that class's header comment) specifically so a build compiles and loads
# cleanly with no projectM SDK present at all. This target check only records
# whether the SDK was available at configure time for EngineInfo::hasProjectM();
# it must NOT actually link the target -- doing so used to pull vcpkg's
# zlib/libpng in transitively (the whole reason SingleZlibLibpngCheck.cmake's
# external-zlib/libpng path existed), which broke loading in real, independent
# VST3 hosts (REAPER, Cubase) even though it passed pluginval and Standalone --
# see development_roadmap.md §4.11 for the full investigation.
if(MILKDAWP_WITH_PROJECTM AND MILKDAWP_PROJECTM_TARGET AND TARGET ${MILKDAWP_PROJECTM_TARGET})
  target_compile_definitions(milkdawp_engine PRIVATE MILKDAWP_HAS_PROJECTM=1)
else()
  target_compile_definitions(milkdawp_engine PRIVATE MILKDAWP_HAS_PROJECTM=0)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
