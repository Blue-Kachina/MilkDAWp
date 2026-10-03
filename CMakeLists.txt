cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# vcpkg toolchain fallback for configures that bypass CMakePresets.json.
#
# The presets (dev-linux/dev-mac/dev-win, ci-*) set CMAKE_TOOLCHAIN_FILE,
# VCPKG_TARGET_TRIPLET, and VCPKG_OVERLAY_TRIPLETS explicitly. Some IDE
# integrations (observed: CLion's default "Debug" CMake profile via
# JetBrains Gateway/remote dev) run a plain `cmake -S -B` invocation instead
# of picking up a preset, which silently drops all three -- projectM then
# fails to resolve because it was installed under a vcpkg triplet that was
# never named (cmake/ProjectMDependency.cmake:19-22). Fill in the same
# defaults the presets use, from the container's VCPKG_ROOT, whenever they
# haven't already been supplied by a preset or -D on the command line. This
# must run before project() for CMAKE_TOOLCHAIN_FILE to take effect.
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE FILEPATH "Vcpkg toolchain file")

  if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(CMAKE_HOST_WIN32)
      set(_mdw_default_triplet "x64-windows-dynamic")
    elseif(CMAKE_HOST_APPLE)
      if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        set(_mdw_default_triplet "arm64-osx-dynamic")
      else()
        set(_mdw_default_triplet "x64-osx-dynamic")
      endif()
    else()
      set(_mdw_default_triplet "x64-linux-dynamic")
    endif()
    set(VCPKG_TARGET_TRIPLET "${_mdw_default_triplet}" CACHE STRING "Vcpkg target triplet")
    unset(_mdw_default_triplet)
  endif()

  if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
    set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/triplets" CACHE STRING "Vcpkg overlay triplets")
  endif()

  message(STATUS
    "MilkDAWp: no CMAKE_TOOLCHAIN_FILE given (no preset used?) -- defaulting to "
    "vcpkg at $ENV{VCPKG_ROOT}, triplet ${VCPKG_TARGET_TRIPLET}")
endif()

# MilkDAWp 2 -- ground-up rebuild of MilkDAWp (see development_roadmap.md).
# This is Phase 0.1: top-level project setup, dependency plumbing, and build
# presets. milkdawp_core/engine/ui/plugin/app do not exist yet (Phase 0.2).
project(MilkDAWp VERSION 2.0.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not supported. Delete CMakeCache.txt and "
    "CMakeFiles/, then configure into a separate build directory (the "
    "CMakePresets.json presets already do this for you).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Options (D1-D14 in development_roadmap.md §5)
# ---------------------------------------------------------------------------
option(MILKDAWP_BUILD_PLUGIN "Build the plugin target (VST3/AU/Standalone, Phase 3)" ON)
option(MILKDAWP_BUILD_APP "Build the standalone app target (Phase 4)" ON)
option(MILKDAWP_PLUGIN_STANDALONE
  "Dev-only: also build the plugin as a JUCE Standalone (the plugin editor without a DAW). The shipped app is milkdawp_app; release presets turn this off (4.11)." ON)
option(MILKDAWP_WITH_PROJECTM "Link libprojectM into the engine (D7)" ON)
option(MILKDAWP_DEV_ALT_IDENTITY
  "Dev-only: build under a distinct plugin code/name so it can coexist with a real v1/v2 install in the same host (ADR-0007). Never set for a release build." OFF)

# ---------------------------------------------------------------------------
# Platform defaults
# ---------------------------------------------------------------------------
if(APPLE)
  # Universal build; D9 sets the macOS floor at 12+.
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Architectures for macOS builds")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
elseif(UNIX)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(MSVC)
  # Dynamic CRT throughout; matches the *-dynamic vcpkg triplets in triplets/.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC runtime")
endif()

# ---------------------------------------------------------------------------
# cmake/ modules
# ---------------------------------------------------------------------------
include(cmake/Warnings.cmake)              # milkdawp_warnings interface target (D5)
include(cmake/Sanitizers.cmake)            # milkdawp_sanitizers interface target (0.4, §8)
include(cmake/SingleZlibLibpngCheck.cmake) # §4.11 zlib/libpng single-copy decision + link check
include(cmake/FetchJuce.cmake)             # JUCE 9 via FetchContent, pinned tag+hash (D4)

if(MILKDAWP_WITH_PROJECTM)
  include(cmake/ProjectMDependency.cmake)  # vcpkg projectM, validated SHARED (D10)
endif()

include(CTest)
enable_testing()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(core)                  # milkdawp_core            [0.2, Phase 1]
add_subdirectory(engine)                # milkdawp_engine          [0.2, Phase 2]
add_subdirectory(ui)                    # milkdawp_ui              [0.2, Phase 2.11]
if(MILKDAWP_BUILD_PLUGIN)
  add_subdirectory(plugin)              # milkdawp_plugin          [0.2, Phase 3]
endif()
if(MILKDAWP_BUILD_APP)
  add_subdirectory(app)                 # milkdawp_app             [0.2, Phase 4]
endif()
add_subdirectory(tools/mdw-analyze)     # offline analysis CLI     [0.2, Phase 1]
add_subdirectory(tools/generate-param-docs) # docs/parameters.md generator [1.13]
add_subdirectory(tools/generate-fixtures)   # fixtures/ generator      [1.9]
option(MILKDAWP_BUILD_MDW_VIEW "Build the mdw-view developer viewer (Phase 2.9)" ON)
if(MILKDAWP_BUILD_MDW_VIEW)
  add_subdirectory(tools/mdw-view)      # dev viewer: WAV -> engine -> window [2.9]
endif()
