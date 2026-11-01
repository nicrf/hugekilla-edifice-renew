//sokol_gfx's implementation (src/third_party/sokol, zlib license), alone in its file so
//that its warnings don't fail an EDIFICE_WERROR build. The backend (SOKOL_METAL,
//SOKOL_D3D11, SOKOL_GLCORE or, on the web, SOKOL_GLES3) comes from CMake; on macOS this is Objective-C++ with ARC.

#define SOKOL_IMPL
#include "sokol_gfx.h"
