//Between the parts of the sokol_gfx backend (HK_RENDER_SOKOL): renderer_sokol.cpp
//(the batches, recorded then drawn), texture_sokol.cpp (the texture registry) and
//sokol_native.cpp (the device, the swapchain and the reads back, which sokol_gfx
//leaves to the 3D API).

#ifndef RSOKOLH
#define RSOKOLH

#include "sokol_gfx.h"

//sokol_native.cpp
sg_environment RSokolEnvironment();       //for sg_setup
sg_swapchain RSokolSwapchain();           //the window's next drawable, for the present pass
void RSokolPresentNative();               //after sg_commit (D3D11 Present, GL swap; Metal presents in the pass)
//read back the offscreen frame (renderer_sokol.cpp), after its pass and an sg_commit:
//x, y from the bottom left, as GL. Color: 4 bytes a pixel (RGBA), rows bottom-up.
void RSokolReadColor(sg_image image,int x,int y,int w,int h,unsigned char *rgba);
int RSokolReadDepth(sg_image image,int x,int y,float *depth);   //0 if it couldn't
int RSokolOriginTopLeft();                //Metal, D3D11: the image's first row is the top

//renderer_sokol.cpp
void RSokolEndFrame();                    //RPresent: draws, presents, frees what the frame replaced
void RSokolShutdown();                    //before sg_shutdown

//texture_sokol.cpp: the slot's texture (invalid id: none, as GL's texture 0) and its
//sampler (TexFilter); the view stays valid until the end of the frame even if the
//slot is uploaded again
sg_view TexSokolView(int slot,int *filter);
void TexSokolEndFrame();

#endif
