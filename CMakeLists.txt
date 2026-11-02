cmake_minimum_required(VERSION 3.16)
project(Edifice VERSION 0.6.2 LANGUAGES C CXX)

# Edifice (HugeKilla) - one build, one code path for every platform:
#   SDL2 (window, input, audio) + sokol_gfx (Metal on macOS, D3D11 on Windows,
#   GL 4.1 core on Linux; roadmap M13), or legacy OpenGL with HK_RENDER_SOKOL=OFF.
# SDL2 is downloaded and linked statically, so the game has no runtime
# dependency besides the OS (set EDIFICE_SYSTEM_SDL2=ON to use an installed SDL2).
# main() is SDL's on every platform (src/platform/platform_sdl.cpp); the original
# Win32 code (WinMain, DirectSound, dialogs) was removed in 0.5.4 (roadmap M12.3).
# See BUILD.md, or simply run: python3 build.py

# C++17 for std::filesystem (src/platform/fileio.cpp): macOS 10.15 or later
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_property(HK_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT HK_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(MSVC)
  # Static CRT, like the original Release build: no VC++ redistributable needed.
  # With EDIFICE_GNS (below) the shared one for everything, as its DLL needs.
  if(EDIFICE_GNS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

# ---- Web (Emscripten, roadmap M14) ---------------------------------------------
# The CMake preset "web" (emcmake, or python3 build.py --web): the game as a page,
# solo and offline for now, with no threads (GitHub Pages can't send the COOP/COEP
# headers SharedArrayBuffer needs), so neither libcurl nor GameNetworkingSockets.
if(EMSCRIPTEN AND EDIFICE_GNS)
  message(FATAL_ERROR "EDIFICE_GNS: GameNetworkingSockets has no web build (M14 uses WebRTC instead)")
endif()

# ---- SDL2 --------------------------------------------------------------------
option(EDIFICE_SYSTEM_SDL2 "Use an installed SDL2 instead of building it" OFF)
if(EMSCRIPTEN)
  # Emscripten's own SDL2 port (WebGL, Web Audio, browser input), not the release
  # tarball: SDL's Emscripten backend is built and kept up to date there
  add_library(hk_sdl2_web INTERFACE)
  target_compile_options(hk_sdl2_web INTERFACE -sUSE_SDL=2)
  target_link_options(hk_sdl2_web INTERFACE -sUSE_SDL=2)
  set(HK_SDL_LIBS hk_sdl2_web)
elseif(EDIFICE_SYSTEM_SDL2)
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

# ---- Opus (voice chat codec, BSD license) -----------------------------------
# Built statically from the official xiph release, like SDL2.
include(FetchContent)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
if(EDIFICE_GNS)
  set(OPUS_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
else()
  set(OPUS_STATIC_RUNTIME ON CACHE BOOL "" FORCE)   # MSVC: same static CRT as the game
endif()
list(LENGTH CMAKE_OSX_ARCHITECTURES HK_NUM_OSX_ARCHS)
if(HK_NUM_OSX_ARCHS GREATER 1 OR EMSCRIPTEN)
  # universal macOS build: opus picks SSE or NEON sources for the host CPU only;
  # WebAssembly: none of them
  set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(opus
  URL https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz
  URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1)
FetchContent_MakeAvailable(opus)

# ---- libcurl (Internet game list: edifice-lobby over HTTPS, roadmap M7c) ------
# The system's libcurl on macOS and Linux (Debian/Ubuntu: libcurl4-openssl-dev).
# On Windows (MSVC), where there is none, curl is built statically from source with
# the system TLS (Schannel), like SDL2 and Opus. Without libcurl the game builds
# anyway, and the join screen's Internet tab says it isn't available.
option(EDIFICE_LOBBY "Internet game list (needs libcurl)" ON)
set(HK_CURL_LIB)
if(EDIFICE_LOBBY AND NOT EMSCRIPTEN)   # the web build fetches through the browser (lobby.cpp)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    set(HK_CURL_LIB CURL::libcurl)
  elseif(MSVC)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
    if(EDIFICE_GNS)
      set(CURL_STATIC_CRT OFF CACHE BOOL "" FORCE)     # same shared CRT as the game
    else()
      set(CURL_STATIC_CRT ON CACHE BOOL "" FORCE)      # same static CRT as the game
    endif()
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE STRING "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(PICKY_COMPILER OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(curl
      URL https://github.com/curl/curl/releases/download/curl-8_10_1/curl-8.10.1.tar.xz
      URL_HASH SHA256=73a4b0e99596a09fa5924a4fb7e4b995a85fda0d18a2c02ab9cf134bebce04ee)
    FetchContent_MakeAvailable(curl)
    set(HK_CURL_LIB CURL::libcurl)
  endif()
  if(HK_CURL_LIB)
    message(STATUS "Internet game list: libcurl")
  else()
    message(STATUS "Internet game list: disabled (libcurl not found)")
  endif()
endif()

# ---- Renderer backend (roadmap M13) -------------------------------------------
# sokol_gfx (renderer_sokol.cpp, src/third_party/sokol, zlib license) on the
# platform's API: Metal on macOS, D3D11 on Windows, GL 4.1 core on Linux: the
# default since M13.4. HK_RENDER_SOKOL=OFF: the fixed-function OpenGL 2.1 backend
# (renderer_gl.cpp), kept for one release, for the machines without GL 4.1 or D3D11.
# HK_SOKOL_BACKEND=GLCORE also builds the Linux path on macOS (to test it there).
option(HK_RENDER_SOKOL "Draw with sokol_gfx (Metal, D3D11, GL 4.1 core) instead of fixed-function OpenGL" ON)
set(HK_SOKOL_BACKEND "" CACHE STRING "sokol_gfx backend: METAL, D3D11 or GLCORE (empty: the platform's)")
if(HK_RENDER_SOKOL)
  if(NOT HK_SOKOL_BACKEND)
    if(EMSCRIPTEN)
      set(HK_SOKOL_BACKEND GLES3)   # WebGL2
    elseif(APPLE)
      set(HK_SOKOL_BACKEND METAL)
    elseif(WIN32)
      set(HK_SOKOL_BACKEND D3D11)
    else()
      set(HK_SOKOL_BACKEND GLCORE)
    endif()
  endif()
  if(NOT HK_SOKOL_BACKEND MATCHES "^(METAL|D3D11|GLCORE|GLES3)$")
    message(FATAL_ERROR "HK_SOKOL_BACKEND must be METAL, D3D11, GLCORE or GLES3 (not ${HK_SOKOL_BACKEND})")
  endif()
  if(EMSCRIPTEN AND NOT HK_SOKOL_BACKEND STREQUAL "GLES3")
    message(FATAL_ERROR "the web build draws with WebGL2: HK_SOKOL_BACKEND=GLES3")
  endif()
  if(HK_SOKOL_BACKEND STREQUAL "GLCORE")
    find_package(OpenGL REQUIRED)
  endif()
  message(STATUS "Renderer: sokol_gfx ${HK_SOKOL_BACKEND}")

  # The shaders (src/render/shaders/*.glsl, sokol-shdc's annotated GLSL) become a C
  # header at build time with sokol-shdc, downloaded once for the host from the
  # sokol-tools-bin repository at a pinned commit and checked against its SHA-256
  # (HK_SOKOL_SHDC: an installed one instead, e.g. for an offline build).
  set(HK_SOKOL_SHDC "" CACHE FILEPATH "sokol-shdc to use instead of downloading it")
  set(HK_SHDC ${HK_SOKOL_SHDC})
  if(NOT HK_SHDC)
    set(HK_SHDC_COMMIT b975d5642093d62e76a1e088bdae945d6d8b8ff2)   # 2026-10-11, matches sokol_gfx.h
    set(HK_SHDC_EXE sokol-shdc)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
      set(HK_SHDC_DIR win32)
      set(HK_SHDC_EXE sokol-shdc.exe)
      set(HK_SHDC_SHA256 43d42c2e477bcc5012420f8d9ff641fedc41d552d165397fff4f2e60ba0d2ebb)
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
      set(HK_SHDC_DIR osx_arm64)
      set(HK_SHDC_SHA256 2a807d62dffcc67a50a6ab51df57cde2fc8b23a3daf6207177d79e00fbd4086b)
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
      set(HK_SHDC_DIR osx)
      set(HK_SHDC_SHA256 67e5c841a4ca58488e6592560dd8795c7b232a8805d2350ca346028541716252)
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
      set(HK_SHDC_DIR linux_arm64)
      set(HK_SHDC_SHA256 c31155b7d340cd561b6fac8d8580c2377abd9140b25d7403c085cb1e700cea14)
    else()
      set(HK_SHDC_DIR linux)
      set(HK_SHDC_SHA256 f227db5b282dcaa8085796ba83cda217d3114cf34913ca6ba4b0103f25ccc779)
    endif()
    set(HK_SHDC ${CMAKE_CURRENT_BINARY_DIR}/sokol-shdc/${HK_SHDC_EXE})
    if(NOT EXISTS ${HK_SHDC})
      set(dl ${CMAKE_CURRENT_BINARY_DIR}/sokol-shdc-download/${HK_SHDC_EXE})
      file(DOWNLOAD https://raw.githubusercontent.com/floooh/sokol-tools-bin/${HK_SHDC_COMMIT}/bin/${HK_SHDC_DIR}/${HK_SHDC_EXE}
        ${dl} EXPECTED_HASH SHA256=${HK_SHDC_SHA256} STATUS dlstatus)
      list(GET dlstatus 0 dlcode)
      if(NOT dlcode EQUAL 0)
        message(FATAL_ERROR "sokol-shdc download failed: ${dlstatus} (set HK_SOKOL_SHDC to a local copy)")
      endif()
      # a download isn't executable
      file(COPY ${dl} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/sokol-shdc
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
      file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/sokol-shdc-download)
    endif()
  endif()
  set(HK_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  if(MSVC)
    set(HK_SHDC_ERRFMT msvc)   # errors that Visual Studio links to the line
  else()
    set(HK_SHDC_ERRFMT gcc)
  endif()
  # Metal and HLSL as source (compiled when the game starts: no Xcode or Windows tools
  # needed here), GLSL 4.10 and the web's GLSL ES 3.00 and WGSL (M14) too
  add_custom_command(OUTPUT ${HK_SHADER_DIR}/edifice.glsl.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${HK_SHADER_DIR}
    COMMAND ${HK_SHDC} --input ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/edifice.glsl
            --output ${HK_SHADER_DIR}/edifice.glsl.h --slang glsl410:glsl300es:hlsl5:metal_macos:wgsl
            --ifdef --no-log-cmdline --errfmt ${HK_SHDC_ERRFMT}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/edifice.glsl ${HK_SHDC}
    COMMENT "sokol-shdc: src/render/shaders/edifice.glsl"
    VERBATIM)
  # the Ultra graphics (renderer_ultra.cpp): desktop APIs only, the web stays Classic
  add_custom_command(OUTPUT ${HK_SHADER_DIR}/ultra.glsl.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${HK_SHADER_DIR}
    COMMAND ${HK_SHDC} --input ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/ultra.glsl
            --output ${HK_SHADER_DIR}/ultra.glsl.h --slang glsl410:hlsl5:metal_macos
            --ifdef --no-log-cmdline --errfmt ${HK_SHDC_ERRFMT}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/ultra.glsl ${HK_SHDC}
    COMMENT "sokol-shdc: src/render/shaders/ultra.glsl"
    VERBATIM)
  set(HK_RENDER_SOURCES src/render/renderer_sokol.cpp src/render/texture_sokol.cpp
    src/render/sokol_native.cpp src/render/sokol_gfx_impl.cpp src/render/renderer_ultra.cpp
    ${HK_SHADER_DIR}/edifice.glsl.h ${HK_SHADER_DIR}/ultra.glsl.h)
elseif(EMSCRIPTEN)
  message(FATAL_ERROR "the web build draws with sokol_gfx (WebGL2): HK_RENDER_SOKOL=ON")
else()
  find_package(OpenGL REQUIRED)
  set(HK_RENDER_SOURCES src/render/renderer_gl.cpp src/render/texture_gl.cpp src/render/rcontext_gl.cpp)
endif()

# ---- Sources -----------------------------------------------------------------
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)

set(SOURCES
  src/render/ase.cpp
  src/render/redrawing.cpp
  src/render/effects.cpp
  src/render/textures.cpp
  src/render/renderer.cpp
  ${HK_RENDER_SOURCES}
  src/render/image.cpp
  src/game/cfgfile.cpp
  src/game/GamePlay.cpp
  src/game/hugekilla.cpp
  src/game/land.cpp
  src/game/mapfile.cpp
  src/game/playing.cpp
  src/game/structures.cpp
  src/game/Vector.cpp
  src/net/lobby.cpp
  src/net/lobbygame.cpp
  src/net/multiplay.cpp
  src/net/netlink.cpp
  src/net/signaling.cpp
  src/net/sock.cpp
  src/net/udplink.cpp
  src/net/version.cpp
  src/net/hmac.cpp
  src/ui/console.cpp
  src/ui/ui.cpp
  src/ui/options.cpp
  src/ui/serversetup.cpp
  src/ui/scoreboard.cpp
  src/ui/buildmenu.cpp
  src/ui/chat.cpp
  src/ui/joinscreen.cpp
  src/ui/spawnscreen.cpp
  src/audio/sound_sdl.cpp
  src/audio/voice.cpp
  src/platform/platform_sdl.cpp
  src/platform/gamepad.cpp
  src/platform/touch.cpp
  src/platform/fileio.cpp
  src/platform/system_sdl.cpp
)
if(EMSCRIPTEN)
  list(APPEND SOURCES src/net/webrtclink.cpp)   # joins native hosts over WebRTC (M14 task 3)
endif()

# The sources include headers by bare name ("vector.h", "redrawing.h"...), so every
# module directory is on the include path. Each file includes what it uses from
# src/platform: "hktypes.h" (DWORD, WORD), "hktime.h", "hkkeys.h", "hksocket.h",
# OpenGL through "gl.h", the window and the user folder through "platform.h".
set(INCLUDE_DIRS
  ${SRC}/game ${SRC}/render ${SRC}/net ${SRC}/ui ${SRC}/audio ${SRC}/platform
  ${SRC}/third_party)   # "stb/stb_image.h" (public domain / MIT, see its LICENSE)

# ---- Target ------------------------------------------------------------------
if(EMSCRIPTEN)
  # The site: index.html (src/platform/web/shell.html), index.js, index.wasm and
  # index.data, data/ preloaded as /data (read-only, in memory). The user folder
  # is /user, in IndexedDB (src/platform/web/pre.js).
  add_executable(Edifice ${SOURCES})
  set_target_properties(Edifice PROPERTIES OUTPUT_NAME index SUFFIX .html)
  set(HK_WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/web)
  target_link_options(Edifice PRIVATE
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=128MB   # the game has 80 MB of static arrays
    # the main thread's stack natively is 1 MB (Windows) to 8 MB: the land
    # generation keeps big arrays there
    -sSTACK_SIZE=8MB
    -sENVIRONMENT=web
    -sFETCH=1   # the lobby (src/net/lobby.cpp: emscripten_fetch, M14 task 2)
    -lidbfs.js
    "SHELL:--js-library ${HK_WEB_DIR}/webrtc.js"   # WebSocket relay and DataChannels (webrtclink.cpp)
    "SHELL:--pre-js ${HK_WEB_DIR}/pre.js"
    "SHELL:--shell-file ${HK_WEB_DIR}/shell.html"
    "SHELL:--preload-file ${DATA_DIR}@/data"
    "SHELL:--exclude-file *.DS_Store"
    "SHELL:--exclude-file ${DATA_DIR}/hd"   # the Ultra graphics' textures: desktop only
    $<$<CONFIG:Debug>:-sASSERTIONS=2>)
  file(GLOB_RECURSE DATA_FILES CONFIGURE_DEPENDS ${DATA_DIR}/*)
  set_property(TARGET Edifice APPEND PROPERTY LINK_DEPENDS
    ${HK_WEB_DIR}/pre.js ${HK_WEB_DIR}/shell.html ${HK_WEB_DIR}/webrtc.js ${DATA_FILES})
elseif(APPLE)
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
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    # CMake's default template plus NSMicrophoneUsageDescription (voice chat)
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in)
  # Bundle any non-system dylib (only with EDIFICE_SYSTEM_SDL2) and ad-hoc sign
  # the whole bundle so that `codesign -v --deep` passes.
  add_custom_command(TARGET Edifice POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DBUNDLE=$<TARGET_BUNDLE_DIR:Edifice>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_bundle_fixup.cmake
    COMMENT "Signing Edifice.app")
  install(TARGETS Edifice BUNDLE DESTINATION .)
else()
  if(WIN32)
    add_executable(Edifice WIN32 ${SOURCES} src/platform/windows/edifice.rc)   # the icon
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

# 2000-era code: keep the noise down, and its signed char assumption. No -fwrapv,
# -fno-strict-aliasing nor -Wno-return-type any more: the code they covered is fixed
# (roadmap M12.6), and the sanitizers check it (BUILD.md).
if(MSVC)
  # MSVC's default permissive mode is what the VC6-era code needs: never add /permissive-.
  target_compile_definitions(Edifice PRIVATE WIN32 _WINDOWS _MBCS _USE_MATH_DEFINES NOMINMAX
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  target_compile_options(Edifice PRIVATE /W1)
else()
  target_compile_options(Edifice PRIVATE -fsigned-char
    -Wno-write-strings -Wno-deprecated-declarations -Wno-parentheses
    -Wno-format-security)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(Edifice PRIVATE -Wno-dangling-else -Wno-nonportable-include-path
      -Wno-c++11-narrowing -Wno-pragma-once-outside-header
      -Wno-implicit-const-int-float-conversion)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC turns some VC6-isms into errors that clang only warns about.
    # snprintf cutting a long name or line is what the code wants (-Wformat-truncation).
    target_compile_options(Edifice PRIVATE -fpermissive -Wno-narrowing -Wno-format-truncation)
  endif()
  if(WIN32)   # MinGW
    target_compile_definitions(Edifice PRIVATE _USE_MATH_DEFINES)
    target_link_options(Edifice PRIVATE -static)
  endif()
endif()

target_link_libraries(Edifice PRIVATE ${HK_SDL_LIBS} Opus::opus)
if(HK_CURL_LIB)
  target_link_libraries(Edifice PRIVATE ${HK_CURL_LIB})
  target_compile_definitions(Edifice PRIVATE EDIFICE_LOBBY_CURL)
endif()
if(WIN32)
  target_link_libraries(Edifice PRIVATE ws2_32 winmm)
endif()
if(HK_RENDER_SOKOL)
  target_include_directories(Edifice PRIVATE ${SRC}/third_party/sokol ${HK_SHADER_DIR})
  target_compile_definitions(Edifice PRIVATE SOKOL_${HK_SOKOL_BACKEND} HK_RENDER_SOKOL)
  if(APPLE)
    # sokol_gfx's Metal backend is Objective-C; ARC, as its samples
    set_source_files_properties(src/render/sokol_native.cpp src/render/sokol_gfx_impl.cpp
      PROPERTIES COMPILE_OPTIONS "-xobjective-c++;-fobjc-arc")
    target_link_libraries(Edifice PRIVATE "-framework Foundation" "-framework QuartzCore")
  endif()
  # third-party code: its warnings stay warnings (EDIFICE_WERROR, below)
  set_property(SOURCE src/render/sokol_gfx_impl.cpp APPEND PROPERTY COMPILE_OPTIONS
    $<IF:$<CXX_COMPILER_ID:MSVC>,/WX-,-Wno-error>)
  if(HK_SOKOL_BACKEND STREQUAL "METAL")
    target_link_libraries(Edifice PRIVATE "-framework Metal")
  elseif(HK_SOKOL_BACKEND STREQUAL "D3D11")
    target_link_libraries(Edifice PRIVATE d3d11 dxgi)
  elseif(HK_SOKOL_BACKEND STREQUAL "GLES3")
    # WebGL2: linked by Emscripten (-sMAX_WEBGL_VERSION above)
  elseif(APPLE)
    target_link_libraries(Edifice PRIVATE "-framework OpenGL")
  else()
    target_link_libraries(Edifice PRIVATE OpenGL::GL)
  endif()
else()
  target_link_libraries(Edifice PRIVATE OpenGL::GL)
  if(APPLE)
    target_link_libraries(Edifice PRIVATE "-framework OpenGL")
  elseif(WIN32)
    target_link_libraries(Edifice PRIVATE glu32)
  else()
    if(TARGET OpenGL::GLU)
      target_link_libraries(Edifice PRIVATE OpenGL::GLU)
    else()
      message(FATAL_ERROR "GLU not found (Debian/Ubuntu: sudo apt install libglu1-mesa-dev)")
    endif()
  endif()
endif()

# ---- Tests -------------------------------------------------------------------
# Unit tests that need neither a window nor the game data (run with ctest).
enable_testing()
add_executable(worldrandom_test tests/worldrandom_test.cpp)
target_include_directories(worldrandom_test PRIVATE ${SRC}/game)
add_test(NAME worldrandom COMMAND worldrandom_test)
add_executable(wire_test tests/wire_test.cpp)
target_include_directories(wire_test PRIVATE ${SRC}/net)
add_test(NAME wire COMMAND wire_test)
add_executable(voice_test tests/voice_test.cpp)
target_include_directories(voice_test PRIVATE ${SRC}/net ${SRC}/audio)
target_link_libraries(voice_test PRIVATE Opus::opus)
add_test(NAME voice COMMAND voice_test)
# the server's anti-cheat checks (roadmap M11, src/net/anticheat.h)
add_executable(anticheat_test tests/anticheat_test.cpp)
target_include_directories(anticheat_test PRIVATE ${SRC}/net)
add_test(NAME anticheat COMMAND anticheat_test)
# SHA-256 / HMAC-SHA256 (RFC 4231) and the join cookie (roadmap M11 phase 2)
add_executable(hmac_test tests/hmac_test.cpp src/net/hmac.cpp)
target_include_directories(hmac_test PRIVATE ${SRC}/net)
add_test(NAME hmac COMMAND hmac_test)
# portable file access and the case-insensitive data lookup (roadmap M12.4)
add_executable(fileio_test tests/fileio_test.cpp src/platform/fileio.cpp)
target_include_directories(fileio_test PRIVATE ${SRC}/platform)
add_test(NAME fileio COMMAND fileio_test)
set(HK_TEST_INCLUDES ${SRC}/game ${SRC}/net ${SRC}/platform)   # "hktypes.h": DWORD...
add_executable(map_test tests/map_test.cpp src/game/mapfile.cpp)
target_include_directories(map_test PRIVATE ${HK_TEST_INCLUDES})
add_test(NAME map COMMAND map_test)
# with HK_LOBBY_URL set (a running server/), also talks to the lobby (tests/net_lobby.py)
add_executable(lobby_test tests/lobby_test.cpp src/net/lobby.cpp)
target_include_directories(lobby_test PRIVATE ${SRC}/net)
if(HK_CURL_LIB)
  target_link_libraries(lobby_test PRIVATE ${HK_CURL_LIB})
  target_compile_definitions(lobby_test PRIVATE EDIFICE_LOBBY_CURL)
endif()
add_test(NAME lobby COMMAND lobby_test)
# the signaling relay's lines and the checks on P2P signals (roadmap M7d)
add_executable(signaling_test tests/signaling_test.cpp src/net/signaling.cpp)
target_include_directories(signaling_test PRIVATE ${SRC}/net)
add_test(NAME signaling COMMAND signaling_test)
# the gamepad's dead zone and curve, sticks as movement, the menus' focus moves
add_executable(input_test tests/input_test.cpp)
target_include_directories(input_test PRIVATE ${SRC}/ui)
add_test(NAME input COMMAND input_test)
if(EMSCRIPTEN)
  # the web build's unit tests run in Node.js (the toolchain's emulator), on the
  # real files (fileio_test makes folders)
  foreach(t worldrandom_test wire_test voice_test anticheat_test hmac_test fileio_test map_test
      lobby_test signaling_test input_test)
    target_link_options(${t} PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB)
  endforeach()
  target_link_options(lobby_test PRIVATE -sFETCH=1)   # lobby.cpp's emscripten_fetch
endif()

# ---- GameNetworkingSockets (roadmap M7) ---------------------------------------
# Encrypted connections and P2P Internet games; on in the release archives.
# Off by default: python3 build.py --gns, or configure with the vcpkg toolchain
# and the "gns" feature of vcpkg.json, e.g.
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
  # On Windows GameNetworkingSockets comes as a DLL (vcpkg x64-windows, like
  # Valve ships it): the shared C++ runtime (above, and for the tests below). The
  # static vcpkg triplet builds it importing protobuf from a DLL that isn't there.
  add_test(NAME gns_loopback COMMAND gns_loopback_test)

  # The game: GnsLink (docs/M7.md), used with transport=gns in net.ini
  target_sources(Edifice PRIVATE src/net/gnslink.cpp src/net/gnssignal.cpp)
  target_compile_definitions(Edifice PRIVATE EDIFICE_GNS)
  target_link_libraries(Edifice PRIVATE ${HK_GNS_LIB})
  if(MSVC)
    # Release archives run on a clean Windows: the C++ runtime DLLs go next to
    # Edifice.exe (app-local deployment, allowed by the Visual Studio redist
    # terms), as do GameNetworkingSockets.dll and its DLLs (copied by vcpkg).
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      add_custom_command(TARGET Edifice POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:Edifice>
        COMMENT "Copying the C++ runtime DLLs next to Edifice.exe")
    else()
      message(WARNING "MSVC runtime DLLs not found: Edifice.exe will need the VC++ redistributable")
    endif()
  endif()
  # Sock over GnsLink against a game server (tests/run_gns_netplay_test.sh)
  add_executable(gns_netplay_test tests/gns_netplay_test.cpp src/net/gnslink.cpp src/net/gnssignal.cpp src/net/signaling.cpp
    src/net/sock.cpp src/net/udplink.cpp src/game/Vector.cpp)
  target_include_directories(gns_netplay_test PRIVATE ${INCLUDE_DIRS})
  target_link_libraries(gns_netplay_test PRIVATE ${HK_GNS_LIB})
  # P2P between two processes through a running lobby (tests/net_p2p.py)
  add_executable(gns_p2p_test tests/gns_p2p_test.cpp src/net/gnslink.cpp src/net/gnssignal.cpp
    src/net/signaling.cpp src/net/udplink.cpp src/net/lobby.cpp)
  target_include_directories(gns_p2p_test PRIVATE ${INCLUDE_DIRS})
  target_link_libraries(gns_p2p_test PRIVATE ${HK_GNS_LIB})
  if(HK_CURL_LIB)
    target_link_libraries(gns_p2p_test PRIVATE ${HK_CURL_LIB})
    target_compile_definitions(gns_p2p_test PRIVATE EDIFICE_LOBBY_CURL)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # vcpkg links its own OpenSSL statically for GameNetworkingSockets; exported,
    # its symbols would take the place of the system libcrypto that libcurl and
    # libssl use (HTTPS to the lobby then fails in the TLS handshake)
    foreach(t Edifice gns_netplay_test gns_p2p_test)
      target_link_options(${t} PRIVATE -Wl,--exclude-libs,ALL)
    endforeach()
  endif()
  if(MSVC)
    set_property(TARGET gns_loopback_test gns_netplay_test gns_p2p_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    target_compile_definitions(gns_netplay_test PRIVATE WIN32 _WINDOWS _MBCS _USE_MATH_DEFINES NOMINMAX
      _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
    target_compile_definitions(gns_p2p_test PRIVATE WIN32 _WINDOWS _MBCS _USE_MATH_DEFINES NOMINMAX
      _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
    target_link_libraries(gns_netplay_test PRIVATE ws2_32 opengl32)
    target_link_libraries(gns_p2p_test PRIVATE ws2_32 opengl32)
  else()
    foreach(t gns_netplay_test gns_p2p_test)
      target_compile_options(${t} PRIVATE -fsigned-char -Wno-write-strings
        -Wno-deprecated-declarations $<$<CXX_COMPILER_ID:GNU>:-fpermissive>)
    endforeach()
  endif()
endif()

# ---- WebRTC bridge (roadmap M14 task 3) ----------------------------------------
# A native host also takes browser players, over WebRTC DataChannels
# (src/net/rtchost.cpp): libdatachannel (MPL 2.0, with libjuice, usrsctp, plog,
# and the OpenSSL GameNetworkingSockets already has), the webrtc feature of
# vcpkg.json. Needs EDIFICE_GNS: the browsers' offers come through its signaling
# client (gnssignal.cpp). python3 build.py turns it on with --gns (--no-webrtc
# leaves it out). The browser side (webrtclink.cpp) is always in the web build.
option(EDIFICE_WEBRTC "Native hosts take browser players over WebRTC (needs EDIFICE_GNS, vcpkg)" OFF)
if(EDIFICE_WEBRTC AND NOT EDIFICE_GNS)
  message(FATAL_ERROR "EDIFICE_WEBRTC needs EDIFICE_GNS (the signaling client and STUN/TURN setup are GnsLink's)")
endif()
if(EDIFICE_WEBRTC)
  find_package(LibDataChannel CONFIG REQUIRED)
  target_sources(Edifice PRIVATE src/net/rtchost.cpp)
  target_compile_definitions(Edifice PRIVATE EDIFICE_WEBRTC)
  target_link_libraries(Edifice PRIVATE LibDataChannel::LibDataChannel)
endif()

# ---- Warnings as errors --------------------------------------------------------
# The CI builds with EDIFICE_WERROR=ON (.github/workflows/release.yml): a warning in
# the game or its tests fails the build. Third-party code (SDL, opus, curl, GNS) is in
# its own targets and keeps its warnings. Off by default: a newer compiler at home may
# warn about more, which shouldn't stop a build.
option(EDIFICE_WERROR "Treat warnings in the game code as errors" OFF)
if(EDIFICE_WERROR)
  set(HK_WERROR_TARGETS Edifice worldrandom_test wire_test voice_test anticheat_test hmac_test
    fileio_test map_test lobby_test signaling_test)
  if(EDIFICE_GNS)
    list(APPEND HK_WERROR_TARGETS gns_loopback_test gns_netplay_test gns_p2p_test)
  endif()
  foreach(t ${HK_WERROR_TARGETS})
    target_compile_options(${t} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>)
  endforeach()
endif()

# ---- Fuzzing -----------------------------------------------------------------
# libFuzzer harnesses of every parser of network or file input (tests/fuzz,
# roadmap M6). Off by default; see BUILD.md. With clang's libFuzzer they are
# real fuzzers (-fsanitize=fuzzer,address,undefined); otherwise (Apple clang,
# GCC) plain programs that run their inputs once (tests/fuzz/fuzz_main.cpp),
# with AddressSanitizer/UBSan when available. ctest runs each over its seeds.
option(EDIFICE_FUZZ "Build the fuzzing harnesses of tests/fuzz" OFF)
if(EDIFICE_FUZZ)
  include(CheckCXXSourceCompiles)
  set(HK_FUZZ_PROBE "#include <stddef.h>\n#include <stdint.h>\nextern \"C\" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) { return n && d[0]; }\n")
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer,address,undefined")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer,address,undefined")
  check_cxx_source_compiles("${HK_FUZZ_PROBE}" HK_HAVE_LIBFUZZER)
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
  check_cxx_source_compiles("${HK_FUZZ_PROBE}int main() { return LLVMFuzzerTestOneInput(0, 0); }\n" HK_HAVE_ASAN)
  unset(CMAKE_REQUIRED_FLAGS)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  if(HK_HAVE_LIBFUZZER)
    set(HK_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)
    set(HK_FUZZ_MAIN)
    message(STATUS "Fuzzing: libFuzzer harnesses")
  else()
    if(HK_HAVE_ASAN)
      set(HK_FUZZ_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    endif()
    set(HK_FUZZ_MAIN tests/fuzz/fuzz_main.cpp)
    message(STATUS "Fuzzing: no libFuzzer runtime, harnesses built as corpus runners")
  endif()
  set(HK_FUZZ_CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus)
  add_custom_target(fuzz)
  # fuzz_<name>: harness source, extra sources
  function(hk_fuzzer name source)
    add_executable(fuzz_${name} ${source} ${HK_FUZZ_MAIN} ${ARGN})
    target_include_directories(fuzz_${name} PRIVATE ${INCLUDE_DIRS})
    target_compile_definitions(fuzz_${name} PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION)
    target_compile_options(fuzz_${name} PRIVATE -g -O1 ${HK_FUZZ_FLAGS} -fsigned-char
      -Wno-write-strings -Wno-deprecated-declarations -Wno-parentheses)
    target_link_options(fuzz_${name} PRIVATE ${HK_FUZZ_FLAGS})
    add_dependencies(fuzz fuzz_${name})
    # every seed once (libFuzzer: -runs=0 only runs the corpus)
    add_test(NAME fuzz_${name} COMMAND fuzz_${name} -runs=0 ${HK_FUZZ_CORPUS}/fuzz_${name})
  endfunction()
  hk_fuzzer(sock tests/fuzz/fuzz_sock.cpp src/net/sock.cpp src/game/Vector.cpp)
  hk_fuzzer(simple tests/fuzz/fuzz_simple.cpp src/net/sock.cpp src/game/Vector.cpp)
  hk_fuzzer(hello tests/fuzz/fuzz_hello.cpp src/net/version.cpp src/game/Vector.cpp)
  foreach(msg Position:PlayerPosition FireGun:FireGunDirection KilledPosition:KilledPosition
      UpdateServerObject:UpdateServerObject Snapshot:ServerObjectSnapshot
      ServerVars:ServerVarsMessage RGD:RemoteGameDataMessage LandInfo:LandInfo)
    string(REPLACE ":" ";" pair ${msg})
    list(GET pair 0 msgname)
    list(GET pair 1 msgtype)
    string(TOLOWER ${msgname} lower)
    string(TOUPPER ${msgname} upper)
    hk_fuzzer(${lower} tests/fuzz/fuzz_msg.cpp src/game/Vector.cpp)
    target_compile_definitions(fuzz_${lower} PRIVATE FUZZ_NAME=${msgname} FUZZ_TYPE=${msgtype} FUZZ_${upper})
  endforeach()
  hk_fuzzer(playernames tests/fuzz/fuzz_playernames.cpp src/game/Vector.cpp)
  hk_fuzzer(chat tests/fuzz/fuzz_chat.cpp src/game/Vector.cpp)
  hk_fuzzer(blocks tests/fuzz/fuzz_blocks.cpp src/game/Vector.cpp)
  hk_fuzzer(voice tests/fuzz/fuzz_voice.cpp)
  hk_fuzzer(map tests/fuzz/fuzz_map.cpp src/game/mapfile.cpp)
  hk_fuzzer(map_legacy tests/fuzz/fuzz_map_legacy.cpp src/game/mapfile.cpp)
  hk_fuzzer(lobby tests/fuzz/fuzz_lobby.cpp src/net/lobby.cpp)
  hk_fuzzer(signaling tests/fuzz/fuzz_signaling.cpp src/net/signaling.cpp)
  # regenerates the seeds of tests/fuzz/corpus (see tests/fuzz/make_seeds.cpp)
  add_executable(fuzz_make_seeds tests/fuzz/make_seeds.cpp src/net/sock.cpp src/net/version.cpp
    src/game/mapfile.cpp src/game/Vector.cpp)
  target_include_directories(fuzz_make_seeds PRIVATE ${INCLUDE_DIRS})
  target_compile_options(fuzz_make_seeds PRIVATE -fsigned-char -Wno-write-strings -Wno-deprecated-declarations)
endif()
