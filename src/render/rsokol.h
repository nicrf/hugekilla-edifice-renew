//Between the parts of the sokol_gfx backend (HK_RENDER_SOKOL): renderer_sokol.cpp
//(the batches, recorded then drawn), texture_sokol.cpp (the texture registry) and
//sokol_native.cpp (the device, the swapchain and the reads back, which sokol_gfx
//leaves to the 3D API).

#ifndef RSOKOLH
#define RSOKOLH

#include "sokol_gfx.h"
#include "renderer.h"

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

//a recorded draw, and the uniforms of the draw shader (edifice.glsl), shared with
//the Ultra path (renderer_ultra.cpp), which draws the scene's commands its way
struct RSokolUniforms
{
	Mat4 proj,modelview;                  //draw_vs_params
	float fogcolor[4],fog[4];             //draw_fs_params
	Mat4 model;                           //Ultra: world from object (the camera's inverse times modelview)
};
struct RSokolCmd
{
	int pipe;                 //classic pipeline key (RSokolPipeDesc)
	int first,count;          //vertices of the frame's buffer
	int uniforms;             //RSokolFrame::uniforms
	sg_view view;
	int sampler;
	int viewport[4];          //width 0: the whole frame (a clear)
	int scissor[4];
	int material;             //RMAT_* (0 outside the Ultra scene)
	int texslot;              //the bound texture's slot, -1 none
	int mark;                 //RMARK_* (rbackend.h), count 0; -1: a draw
};
struct RSokolFrame
{
	sg_buffer vbuf;           //every vertex of the frame
	const RSokolUniforms *uniforms;
	const sg_sampler *samplers;           //TEXF_*
	sg_view white;
	sg_image colorimage,depthimage;       //the frame (RGBA8, depth): the HUD's, what is presented
	sg_view colorview,depthview;
	int width,height;
	int ztozero;              //clip z 0..1 (Metal, D3D11)
	int origintopleft;        //an image's first row is the top (Metal, D3D11)
};
//the classic pipeline of a key: primitive, blending, depth, culling, color writes
sg_pipeline_desc RSokolPipeDesc(int key);
Mat4 RSokolApiProjection(const Mat4 &gl);   //GL's clip z to the API's
int RSokolInverse(const Mat4 &a,Mat4 *out);

//renderer_ultra.cpp: draws cmds[0..n) (the BEGIN mark first) into the frame, the
//scene then its post-processing, outside any pass
int RUltraBackendSupported();
void RUltraInit();
void RUltraShutdown();
void RUltraSetScene(const RUltraScene &scene);
void RUltraDrawScene(const RSokolFrame &frame,const RSokolCmd *cmds,int n);

//texture_sokol.cpp: the slot's texture (invalid id: none, as GL's texture 0) and its
//sampler (TexFilter); the view stays valid until the end of the frame even if the
//slot is uploaded again
sg_view TexSokolView(int slot,int *filter);
void TexSokolEndFrame();

#endif
