# cmake/scripts/deploy_projectm_runtime.cmake
#
# Run via `cmake -P` as a POST_BUILD step -- see
# milkdawp_deploy_projectm_runtime() in ../ProjectMDependency.cmake.
# Copies projectM's shared library into MILKDAWP_DEST_DIR under the name
# ProjectMLibrary looks for, then deletes anything else a vcpkg bin directory
# put there (zlib/libpng, projectM's playlist library, .pdb files), including
# leftovers from the old copy-the-whole-directory deploy.
#
# Inputs:
#   MILKDAWP_PROJECTM_FILE  $<TARGET_FILE:...> of the projectM imported target
#   MILKDAWP_DEST_DIR       directory of the binary that loads projectM
#   MILKDAWP_VCPKG_BIN_DIRS ";"-separated vcpkg bin dirs (release and debug)

foreach(_mdw_var MILKDAWP_PROJECTM_FILE MILKDAWP_DEST_DIR)
  if(NOT DEFINED ${_mdw_var})
    message(FATAL_ERROR "deploy_projectm_runtime.cmake: ${_mdw_var} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/projectm_runtime_name.cmake")
milkdawp_projectm_runtime_name("${MILKDAWP_PROJECTM_FILE}" _mdw_dest_name)

# Follow the symlink chain so the real file is copied, not a dangling link.
file(REAL_PATH "${MILKDAWP_PROJECTM_FILE}" _mdw_src_real)
file(COPY_FILE "${_mdw_src_real}" "${MILKDAWP_DEST_DIR}/${_mdw_dest_name}" ONLY_IF_DIFFERENT)

# Remove every other file that exists in a vcpkg bin dir. Only names vcpkg
# could have put here are touched; our own build outputs never match.
set(_mdw_removed "")
foreach(_mdw_bin_dir IN LISTS MILKDAWP_VCPKG_BIN_DIRS)
  file(GLOB _mdw_vcpkg_files LIST_DIRECTORIES false "${_mdw_bin_dir}/*")
  foreach(_mdw_f IN LISTS _mdw_vcpkg_files)
    get_filename_component(_mdw_name "${_mdw_f}" NAME)
    if(NOT _mdw_name STREQUAL _mdw_dest_name AND EXISTS "${MILKDAWP_DEST_DIR}/${_mdw_name}")
      file(REMOVE "${MILKDAWP_DEST_DIR}/${_mdw_name}")
      list(APPEND _mdw_removed "${_mdw_name}")
    endif()
  endforeach()
endforeach()

if(_mdw_removed)
  list(REMOVE_DUPLICATES _mdw_removed)
  message(STATUS "Removed stale vcpkg files from ${MILKDAWP_DEST_DIR}: ${_mdw_removed}")
endif()
