cmake_minimum_required(VERSION 3.20)

# MilkDAWp - JUCE-based VST3 effect (scaffolding)
project(MilkDAWp VERSION 0.0.1 LANGUAGES C CXX)

# Options aligning with README technical details
option(MILKDAWP_BUILD_STANDALONE "Also build Standalone build target" OFF)
option(MILKDAWP_PREFER_STATIC_RUNTIME_MSVC "Link static MSVC runtime (/MT) where applicable" ON)
option(MILKDAWP_WITH_PROJECTM "Build and link with libprojectM visualization library" OFF)
# Control how libprojectM is linked when built from source (LGPL compliance recommends shared linking by default)
option(MILKDAWP_PROJECTM_LINK_STATIC "Link libprojectM statically when building it (may have LGPL obligations)" OFF)

# Multi-platform defaults to ensure builds on Windows, macOS, and Linux
if(APPLE)
  # Universal build (Intel + Apple Silicon) and reasonable deployment target
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Architectures for macOS builds" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
elseif(UNIX)
  # Linux: ensure position independent code and X11 usage for JUCE GUI modules
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_compile_definitions(JUCE_USE_X11=1)
endif()

# Configure MSVC runtime preference (static linking preference per README)
if(MSVC AND MILKDAWP_PREFER_STATIC_RUNTIME_MSVC)
  foreach(flag_var
          CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_DEBUG
          CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_DEBUG)
    if(${flag_var} MATCHES "/MD")
      string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endif()
    if(${flag_var} MATCHES "/MDd")
      string(REGEX REPLACE "/MDd" "/MTd" ${flag_var} "${${flag_var}}")
    endif()
  endforeach()
endif()

# Dependencies (JUCE, projectM) – set up as FetchContent placeholders.
# Note: Actual fetching requires internet; paths can be overridden by user/tooling.
include(FetchContent)

# JUCE
set(JUCE_GIT_REPOSITORY "https://github.com/juce-framework/JUCE.git" CACHE STRING "JUCE repository URL")
set(JUCE_GIT_TAG "7.0.12" CACHE STRING "JUCE tag/commit")

FetchContent_Declare(
  juce
  GIT_REPOSITORY ${JUCE_GIT_REPOSITORY}
  GIT_TAG        ${JUCE_GIT_TAG}
)

# Allow user to provide a local JUCE path to avoid network fetch
set(JUCE_LOCAL_PATH "" CACHE PATH "Path to a local JUCE checkout to use instead of fetching")
if(JUCE_LOCAL_PATH)
  add_subdirectory(${JUCE_LOCAL_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
  FetchContent_MakeAvailable(juce)
endif()

# libprojectM (optional)
set(PROJECTM_GIT_REPOSITORY "https://github.com/projectM-visualizer/projectm.git" CACHE STRING "projectM repository URL")
set(PROJECTM_GIT_TAG "v3.1.12" CACHE STRING "projectM tag/commit")
set(PROJECTM_LOCAL_PATH "" CACHE PATH "Path to a local projectM checkout to use instead of fetching")

if(MILKDAWP_WITH_PROJECTM)
  message(STATUS "MILKDAWP_WITH_PROJECTM=ON: Integrating libprojectM")
  if(PROJECTM_LOCAL_PATH)
    add_subdirectory(${PROJECTM_LOCAL_PATH} ${CMAKE_BINARY_DIR}/projectM)
  else()
    FetchContent_Declare(
      projectm
      GIT_REPOSITORY ${PROJECTM_GIT_REPOSITORY}
      GIT_TAG        ${PROJECTM_GIT_TAG}
    )
    FetchContent_MakeAvailable(projectm)
  endif()
endif()

# Basic target
set(PRODUCT_NAME "MilkDAWp")
set(COMPANY_NAME "Otitis Media")

# Enable CTest for test discovery
include(CTest)
enable_testing()

# Allow building the plugin separately from tests/CI to avoid GUI deps
option(MILKDAWP_BUILD_PLUGIN "Build the JUCE plugin target" ON)

# Choose plugin formats
if(MILKDAWP_BUILD_PLUGIN)
  set(MILKDAWP_PLUGIN_FORMATS VST3)
  if(MILKDAWP_BUILD_STANDALONE)
    list(APPEND MILKDAWP_PLUGIN_FORMATS Standalone)
  endif()

  # JUCE plugin target: VST3 audio effect; optionally Standalone in dev
  juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME        ${COMPANY_NAME}
    BUNDLE_ID           com.otitismedia.${PROJECT_NAME}
    IS_SYNTH            FALSE
    NEEDS_MIDI_INPUT    FALSE
    NEEDS_MIDI_OUTPUT   FALSE
    IS_MIDI_EFFECT      FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE OMda
    PLUGIN_CODE         Mlkw
    FORMATS             ${MILKDAWP_PLUGIN_FORMATS}
    PRODUCT_NAME        ${PRODUCT_NAME}
  )

  # Sources
  target_sources(${PROJECT_NAME}
    PRIVATE
      src/PluginProcessor.cpp
      src/PluginEditor.cpp
      src/Version.h
  )

  # Public compile definitions and config toggles
  target_compile_definitions(${PROJECT_NAME}
    PRIVATE
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
      JUCE_VST3_CAN_REPLACE_VST2=0
  )

  target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

  # Link JUCE modules we know we'll need as a base (extend in later phases)
  target_link_libraries(${PROJECT_NAME}
    PRIVATE
      juce::juce_audio_utils
      juce::juce_audio_basics
      juce::juce_audio_processors
      juce::juce_gui_basics
      juce::juce_gui_extra
      juce::juce_dsp
  )

  # Optional libprojectM linkage and feature define
  if(MILKDAWP_WITH_PROJECTM)
    # Try common target names created by projectM's CMake
    if(TARGET projectM::projectM)
      target_link_libraries(${PROJECT_NAME} PRIVATE projectM::projectM)
    elseif(TARGET projectM)
      target_link_libraries(${PROJECT_NAME} PRIVATE projectM)
    else()
      message(WARNING "MILKDAWP_WITH_PROJECTM=ON but no projectM CMake target found. Provide PROJECTM_LOCAL_PATH or ensure FetchContent succeeded.")
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE MILKDAWP_HAS_PROJECTM=1)
  else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE MILKDAWP_HAS_PROJECTM=0)
  endif()

  # Optionally also build standalone (useful during development; MVP requires it but Phase 0.1 focuses on VST3)
  if(MILKDAWP_BUILD_STANDALONE)
    juce_add_binary_data(${PROJECT_NAME}Assets SOURCES)
    target_link_libraries(${PROJECT_NAME} PRIVATE juce::juce_product_unlocking)
  endif()

  # Platform tweaks
  if(WIN32)
    # Ensure Unicode and lean Windows headers
    target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
  endif()
endif()

# Organize source in IDEs
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  src/Version.h
  src/Logging.h
)

# Tests: JUCE UnitTest runner
if(BUILD_TESTING)
  add_executable(${PROJECT_NAME}_tests
    tests/Main.cpp
    tests/SampleTests.cpp
    src/Version.h
    src/Logging.h
  )
  target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_17)
  target_link_libraries(${PROJECT_NAME}_tests PRIVATE juce::juce_core)
  add_test(NAME ${PROJECT_NAME}_tests COMMAND ${PROJECT_NAME}_tests)
endif()
