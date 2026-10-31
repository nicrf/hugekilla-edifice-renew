cmake_minimum_required(VERSION 3.16)
project(Edifice VERSION 0.2.0 LANGUAGES CXX)

# Edifice (HugeKilla) - one build for all platforms:
#   macOS / Linux : SDL2 + legacy OpenGL, Win32 API emulated by src/compat
#   Windows (MSVC): original native Win32 path (WinMain, DirectSound, .rc)
# win32/hugekilla.sln / .vcproj are kept for reference only (legacy, not maintained).
# See BUILD.md, or simply run: python3 build.py

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_property(HK_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT HK_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)

# Code shared by every platform.
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
)

# The legacy sources include headers by bare name ("vector.h", "redrawing.h",
# <StdAfx.h>...), so every module directory is on the include path.
set(INCLUDE_DIRS
  ${SRC}/game ${SRC}/render ${SRC}/net ${SRC}/ui ${SRC}/audio ${SRC}/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/win32)   # resource.h (dialog/icon IDs used by the code)

if(WIN32)
  # ---- Native Win32 path ---------------------------------------------------
  list(APPEND SOURCES
    src/audio/sound.cpp          # DirectSound
    win32/hugekilla.rc)          # icons + dialogs
  add_executable(Edifice WIN32 ${SOURCES})
  target_include_directories(Edifice PRIVATE ${INCLUDE_DIRS})
  target_compile_definitions(Edifice PRIVATE WIN32 _WINDOWS _MBCS
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  if(MSVC)
    # Static CRT like the original Release config. Default (natural) struct packing
    # in every config: the old .vcproj built Debug with /Zp1, which made Debug
    # netplay incompatible with Release and the SDL builds (docs/plans.json).
    # MSVC's default is the permissive mode the VC6-era code needs: never add /permissive-.
    set_property(TARGET Edifice PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(Edifice PRIVATE /W1)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Edifice)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # MinGW (MSYS2 or cross-compile): GCC rejects some VC6-isms
    # ("taking address of rvalue" in redrawing.cpp) unless -fpermissive.
    target_compile_options(Edifice PRIVATE -fpermissive -w)
    target_link_options(Edifice PRIVATE -static)
  endif()
  target_link_libraries(Edifice PRIVATE opengl32 glu32 ws2_32 dsound dxguid winmm comdlg32)
  # The Win32 build loads its data relative to the working directory: copy the
  # contents of data/ next to the executable and debug from there
  # (control.ini / net.ini only when missing, see cmake/copy_data.cmake).
  add_custom_command(TARGET Edifice POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DSRC=${DATA_DIR} -DDST=$<TARGET_FILE_DIR:Edifice>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_data.cmake
    COMMENT "Copying game data next to Edifice.exe")
  set_property(TARGET Edifice PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Edifice>)
  install(TARGETS Edifice RUNTIME DESTINATION .)
  install(DIRECTORY ${DATA_DIR}/ DESTINATION . PATTERN .DS_Store EXCLUDE)
else()
  # ---- SDL2 path (macOS, Linux, other POSIX) --------------------------------
  find_package(SDL2 QUIET)
  if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 not found. Install it:\n"
      "  macOS : brew install sdl2\n"
      "  Debian/Ubuntu : sudo apt install libsdl2-dev libglu1-mesa-dev\n"
      "  Fedora : sudo dnf install SDL2-devel mesa-libGLU-devel")
  endif()
  if(NOT TARGET SDL2::SDL2)   # old SDL2 config files only set variables
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}")
  endif()
  find_package(OpenGL REQUIRED)

  list(APPEND SOURCES
    src/platform/platform_sdl.cpp
    src/audio/sound_sdl.cpp
    src/ui/options.cpp
    src/compat/win32compat.cpp)
  list(APPEND INCLUDE_DIRS ${SRC}/compat ${SRC}/compat/include)

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
    # Copy Homebrew's SDL2 into Contents/Frameworks, then ad-hoc sign the whole
    # bundle so that `codesign -v --deep` passes and the .app runs on Macs
    # without Homebrew.
    add_custom_command(TARGET Edifice POST_BUILD
      COMMAND ${CMAKE_COMMAND} -DBUNDLE=$<TARGET_BUNDLE_DIR:Edifice>
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_bundle_fixup.cmake
      COMMENT "Bundling SDL2 and signing Edifice.app")
    install(TARGETS Edifice BUNDLE DESTINATION .)
  else()
    add_executable(Edifice ${SOURCES})
    # data/ next to the executable (found through SDL_GetBasePath()); control.ini
    # and net.ini are only copied when missing (see cmake/copy_data.cmake).
    add_custom_command(TARGET Edifice POST_BUILD
      COMMAND ${CMAKE_COMMAND} -DSRC=${DATA_DIR} -DDST=$<TARGET_FILE_DIR:Edifice>/data
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_data.cmake
      COMMENT "Copying game data next to Edifice")
    # Self-contained, writable layout <prefix>/Edifice + <prefix>/data: install
    # into a game folder, e.g. cmake --install build/linux-release --prefix ~/Games/Edifice
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
      set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Games/Edifice" CACHE PATH "Install prefix" FORCE)
    endif()
    install(TARGETS Edifice RUNTIME DESTINATION .)
    install(DIRECTORY ${DATA_DIR}/ DESTINATION data PATTERN .DS_Store EXCLUDE)
  endif()

  target_include_directories(Edifice PRIVATE ${INCLUDE_DIRS})
  # Last-resort data lookup in the source tree: Debug builds only, so a moved
  # Release binary never writes into the git-tracked data/control.ini.
  target_compile_definitions(Edifice PRIVATE GL_SILENCE_DEPRECATION
    "$<$<CONFIG:Debug>:HK_SOURCE_DATA_DIR=\"${DATA_DIR}\">")
  # 2000-era code: keep the noise down, and keep its signed char / wrapping assumptions.
  target_compile_options(Edifice PRIVATE -fsigned-char -fwrapv -fno-strict-aliasing
    -Wno-write-strings -Wno-deprecated-declarations -Wno-parentheses
    -Wno-return-type -Wno-format-security)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(Edifice PRIVATE -Wno-dangling-else -Wno-nonportable-include-path
      -Wno-c++11-narrowing -Wno-pragma-once-outside-header -Wno-address-of-temporary)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC turns some VC6-isms into errors that clang only warns about.
    target_compile_options(Edifice PRIVATE -fpermissive -Wno-narrowing -w)
  endif()
  target_link_libraries(Edifice PRIVATE SDL2::SDL2 OpenGL::GL)
  if(APPLE)
    target_link_libraries(Edifice PRIVATE "-framework OpenGL")
  else()
    if(TARGET OpenGL::GLU)
      target_link_libraries(Edifice PRIVATE OpenGL::GLU)
    else()
      message(FATAL_ERROR "GLU not found (Debian/Ubuntu: sudo apt install libglu1-mesa-dev)")
    endif()
  endif()
endif()
