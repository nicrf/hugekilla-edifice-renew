cmake_minimum_required(VERSION 3.16)
project(Edifice CXX)

# macOS / POSIX build of HugeKilla (Edifice) on SDL2 + legacy OpenGL.
# Windows keeps using hugekilla.sln.

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

set(SOURCES
  ase.cpp
  cfgfile.cpp
  console.cpp
  GamePlay.cpp
  hugekilla.cpp
  land.cpp
  multiplay.cpp
  playing.cpp
  redrawing.cpp
  sock.cpp
  structures.cpp
  textures.cpp
  ui.cpp
  options.cpp
  Vector.cpp
  platform_sdl.cpp
  sound_sdl.cpp
  compat/win32compat.cpp
)

set(DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/HugeKilla)

if(APPLE)
  # Ship the game data inside Edifice.app/Contents/Resources/HugeKilla
  file(GLOB_RECURSE DATA_FILES RELATIVE ${DATA_DIR} CONFIGURE_DEPENDS ${DATA_DIR}/*)
  foreach(f ${DATA_FILES})
    get_filename_component(dir ${f} DIRECTORY)
    set_source_files_properties(${DATA_DIR}/${f} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/HugeKilla/${dir})
    list(APPEND BUNDLE_DATA ${DATA_DIR}/${f})
  endforeach()
  add_executable(Edifice MACOSX_BUNDLE ${SOURCES} ${BUNDLE_DATA})
  set_target_properties(Edifice PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME Edifice
    MACOSX_BUNDLE_GUI_IDENTIFIER net.sourceforge.hugekilla.edifice)
else()
  add_executable(Edifice ${SOURCES})
endif()

target_include_directories(Edifice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
target_compile_definitions(Edifice PRIVATE GL_SILENCE_DEPRECATION HK_SOURCE_DATA_DIR="${DATA_DIR}")
# 2000-era code: keep the noise down, and keep its signed char / wrapping assumptions.
target_compile_options(Edifice PRIVATE -fsigned-char -fwrapv -fno-strict-aliasing
  -Wno-write-strings -Wno-deprecated-declarations -Wno-dangling-else -Wno-parentheses
  -Wno-nonportable-include-path -Wno-c++11-narrowing -Wno-return-type -Wno-format-security
  -Wno-pragma-once-outside-header -Wno-address-of-temporary)
target_link_libraries(Edifice PRIVATE SDL2::SDL2 OpenGL::GL)
if(APPLE)
  target_link_libraries(Edifice PRIVATE "-framework OpenGL")
endif()
