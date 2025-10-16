# Copy a list of candidate DLL names OR glob patterns from a source directory to a destination directory if they exist.
# Usage:
#   cmake -D SRC_DIR=... -D DEST_DIR=... -D NAMES="name1.dll;name2.dll;..." -P copy_runtime_deps.cmake
# or
#   cmake -D SRC_DIR=... -D DEST_DIR=... -D PATTERNS="libpng16*.dll;zlib*.dll;..." -P copy_runtime_deps.cmake

if(NOT DEFINED SRC_DIR OR NOT DEFINED DEST_DIR)
  message(FATAL_ERROR "copy_runtime_deps.cmake requires SRC_DIR and DEST_DIR variables")
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")

# First, copy explicit names if provided
if(DEFINED NAMES)
  foreach(_n IN LISTS NAMES)
    set(_src "${SRC_DIR}/${_n}")
    if(EXISTS "${_src}")
      file(COPY "${_src}" DESTINATION "${DEST_DIR}")
      message(STATUS "Copied runtime dep: ${_src} -> ${DEST_DIR}")
    endif()
  endforeach()
endif()

# Then, copy any files that match provided glob patterns
if(DEFINED PATTERNS)
  foreach(_p IN LISTS PATTERNS)
    file(GLOB _matches "${SRC_DIR}/${_p}")
    foreach(_m IN LISTS _matches)
      if(EXISTS "${_m}")
        file(COPY "${_m}" DESTINATION "${DEST_DIR}")
        message(STATUS "Copied runtime dep (glob): ${_m} -> ${DEST_DIR}")
      endif()
    endforeach()
  endforeach()
endif()
