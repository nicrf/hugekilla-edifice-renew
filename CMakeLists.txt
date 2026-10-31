cmake_minimum_required(VERSION 3.16)
project(Edifice VERSION 0.3.0 LANGUAGES C CXX)

# Edifice (HugeKilla) - one build, one code path for every platform:
#   SDL2 (window, input, audio) + legacy OpenGL on macOS, Linux and Windows.
# SDL2 is downloaded and linked statically, so the game has no runtime
# dependency besides the OS (set EDIFICE_SYSTEM_SDL2=ON to use an installed SDL2).
# The original Win32 code (WinMain, DirectSound, dialogs) is still in the sources
# behind HK_WIN32_NATIVE for reference, but is no longer built.
# See BUILD.md, or simply run: python3 build.py

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_property(HK_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT HK_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(MSVC)
  # Static CRT, like the original Release build: no VC++ redistributable needed.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- SDL2 --------------------------------------------------------------------
option(EDIFICE_SYSTEM_SDL2 "Use an installed SDL2 instead of building it" OFF)
if(EDIFICE_SYSTEM_SDL2)
  find_package(SDL2 REQUIRED)
  set(HK_SDL_LIBS SDL2::SDL2)
else()
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
    URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165)
  FetchContent_MakeAvailable(SDL2)
  set(HK_SDL_LIBS SDL2::SDL2-static)
endif()
if(TARGET SDL2::SDL2main)
  list(PREPEND HK_SDL_LIBS SDL2::SDL2main)
endif()

find_package(OpenGL REQUIRED)

# ---- Sources -----------------------------------------------------------------
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)

set(SOURCES
  src/render/ase.cpp
  src/render/redrawing.cpp
  src/render/textures.cpp
  src/game/cfgfile.cpp
  src/game/GamePlay.cpp
  src/game/hugekilla.cpp
  src/game/land.cpp
  src/game/playing.cpp
  src/game/structures.cpp
  src/game/Vector.cpp
  src/net/multiplay.cpp
  src/net/sock.cpp
  src/ui/console.cpp
  src/ui/ui.cpp
  src/ui/options.cpp
  src/ui/serversetup.cpp
  src/ui/scoreboard.cpp
  src/ui/buildmenu.cpp
  src/ui/chat.cpp
  src/ui/joinscreen.cpp
  src/audio/sound_sdl.cpp
  src/platform/platform_sdl.cpp
  src/compat/win32compat.cpp
)

# The legacy sources include headers by bare name ("vector.h", "redrawing.h",
# <StdAfx.h>...), so every module directory is on the include path.
set(INCLUDE_DIRS
  ${SRC}/game ${SRC}/render ${SRC}/net ${SRC}/ui ${SRC}/audio ${SRC}/platform ${SRC}/compat
  ${CMAKE_CURRENT_SOURCE_DIR}/win32)   # resource.h (dialog/icon IDs used by the code)
if(NOT WIN32)
  # Stand-ins for <windows.h>, "gl/gl.h"... (on Windows the real headers are used)
  list(APPEND INCLUDE_DIRS ${SRC}/compat/include)
endif()

