# vcpkg triplet for self-contained macOS builds of Integra Live: static libraries,
# universal (Apple Silicon + Intel), macOS 11 or later.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES "arm64;x86_64")
set(VCPKG_OSX_DEPLOYMENT_TARGET "11.0")
set(VCPKG_BUILD_TYPE release)   # only release libraries are linked
