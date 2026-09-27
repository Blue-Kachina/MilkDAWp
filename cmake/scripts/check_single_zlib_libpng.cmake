# cmake/scripts/check_single_zlib_libpng.cmake
#
# Run via `cmake -P` as a POST_BUILD step -- see
# milkdawp_check_single_zlib_libpng() in ../SingleZlibLibpngCheck.cmake.
# Lists the shared libraries a binary depends on and fails if it finds more
# than one zlib or more than one libpng. Best-effort: unresolvable tooling
# warns and skips rather than failing the build.

if(NOT DEFINED MILKDAWP_CHECK_TARGET_FILE)
  message(FATAL_ERROR "check_single_zlib_libpng.cmake: MILKDAWP_CHECK_TARGET_FILE not set")
endif()

if(MILKDAWP_CHECK_PLATFORM STREQUAL "Windows")
  find_program(_mdw_dep_tool dumpbin)
  set(_mdw_dep_args /dependents "${MILKDAWP_CHECK_TARGET_FILE}")
elseif(MILKDAWP_CHECK_PLATFORM STREQUAL "Darwin")
  find_program(_mdw_dep_tool otool)
  set(_mdw_dep_args -L "${MILKDAWP_CHECK_TARGET_FILE}")
else()
  find_program(_mdw_dep_tool ldd)
  set(_mdw_dep_args "${MILKDAWP_CHECK_TARGET_FILE}")
endif()

if(NOT _mdw_dep_tool)
  message(WARNING "No dependency-listing tool found for ${MILKDAWP_CHECK_PLATFORM}; "
                   "skipping single-zlib/libpng check for ${MILKDAWP_CHECK_TARGET_FILE}")
  return()
endif()

execute_process(
  COMMAND "${_mdw_dep_tool}" ${_mdw_dep_args}
  OUTPUT_VARIABLE _mdw_deps
  RESULT_VARIABLE _mdw_result
)

if(NOT _mdw_result EQUAL 0)
  message(WARNING "Could not list dependencies of ${MILKDAWP_CHECK_TARGET_FILE}; "
                   "skipping single-zlib/libpng check")
  return()
endif()

# ldd prints "libz.so.1 => /lib/x86_64-linux-gnu/libz.so.1 (0x...)": keep only
# the resolved path, or the one copy counts twice (soname + path).
if(NOT MILKDAWP_CHECK_PLATFORM STREQUAL "Windows" AND NOT MILKDAWP_CHECK_PLATFORM STREQUAL "Darwin")
  string(REGEX REPLACE "[^ \t\r\n]+ => " "" _mdw_deps "${_mdw_deps}")
endif()

string(REGEX MATCHALL "[^ \t\r\n]*lib(z|png1?6?)d?\\.(dll|dylib|so[.0-9]*)" _mdw_matches "${_mdw_deps}")
string(REGEX MATCHALL "[^ \t\r\n]*z(lib)?1?d?\\.dll" _mdw_zlib_win_matches "${_mdw_deps}")
list(APPEND _mdw_matches ${_mdw_zlib_win_matches})

set(_mdw_zlib_matches "")
set(_mdw_libpng_matches "")
foreach(_mdw_m IN LISTS _mdw_matches)
  string(TOLOWER "${_mdw_m}" _mdw_m_lower)
  if(_mdw_m_lower MATCHES "png")
    list(APPEND _mdw_libpng_matches "${_mdw_m}")
  else()
    list(APPEND _mdw_zlib_matches "${_mdw_m}")
  endif()
endforeach()

list(REMOVE_DUPLICATES _mdw_zlib_matches)
list(REMOVE_DUPLICATES _mdw_libpng_matches)
list(LENGTH _mdw_zlib_matches _mdw_zlib_count)
list(LENGTH _mdw_libpng_matches _mdw_libpng_count)

if(_mdw_zlib_count GREATER 1)
  message(FATAL_ERROR "${MILKDAWP_CHECK_TARGET_FILE} links ${_mdw_zlib_count} distinct zlib copies: ${_mdw_zlib_matches}")
endif()
if(_mdw_libpng_count GREATER 1)
  message(FATAL_ERROR "${MILKDAWP_CHECK_TARGET_FILE} links ${_mdw_libpng_count} distinct libpng copies: ${_mdw_libpng_matches}")
endif()

message(STATUS "${MILKDAWP_CHECK_TARGET_FILE}: single-zlib/libpng check passed (zlib=${_mdw_zlib_count}, libpng=${_mdw_libpng_count})")
