# cmake/SingleZlibLibpngCheck.cmake
#
# JUCE 9 compiles its bundled zlib/libpng as C, not wrapped in a C++ namespace
# (§4.11 of development_roadmap.md), which risks an ODR violation if vcpkg's
# zlib/libpng also link into the same binary.
#
# History (D4/§4.11, revised 2026-09-26): this used to default ON whenever
# projectM was enabled, on the assumption that projectM/freetype pulled
# vcpkg's zlib/libpng in transitively. That assumption was wrong: projectM is
# never actually linked (ProjectMLibrary loads it entirely at runtime via
# juce::DynamicLibrary), so there was never a second copy to conflict with.
# Worse, forcing external zlib/libpng introduced real, separate DLLs
# (z.dll/libpng16.dll) next to the plugin binary, and that broke loading in
# real, independent VST3 hosts (REAPER, Cubase both silently rejected it)
# even though it passed pluginval and the Standalone build every time --
# see development_roadmap.md §4.11 for the full investigation. Defaulting
# this OFF restores JUCE's own bundled (statically-compiled, no separate DLL)
# zlib/libpng, which real hosts load fine. Left as a manual option rather
# than deleted in case a future dependency genuinely needs to link its own
# zlib/libpng at build time.
option(MILKDAWP_JUCE_ZLIB_LIBPNG_FROM_VCPKG
  "Disable JUCE's bundled zlib/libpng and rely on vcpkg's copies instead (only needed if something else genuinely links its own zlib/libpng at build time, §4.11)"
  OFF)

if(MILKDAWP_JUCE_ZLIB_LIBPNG_FROM_VCPKG)
  add_compile_definitions(JUCE_INCLUDE_ZLIB_CODE=0 JUCE_INCLUDE_PNGLIB_CODE=0)
  find_package(ZLIB REQUIRED)
  find_package(PNG REQUIRED)
endif()

# milkdawp_link_external_zlib_libpng(<target>)
#
# Links vcpkg's zlib/libpng into <target> when MILKDAWP_JUCE_ZLIB_LIBPNG_FROM_VCPKG
# is ON (required once JUCE's bundled copies are disabled above); a no-op
# otherwise. Call this on every target that links a JUCE module using zlib or
# libpng (juce_core, juce_graphics) -- i.e. milkdawp_engine, milkdawp_ui,
# milkdawp_plugin, milkdawp_app.
function(milkdawp_link_external_zlib_libpng target)
  if(MILKDAWP_JUCE_ZLIB_LIBPNG_FROM_VCPKG)
    target_link_libraries(${target} PRIVATE ZLIB::ZLIB PNG::PNG)
  endif()
endfunction()

# milkdawp_check_single_zlib_libpng(<target>)
#
# Adds a POST_BUILD step enforcing the decision above at link time: it lists
# <target>'s shared-library dependencies and fails the build if more than one
# zlib or more than one libpng copy would load at runtime.
#
# Call this on every final linked binary (plugin, app, mdw-analyze, engine
# tests) once those targets exist -- there is nothing to check yet in Phase 0.
function(milkdawp_check_single_zlib_libpng target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DMILKDAWP_CHECK_TARGET_FILE=$<TARGET_FILE:${target}>"
      "-DMILKDAWP_CHECK_PLATFORM=${CMAKE_SYSTEM_NAME}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/check_single_zlib_libpng.cmake"
    VERBATIM
    COMMENT "Checking ${target} links exactly one zlib and one libpng copy"
  )
endfunction()
