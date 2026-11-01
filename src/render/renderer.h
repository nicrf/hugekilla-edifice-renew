//The renderer (M13): what the game asks of the GPU, without OpenGL. The backend
//(renderer_gl.cpp, fixed-function OpenGL) is the only code that calls gl*; a
//sokol_gfx backend (M13.3) implements the same functions.

#ifndef RENDERERH
#define RENDERERH

#include "rmath.h"

//matrices: the projection, and the modelview (camera, then the model drawn). The 2D
//pass (menus, HUD, console) draws in normalized device coordinates, -1..1 on both
//axes, with both matrices the identity.
void RSetProjection(const Mat4 &m);
void RSetModelView(const Mat4 &m);
const Mat4 &RProjection();
const Mat4 &RModelView();
void RSet2D();   //identity projection and modelview

//the drawing area in pixels of the window's drawable (x, y from the bottom left)
void RViewport(int x,int y,int w,int h);
void RGetViewport(int vp[4]);

//reads back the frame being drawn: rows bottom-up and packed (no padding), 3 bytes a
//pixel, RGB or BGR (HK_SHOT, the screenshot key); and the depth at one pixel, 0..1
//(the sun flare looks for what hides the sun)
void RReadPixels(int x,int y,int w,int h,int bgr,unsigned char *out);
void RReadDepth(int x,int y,float *depth);   //outside the window: may leave *depth as it was

#endif
