# vcpkg triplet for the macOS release (build.py --gns --universal): static
# libraries for both Intel and Apple Silicon, release only, same minimum macOS as
# the game (CMAKE_OSX_DEPLOYMENT_TARGET in build.py).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES "arm64;x86_64")
set(VCPKG_OSX_DEPLOYMENT_TARGET 10.15)
set(VCPKG_BUILD_TYPE release)
