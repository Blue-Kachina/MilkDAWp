# Custom triplet for MilkDAWp: ARM64 macOS (Apple Silicon) with dynamic linking
# Forces all dependencies to build as shared libraries (.dylib)
# Required for LGPL compliance with projectM

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
