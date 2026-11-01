//OpenGL 1.x and GLU, for the code that draws (the fixed pipeline, until M13 moves it
//behind src/render/). Only the files that call gl* include it.

#ifndef HK_GL_H
#define HK_GL_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   //<GL/gl.h> needs its WINGDIAPI and APIENTRY first
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
