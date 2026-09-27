# cmake/ProjectMDependency.cmake
#
# Locates libprojectM (vcpkg-provided; see vcpkg.json) and validates it is a
# SHARED library, which projectM's LGPL-2.1 licence requires when dynamically
# linked into MilkDAWp's AGPL-3.0-or-later binary (D10). The triplets/*.cmake
# overlay triplets force this; this check catches a misconfigured triplet.
#
# Exposes MILKDAWP_PROJECTM_TARGET for consumers once milkdawp_engine exists.

# milkdawp_check_runtime_layout(<target>)
#
# Adds a POST_BUILD step that fails the build unless <target>'s directory
# holds only the binary, its build byproducts, and (with projectM) projectM's
# library (3.10). Call it on every shipped binary (the VST3, the Standalone,
# later the app) after milkdawp_deploy_projectm_runtime(), so it sees the
# deployed result. Not for dev tools or tests, whose output directories are
# shared with other targets.
function(milkdawp_check_runtime_layout target)
  set(_mdw_projectm_file "")
  if(MILKDAWP_WITH_PROJECTM)
    set(_mdw_projectm_file "$<TARGET_FILE:${MILKDAWP_PROJECTM_TARGET}>")
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DMILKDAWP_CHECK_TARGET_FILE=$<TARGET_FILE:${target}>"
      "-DMILKDAWP_PROJECTM_FILE=${_mdw_projectm_file}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/check_runtime_layout.cmake"
    VERBATIM
    COMMENT "Checking nothing unexpected ships next to ${target}"
  )
endfunction()

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
# itself* dynamically links against (4.1.7 pulled in glew32d.dll this way).
# The 4.2 overlay port (ADR-0008) needs nothing beyond system libraries and
# the C/C++ runtime (`dumpbin /dependents`, 2026-09-26), so this copies
# exactly one file: projectM's library, resolved per config from the imported
# target. It used to copy the whole vcpkg bin directory, which also shipped
# zlib/libpng (vcpkg dependencies of other things in the manifest; we use
# JUCE's bundled copies, §4.11), projectM's unused playlist library and .pdb
# files. Stray zlib/libpng DLLs next to the plugin are what broke loading in
# REAPER/Cubase (§4.11's 2026-09-26 update), so the deploy step also deletes
# any file from a vcpkg bin directory it finds left over there, and
# milkdawp_check_runtime_layout() fails the build if anything else turns up.
# If a projectM pin gains a real DLL dependency, the engine will fail to load
# projectM and the dependency has to be added here deliberately.
#
# Call this on every final linked binary that constructs a RenderEngine
# (plugin, app, mdw-view, engine tests) so ProjectMLibrary's
# bundle-relative/module-directory search (§2.1) finds the library.
function(milkdawp_deploy_projectm_runtime target)
  set(_mdw_triplet_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DMILKDAWP_PROJECTM_FILE=$<TARGET_FILE:${MILKDAWP_PROJECTM_TARGET}>"
      "-DMILKDAWP_DEST_DIR=$<TARGET_FILE_DIR:${target}>"
      "-DMILKDAWP_VCPKG_BIN_DIRS=${_mdw_triplet_dir}/bin$<SEMICOLON>${_mdw_triplet_dir}/debug/bin"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/deploy_projectm_runtime.cmake"
    VERBATIM
    COMMENT "Deploying projectM's library next to ${target}"
  )
endfunction()
