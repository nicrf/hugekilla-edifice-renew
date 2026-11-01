cmake_minimum_required(VERSION 3.16)
project(Edifice VERSION 0.4.1 LANGUAGES C CXX)

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
  # With EDIFICE_GNS (below) the shared one for everything, as its DLL needs.
  if(EDIFICE_GNS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
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
if(HK_NUM_OSX_ARCHS GREATER 1)
  # universal macOS build: opus picks SSE or NEON sources for the host CPU only
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
if(EDIFICE_LOBBY)
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
    set(CURL_STATIC_CRT ON CACHE BOOL "" FORCE)        # same static CRT as the game
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
  src/ui/console.cpp
  src/ui/ui.cpp
  src/ui/options.cpp
  src/ui/serversetup.cpp
  src/ui/scoreboard.cpp
  src/ui/buildmenu.cpp
  src/ui/chat.cpp
  src/ui/joinscreen.cpp
  src/audio/sound_sdl.cpp
  src/audio/voice.cpp
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

target_link_libraries(Edifice PRIVATE ${HK_SDL_LIBS} Opus::opus OpenGL::GL)
if(HK_CURL_LIB)
  target_link_libraries(Edifice PRIVATE ${HK_CURL_LIB})
  target_compile_definitions(Edifice PRIVATE EDIFICE_LOBBY_CURL)
endif()
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
add_executable(wire_test tests/wire_test.cpp)
target_include_directories(wire_test PRIVATE ${SRC}/net)
add_test(NAME wire COMMAND wire_test)
add_executable(voice_test tests/voice_test.cpp)
target_include_directories(voice_test PRIVATE ${SRC}/net ${SRC}/audio)
target_link_libraries(voice_test PRIVATE Opus::opus)
add_test(NAME voice COMMAND voice_test)
set(HK_TEST_INCLUDES ${SRC}/game ${SRC}/net ${SRC}/compat)
if(NOT WIN32)
  list(APPEND HK_TEST_INCLUDES ${SRC}/compat/include)   # "windows.h": DWORD...
endif()
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
  # On Windows GameNetworkingSockets comes as a DLL (vcpkg x64-windows, like
  # Valve ships it): the shared C++ runtime (above, and for the tests below). The
  # static vcpkg triplet builds it importing protobuf from a DLL that isn't there.
  add_test(NAME gns_loopback COMMAND gns_loopback_test)

  # The game: GnsLink (docs/M7.md), used with transport=gns in net.ini
  target_sources(Edifice PRIVATE src/net/gnslink.cpp src/net/gnssignal.cpp)
  target_compile_definitions(Edifice PRIVATE EDIFICE_GNS)
  target_link_libraries(Edifice PRIVATE ${HK_GNS_LIB})
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
      target_compile_options(${t} PRIVATE -fsigned-char -fwrapv -Wno-write-strings
        -Wno-deprecated-declarations $<$<CXX_COMPILER_ID:GNU>:-fpermissive>)
    endforeach()
  endif()
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
    target_compile_options(fuzz_${name} PRIVATE -g -O1 ${HK_FUZZ_FLAGS} -fsigned-char -fwrapv
      -fno-strict-aliasing -Wno-write-strings -Wno-deprecated-declarations -Wno-parentheses)
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
  target_compile_options(fuzz_make_seeds PRIVATE -fsigned-char -fwrapv -Wno-write-strings -Wno-deprecated-declarations)
endif()
