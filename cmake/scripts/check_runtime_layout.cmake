# cmake/scripts/check_runtime_layout.cmake
#
# Run via `cmake -P` as a POST_BUILD step -- see
# milkdawp_check_runtime_layout() in ../ProjectMDependency.cmake.
# Fails the build unless the binary's directory holds exactly the binary, its
# own build byproducts (.pdb, .ilk, ...), and projectM's library. Any other
# file would ship with the plugin, and a stray zlib/libpng DLL there is what
# made REAPER and Cubase reject the plugin (§4.11, 2026-09-26).
#
# Inputs:
#   MILKDAWP_CHECK_TARGET_FILE  $<TARGET_FILE:...> of the shipped binary
#   MILKDAWP_PROJECTM_FILE      $<TARGET_FILE:...> of projectM, or empty when
#                               the build has no projectM

if(NOT DEFINED MILKDAWP_CHECK_TARGET_FILE)
  message(FATAL_ERROR "check_runtime_layout.cmake: MILKDAWP_CHECK_TARGET_FILE not set")
endif()

get_filename_component(_mdw_dir "${MILKDAWP_CHECK_TARGET_FILE}" DIRECTORY)
get_filename_component(_mdw_binary "${MILKDAWP_CHECK_TARGET_FILE}" NAME)
get_filename_component(_mdw_stem "${MILKDAWP_CHECK_TARGET_FILE}" NAME_WLE)

set(_mdw_projectm_name "")
if(MILKDAWP_PROJECTM_FILE)
  include("${CMAKE_CURRENT_LIST_DIR}/projectm_runtime_name.cmake")
  milkdawp_projectm_runtime_name("${MILKDAWP_PROJECTM_FILE}" _mdw_projectm_name)
  if(NOT EXISTS "${_mdw_dir}/${_mdw_projectm_name}")
    message(FATAL_ERROR "${_mdw_binary}: ${_mdw_projectm_name} is missing from ${_mdw_dir}; "
                        "the projectM deploy step should have put it there")
  endif()
endif()

file(GLOB _mdw_files LIST_DIRECTORIES true RELATIVE "${_mdw_dir}" "${_mdw_dir}/*")
set(_mdw_unexpected "")
foreach(_mdw_name IN LISTS _mdw_files)
  if(_mdw_name STREQUAL _mdw_binary OR _mdw_name STREQUAL _mdw_projectm_name)
    continue()
  endif()
  # The linker's byproducts share the binary's stem: "MilkDAWp.pdb", "MilkDAWp.ilk".
  string(FIND "${_mdw_name}" "${_mdw_stem}." _mdw_pos)
  if(_mdw_pos EQUAL 0 AND NOT IS_DIRECTORY "${_mdw_dir}/${_mdw_name}")
    continue()
  endif()
  list(APPEND _mdw_unexpected "${_mdw_name}")
endforeach()

if(_mdw_unexpected)
  string(REPLACE ";" "\n  " _mdw_list "${_mdw_unexpected}")
  message(FATAL_ERROR "${_mdw_binary}: unexpected files next to the binary in ${_mdw_dir}:\n  ${_mdw_list}\n"
                      "Only the binary, its build byproducts and projectM's library may ship there. "
                      "If these are left over from an older build, delete them.")
endif()

message(STATUS "${_mdw_binary}: runtime layout check passed")
