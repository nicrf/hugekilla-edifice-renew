//Between renderer.cpp (state, batch) and a backend: renderer_gl.cpp and
//texture_gl.cpp for OpenGL. Only the renderer includes this.

#ifndef RBACKENDH
#define RBACKENDH

#include "renderer.h"

//everything a draw depends on; a backend draws a batch with exactly this
struct RState
{
	int caps;             //RCAP_*
	RBlend blend;
	RFace cullface;
	int depthwrite;
	float linewidth,pointsize;
	float fogstart,fogend,fogcolor[4];
	float clearcolor[4];
	int texture;          //registry slot, -1: none bound (GL's texture 0)
	int viewport[4],scissor[4];
	Mat4 projection,modelview;
};

//a run of vertices of the batch drawn as one primitive type
struct RCmd
{
	RPrim prim;
	int first,count;
};

void RBackendInit();
void RBackendDraw(const RState &state,const RVertex *verts,const RCmd *cmds,int numcmds);
void RBackendClear(const RState &state,int color,int depth);
void RBackendReadPixels(int x,int y,int w,int h,int bgr,unsigned char *out);
void RBackendReadDepth(int x,int y,float *depth);

//texture_gl.cpp: binds the slot's texture if it isn't already
void TexBackendBind(int slot);

#endif
