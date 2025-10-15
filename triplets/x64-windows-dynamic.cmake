# Custom triplet for MilkDAWp: x64 Windows with dynamic linking
# Forces all dependencies to build as shared libraries (DLLs)
# Required for LGPL compliance with projectM and MSVC dynamic runtime (/MD)

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Use the default Windows toolchain
set(VCPKG_CMAKE_SYSTEM_NAME "")
