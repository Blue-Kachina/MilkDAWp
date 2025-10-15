cmake_minimum_required(VERSION 3.22)

# MilkDAWp - JUCE-based VST3 effect (scaffolding)
project(MilkDAWp VERSION 0.0.1 LANGUAGES C CXX)

# Copy built plugin to a VST3 folder after build (helps DAWs like Cubase discover it)
# Default: copy to the per-user VST3 directory to avoid requiring admin rights.
# You can:
#  - Set MILKDAWP_VST3_OVERRIDE_COPY_DIR to an absolute path (preferred for custom Cubase paths), OR
#  - Turn OFF MILKDAWP_VST3_COPY_TO_USER_DIR to use JUCE's default (CommonProgramFiles/Common Files/VST3), OR
#  - Leave defaults to use %APPDATA%/VST3 (per-user).
option(MILKDAWP_VST3_COPY_TO_USER_DIR "Copy VST3 to per-user VST3 directory (%APPDATA%/VST3) after build" ON)
set(MILKDAWP_VST3_OVERRIDE_COPY_DIR "" CACHE PATH "Override VST3 copy destination (e.g. C:/Users/Admin/AppData/Local/Programs/Common/VST3)")
# Enable JUCE's post-build copy step
set_property(DIRECTORY PROPERTY JUCE_COPY_PLUGIN_AFTER_BUILD TRUE)
# Choose destination order on Windows
if(WIN32)
  if(MILKDAWP_VST3_OVERRIDE_COPY_DIR)
    # Custom override provided by the developer/user
    set_property(DIRECTORY PROPERTY JUCE_VST3_COPY_DIR "${MILKDAWP_VST3_OVERRIDE_COPY_DIR}")
  elseif(MILKDAWP_VST3_COPY_TO_USER_DIR)
    # Per-user VST3 location recognized by the VST3 spec
    # JUCE expects forward slashes; CMake will normalize for Windows
    set_property(DIRECTORY PROPERTY JUCE_VST3_COPY_DIR "$ENV{APPDATA}/VST3")
  endif()
endif()

# Options aligning with README technical details (Phase 2 updates)
option(MILKDAWP_BUILD_STANDALONE "Also build Standalone build target" OFF)
# Phase 2: Prefer dynamic runtime and dynamic projectM by default
option(MILKDAWP_WITH_PROJECTM "Build and link with libprojectM visualization library" ON)
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

# Phase 2: Enforce dynamic MSVC runtime (/MD). Static runtime is no longer supported.
if(MSVC)
  # Use modern CMake variable to control runtime library
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC runtime" FORCE)
endif()

# Dependencies (JUCE, projectM) – managed via vcpkg
# Versions and baseline are locked in vcpkg.json and vcpkg-configuration.json

# Find JUCE via vcpkg
find_package(JUCE CONFIG REQUIRED)
message(STATUS "Found JUCE via vcpkg")

