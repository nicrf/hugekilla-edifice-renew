//OpenGL 1.x and GLU, for the code that draws (the fixed pipeline, until M13 moves it
//behind src/render/). Only the files that call gl* include it.

#ifndef HK_GL_H
#define HK_GL_H

#ifdef _WIN32
#include "win32compat.h"   //<GL/gl.h> needs windows.h's WINGDIAPI and APIENTRY first
#include <GL/gl.h>
#include <GL/glu.h>
#elif defined(__APPLE__)
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION   //deprecated on macOS, not gone (M13: sokol_gfx)
#endif
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#endif
