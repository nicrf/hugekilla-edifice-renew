# vcpkg triplet for the Windows release (build.py --gns --config Release): DLLs
# and the shared C++ runtime like x64-windows, release only.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