# Find projectM via vcpkg when enabled
if(MILKDAWP_WITH_PROJECTM)
  message(STATUS "MILKDAWP_WITH_PROJECTM=ON: Finding libprojectM via vcpkg (shared)")

  # Phase 2: Require shared linkage. Do not allow static for projectM.
  if(MILKDAWP_PROJECTM_LINK_STATIC)
    message(FATAL_ERROR "MILKDAWP_PROJECTM_LINK_STATIC=ON is not supported. Please use shared linking for libprojectM (LGPL).")
  endif()

  # Find projectM package: vcpkg may expose it as projectM4 with target libprojectM::projectM
  # or as projectm with target projectM::projectM / projectm::projectm depending on port version.
  find_package(projectM4 CONFIG QUIET)
  if(NOT projectM4_FOUND)
    find_package(projectm CONFIG QUIET)
  endif()
  if(NOT projectM4_FOUND AND NOT projectm_FOUND)
    message(FATAL_ERROR "libprojectM was requested but neither projectM4 nor projectm CMake packages were found. Ensure vcpkg installed projectm.")
  endif()

  # Validate projectM target presence and that it is a SHARED library
  set(_PROJECTM_TARGET "")
  if(TARGET libprojectM::projectM)
    set(_PROJECTM_TARGET libprojectM::projectM)
  elseif(TARGET projectM::projectM)
    set(_PROJECTM_TARGET projectM::projectM)
  elseif(TARGET projectm::projectm)
    set(_PROJECTM_TARGET projectm::projectm)
  elseif(TARGET projectM)
    set(_PROJECTM_TARGET projectM)
  elseif(TARGET projectm)
    set(_PROJECTM_TARGET projectm)
  endif()

  if(NOT _PROJECTM_TARGET)
    message(FATAL_ERROR "libprojectM was requested but no known CMake target was found (tried libprojectM::projectM, projectM::projectM, projectm::projectm, projectM, projectm).")
  endif()

  get_target_property(_PM_TYPE ${_PROJECTM_TARGET} TYPE)
  if(NOT _PM_TYPE STREQUAL "SHARED_LIBRARY" AND NOT _PM_TYPE STREQUAL "UNKNOWN_LIBRARY")
    message(WARNING "libprojectM target '${_PROJECTM_TARGET}' type is ${_PM_TYPE}. Expected SHARED_LIBRARY for LGPL compliance.")
  endif()

  message(STATUS "Found projectM target: ${_PROJECTM_TARGET}")
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
    VST3_CATEGORIES     "Fx" "Analyzer"
    PRODUCT_NAME        ${PRODUCT_NAME}
  )

  # Sources
  target_sources(${PROJECT_NAME}
    PRIVATE
      src/PluginProcessor.cpp
      src/PluginEditor.cpp
      src/Version.h
      src/Logging.h
      src/AudioAnalysisQueue.h
      src/VisualizationThread.h
      src/ThreadSafeQueue.h
      src/MessageThreadBridge.h
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
      juce::juce_opengl
  )

  # Optional libprojectM linkage and feature define (Phase 2 enforces presence/type when enabled)
  if(MILKDAWP_WITH_PROJECTM)
    if(DEFINED _PROJECTM_TARGET AND TARGET ${_PROJECTM_TARGET})
      target_link_libraries(${PROJECT_NAME} PRIVATE ${_PROJECTM_TARGET})
    else()
      message(FATAL_ERROR "MILKDAWP_WITH_PROJECTM=ON but no projectM CMake target variable was set. This should have been configured earlier.")
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

  # Phase 3 — Platform packaging/runtime loading for libprojectM
  # We bundle the projectM shared library next to the plugin so the host can resolve it at scan/load time.
  if(MILKDAWP_WITH_PROJECTM)
    if(WIN32)
      if(DEFINED _PROJECTM_TARGET AND TARGET ${_PROJECTM_TARGET})
        # Copy projectM DLL next to the built VST3 file in the artefacts output directory
        add_custom_command(TARGET ${PROJECT_NAME}_VST3 POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${_PROJECTM_TARGET}> $<TARGET_FILE_DIR:${PROJECT_NAME}_VST3>
          VERBATIM)

        # Also copy to JUCE's post-build VST3 copy directory (if configured), so DAWs scanning that location can resolve the DLL
        get_property(_JUCE_VST3_COPY_DIR DIRECTORY PROPERTY JUCE_VST3_COPY_DIR)
        if(_JUCE_VST3_COPY_DIR)
          # Some JUCE generators may include stray quotes in JUCE_VST3_COPY_DIR; strip any trailing quotes to avoid path"" issues
          string(REGEX REPLACE "\"$" "" _JUCE_VST3_COPY_DIR "${_JUCE_VST3_COPY_DIR}")
          add_custom_command(TARGET ${PROJECT_NAME}_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_JUCE_VST3_COPY_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${_PROJECTM_TARGET}> ${_JUCE_VST3_COPY_DIR}
            VERBATIM)
        endif()

        # Also copy ALL runtime DLLs this plugin depends on (transitive), so hosts can resolve them during scan/load
        add_custom_command(TARGET ${PROJECT_NAME}_VST3 POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}_VST3> $<TARGET_FILE_DIR:${PROJECT_NAME}_VST3>
          COMMAND_EXPAND_LISTS
          VERBATIM)
        if(_JUCE_VST3_COPY_DIR)
          add_custom_command(TARGET ${PROJECT_NAME}_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}_VST3> ${_JUCE_VST3_COPY_DIR}
            COMMAND_EXPAND_LISTS
            VERBATIM)
        endif()

        # Note: We intentionally DO NOT rename the DLL (e.g., to projectM-milkdawp.dll) on Windows.
        # The dependent DLL name is embedded at link time; renaming would break binding unless we switch to manual LoadLibrary.
        # TODO (future): consider delay-loading and manual resolution to support private, uniquely named copies.
      endif()
    elseif(APPLE)
      # macOS: copy libprojectM.dylib into the bundle Frameworks dir and set rpath accordingly
      # Bundle layout: MilkDAWp.vst3/Contents/MacOS/<binary>; frameworks at MilkDAWp.vst3/Contents/Frameworks
      if(DEFINED _PROJECTM_TARGET AND TARGET ${_PROJECTM_TARGET})
        # Ensure rpath points to Frameworks so the loader can resolve libprojectM at runtime
        target_link_options(${PROJECT_NAME}_VST3 PRIVATE "-Wl,-rpath,@loader_path/../Frameworks")
        add_custom_command(TARGET ${PROJECT_NAME}_VST3 POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}_VST3>/../Frameworks"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${_PROJECTM_TARGET}> "$<TARGET_FILE_DIR:${PROJECT_NAME}_VST3>/../Frameworks/"
          VERBATIM)
        # Note: Codesigning steps are not added here to keep Phase 3 minimal and cross-platform-safe.
      endif()
    elseif(UNIX)
      # Linux: place libprojectM.so under .../YourPlugin.vst3/Contents/Resources/lib and add rpath
      if(DEFINED _PROJECTM_TARGET AND TARGET ${_PROJECTM_TARGET})
        target_link_options(${PROJECT_NAME}_VST3 PRIVATE "-Wl,-rpath,$ORIGIN/../Resources/lib")
        add_custom_command(TARGET ${PROJECT_NAME}_VST3 POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}_VST3>/../Resources/lib"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${_PROJECTM_TARGET}> "$<TARGET_FILE_DIR:${PROJECT_NAME}_VST3>/../Resources/lib/"
          VERBATIM)
      endif()
    endif()
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
    tests/VisualizationThreadTests.cpp
    tests/AudioAnalysisQueueTests.cpp
    tests/MessageThreadTests.cpp
    tests/StateTests.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Version.h
    src/Logging.h
    src/AudioAnalysisQueue.h
    src/VisualizationThread.h
    src/ThreadSafeQueue.h
    src/MessageThreadBridge.h
  )
  target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_17)
  target_link_libraries(${PROJECT_NAME}_tests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_opengl)
  add_test(NAME ${PROJECT_NAME}_tests COMMAND ${PROJECT_NAME}_tests)
endif()