# ---- Target ------------------------------------------------------------------
if(APPLE)
  # Ship the game data inside Edifice.app/Contents/Resources/data
  file(GLOB_RECURSE DATA_FILES RELATIVE ${DATA_DIR} CONFIGURE_DEPENDS ${DATA_DIR}/*)
  list(FILTER DATA_FILES EXCLUDE REGEX "(^|/)\\.DS_Store$")
  foreach(f ${DATA_FILES})
    get_filename_component(dir ${f} DIRECTORY)
    set_source_files_properties(${DATA_DIR}/${f} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/data/${dir})
    list(APPEND BUNDLE_DATA ${DATA_DIR}/${f})
  endforeach()
  add_executable(Edifice MACOSX_BUNDLE ${SOURCES} ${BUNDLE_DATA})
  set_target_properties(Edifice PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME Edifice
    MACOSX_BUNDLE_GUI_IDENTIFIER net.sourceforge.hugekilla.edifice
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  # Bundle any non-system dylib (only with EDIFICE_SYSTEM_SDL2) and ad-hoc sign
  # the whole bundle so that `codesign -v --deep` passes.
  add_custom_command(TARGET Edifice POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DBUNDLE=$<TARGET_BUNDLE_DIR:Edifice>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_bundle_fixup.cmake
    COMMENT "Signing Edifice.app")
  install(TARGETS Edifice BUNDLE DESTINATION .)
else()
  if(WIN32)
    add_executable(Edifice WIN32 ${SOURCES} win32/hugekilla.rc)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Edifice)
    set_property(TARGET Edifice PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Edifice>)
  else()
    add_executable(Edifice ${SOURCES})
  endif()
  # data/ next to the executable (found through SDL_GetBasePath()); control.ini
  # and net.ini are only copied when missing (see cmake/copy_data.cmake).
  add_custom_command(TARGET Edifice POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DSRC=${DATA_DIR} -DDST=$<TARGET_FILE_DIR:Edifice>/data
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_data.cmake
    COMMENT "Copying game data next to Edifice")
  # Self-contained, writable layout <prefix>/Edifice + <prefix>/data, e.g.
  # cmake --install build/linux-release --prefix ~/Games/Edifice
  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND NOT WIN32)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Games/Edifice" CACHE PATH "Install prefix" FORCE)
  endif()
  install(TARGETS Edifice RUNTIME DESTINATION .)
  install(DIRECTORY ${DATA_DIR}/ DESTINATION data PATTERN .DS_Store EXCLUDE)
endif()

target_include_directories(Edifice PRIVATE ${INCLUDE_DIRS})
# Last-resort data lookup in the source tree: Debug builds only, so a moved
# Release binary never writes into the git-tracked data/control.ini.
target_compile_definitions(Edifice PRIVATE GL_SILENCE_DEPRECATION
  EDIFICE_VERSION_STRING="${PROJECT_VERSION}"
  "$<$<CONFIG:Debug>:HK_SOURCE_DATA_DIR=\"${DATA_DIR}\">")

# 2000-era code: keep the noise down, and keep its signed char / wrapping assumptions.
if(MSVC)
  # MSVC's default permissive mode is what the VC6-era code needs: never add /permissive-.
  target_compile_definitions(Edifice PRIVATE WIN32 _WINDOWS _MBCS _USE_MATH_DEFINES NOMINMAX
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  target_compile_options(Edifice PRIVATE /W1)
else()
  target_compile_options(Edifice PRIVATE -fsigned-char -fwrapv -fno-strict-aliasing
    -Wno-write-strings -Wno-deprecated-declarations -Wno-parentheses
    -Wno-return-type -Wno-format-security)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(Edifice PRIVATE -Wno-dangling-else -Wno-nonportable-include-path
      -Wno-c++11-narrowing -Wno-pragma-once-outside-header -Wno-address-of-temporary
      -Wno-implicit-const-int-float-conversion)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC turns some VC6-isms into errors that clang only warns about.
    target_compile_options(Edifice PRIVATE -fpermissive -Wno-narrowing -w)
  endif()
  if(WIN32)   # MinGW
    target_compile_definitions(Edifice PRIVATE _USE_MATH_DEFINES)
    target_link_options(Edifice PRIVATE -static)
  endif()
endif()

target_link_libraries(Edifice PRIVATE ${HK_SDL_LIBS} OpenGL::GL)
if(APPLE)
  target_link_libraries(Edifice PRIVATE "-framework OpenGL")
elseif(WIN32)
  target_link_libraries(Edifice PRIVATE glu32 ws2_32 winmm)
else()
  if(TARGET OpenGL::GLU)
    target_link_libraries(Edifice PRIVATE OpenGL::GLU)
  else()
    message(FATAL_ERROR "GLU not found (Debian/Ubuntu: sudo apt install libglu1-mesa-dev)")
  endif()
endif()

# ---- Tests -------------------------------------------------------------------
# Unit tests that need neither a window nor the game data (run with ctest).
enable_testing()
add_executable(worldrandom_test tests/worldrandom_test.cpp)
target_include_directories(worldrandom_test PRIVATE ${SRC}/game)
add_test(NAME worldrandom COMMAND worldrandom_test)

# ---- GameNetworkingSockets (experimental, roadmap M7) --------------------------
# Encrypted connections and P2P. Off by default: configure with the vcpkg
# toolchain and the "gns" feature of vcpkg.json, e.g.
#   cmake -B build/gns -DEDIFICE_GNS=ON -DVCPKG_MANIFEST_FEATURES=gns \
#     -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
option(EDIFICE_GNS "Build with GameNetworkingSockets (needs vcpkg)" OFF)
if(EDIFICE_GNS)
  find_package(GameNetworkingSockets CONFIG REQUIRED)
  if(TARGET GameNetworkingSockets::static)
    set(HK_GNS_LIB GameNetworkingSockets::static)
  else()
    set(HK_GNS_LIB GameNetworkingSockets::shared)
  endif()
  add_executable(gns_loopback_test tests/gns_loopback_test.cpp)
  target_link_libraries(gns_loopback_test PRIVATE ${HK_GNS_LIB})
  if(MSVC)
    # On Windows GameNetworkingSockets comes as a DLL (vcpkg x64-windows, like
    # Valve ships it): link the shared C++ runtime here. The static vcpkg
    # triplet builds it importing protobuf from a DLL that isn't there.
    set_property(TARGET gns_loopback_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
  add_test(NAME gns_loopback COMMAND gns_loopback_test)
endif()
