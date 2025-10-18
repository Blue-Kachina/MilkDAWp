# Custom triplet for MilkDAWp: x64 Windows with dynamic linking
# Forces all dependencies to build as shared libraries (DLLs)
# Required for LGPL compliance with projectM and MSVC dynamic runtime (/MD)

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Use the default Windows toolchain
set(VCPKG_CMAKE_SYSTEM_NAME "")

# Workaround for ports with legacy CMakeLists that fail with CMake 4.x (e.g., bzip2)
# Instruct CMake to allow policies compatible with at least 3.5 during configure.
# This flag is passed through vcpkg_cmake_configure to all port configure invocations.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
