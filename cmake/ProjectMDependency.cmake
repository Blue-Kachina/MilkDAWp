# cmake/ProjectMDependency.cmake
#
# Locates libprojectM (vcpkg-provided; see vcpkg.json) and validates it is a
# SHARED library, which projectM's LGPL-2.1 licence requires when dynamically
# linked into MilkDAWp's AGPL-3.0-or-later binary (D10). The triplets/*.cmake
# overlay triplets force this; this check catches a misconfigured triplet.
#
# Exposes MILKDAWP_PROJECTM_TARGET for consumers once milkdawp_engine exists.

if(NOT MILKDAWP_WITH_PROJECTM)
  return()
endif()

find_package(projectM4 CONFIG QUIET)
if(NOT projectM4_FOUND)
  find_package(projectm CONFIG QUIET)
endif()
if(NOT projectM4_FOUND AND NOT projectm_FOUND)
  message(FATAL_ERROR
    "MILKDAWP_WITH_PROJECTM=ON but neither the projectM4 nor projectm vcpkg CMake "
    "package was found. Check that vcpkg installed 'projectm' for triplet "
    "${VCPKG_TARGET_TRIPLET}, and that VCPKG_OVERLAY_TRIPLETS points at triplets/.")
endif()

set(MILKDAWP_PROJECTM_TARGET "")
foreach(_mdw_candidate IN ITEMS libprojectM::projectM projectM::projectM projectm::projectm projectM projectm)
  if(TARGET ${_mdw_candidate})
    set(MILKDAWP_PROJECTM_TARGET ${_mdw_candidate})
    break()
  endif()
endforeach()
unset(_mdw_candidate)

if(NOT MILKDAWP_PROJECTM_TARGET)
  message(FATAL_ERROR
    "libprojectM was found but exposes none of the expected CMake targets "
    "(libprojectM::projectM, projectM::projectM, projectm::projectm, projectM, projectm).")
endif()

get_target_property(_mdw_projectm_type ${MILKDAWP_PROJECTM_TARGET} TYPE)
if(NOT _mdw_projectm_type STREQUAL "SHARED_LIBRARY" AND NOT _mdw_projectm_type STREQUAL "UNKNOWN_LIBRARY")
  message(WARNING
    "libprojectM target '${MILKDAWP_PROJECTM_TARGET}' is ${_mdw_projectm_type}, not "
    "SHARED_LIBRARY. Confirm VCPKG_TARGET_TRIPLET is one of the *-dynamic triplets in triplets/.")
endif()
unset(_mdw_projectm_type)

message(STATUS "MilkDAWp: using projectM target ${MILKDAWP_PROJECTM_TARGET}")

# milkdawp_deploy_projectm_runtime(<target>)
#
# ProjectMLibrary (engine/) deliberately never links MILKDAWP_PROJECTM_TARGET
# at build time -- it loads projectM by name at runtime with
# juce::DynamicLibrary (see that class's header comment), specifically so a
# binary built with projectM present still loads cleanly on a machine
# without it. The cost of that design: vcpkg's automatic runtime-dependency
# copy (VCPKG_APPLOCAL_DEPS) only copies DLLs for things a target actually
# links against, so projectM's own shared library is never placed next to
# the plugin/app binary on its own -- and neither is anything *projectM
# itself* dynamically links against (confirmed via `dumpbin /dependents`:
# projectM-4d.dll pulls in glew32d.dll, which is equally undeployed, since
# GLEW is projectM's dependency, not ours). Rather than hand-track projectM's
# transitive DLLs one at a time as they change across versions/platforms,
# this copies the whole vcpkg-installed bin directory for the active config,
# then deletes the specific files we know we don't want: zlib/libpng are
# vcpkg dependencies of *other* things in this manifest, not of projectM or
# of us (§4.11 -- we use JUCE's own bundled zlib/libpng, not vcpkg's), and
# leaving them here is more than just clutter: real, separate DLLs sitting
# next to the plugin binary are exactly what broke loading in REAPER/Cubase
# the one time this project did eagerly link vcpkg's copies (see §4.11's
# 2026-09-26 update) -- not worth re-introducing the same risk for files
# nothing here actually uses. Call this on every final linked binary that
# constructs a RenderEngine (plugin, app, mdw-view) so ProjectMLibrary's
# bundle-relative/module-directory search (§2.1) has everything it needs.
function(milkdawp_deploy_projectm_runtime target)
  set(_mdw_vcpkg_bin "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${_mdw_vcpkg_bin}"
      "$<TARGET_FILE_DIR:${target}>"
    COMMAND ${CMAKE_COMMAND} -E rm -f
      "$<TARGET_FILE_DIR:${target}>/z$<$<CONFIG:Debug>:d>.dll"
      "$<TARGET_FILE_DIR:${target}>/libpng16$<$<CONFIG:Debug>:d>.dll"
    VERBATIM
    COMMENT "Deploying projectM's vcpkg runtime directory next to ${target} (minus unused zlib/libpng)"
  )
  unset(_mdw_vcpkg_bin)
endfunction()
