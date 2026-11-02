//sokol_gfx's window side (rcontext.h, rsokol.h), one 3D API per platform: Metal on
//macOS (compiled as Objective-C++ with ARC), D3D11 on Windows,
//a GL 4.1 core context elsewhere (or anywhere with HK_SOKOL_BACKEND=GLCORE), WebGL2
//(GLES3) in the web build (Emscripten, roadmap M14). Here is
//what sokol_gfx leaves to the application: the device, the window's swapchain and its
//presentation, and reading an image back (HK_SHOT, screenshots, the sun's depth).

#if defined(SOKOL_D3D11)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#elif defined(SOKOL_GLCORE)
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif
#elif defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#endif
#include "rsokol.h"
#include "rcontext.h"
#include "renderer.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static SDL_Window *window;
static int vsync=1;

//---- Metal ----------------------------------------------------------------------
#if defined(SOKOL_METAL)
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL_metal.h>
#include <SDL_syswm.h>

//libobjc's, what @autoreleasepool compiles to: a pool per frame, since the game's
//loop isn't Cocoa's (the drawables and command buffers are autoreleased)
extern "C" void *objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void *pool);

static SDL_MetalView metalview;
static CAMetalLayer *layer;
static id<MTLDevice> device;
static id<CAMetalDrawable> drawable;
static void *framepool;
static NSWindow *nswindow;
static Uint32 presentpause;       //SDL_GetTicks() until which the window isn't presented

unsigned RWindowFlags()
{
	return SDL_WINDOW_METAL;
}

int RCreateContext(SDL_Window *w)
{
	window=w;
	framepool=objc_autoreleasePoolPush();
	metalview=SDL_Metal_CreateView(window);
	if(!metalview)
	{
		fprintf(stderr,"SDL_Metal_CreateView failed: %s\n",SDL_GetError());
		return 0;
	}
	layer=(__bridge CAMetalLayer *)SDL_Metal_GetLayer(metalview);
	device=MTLCreateSystemDefaultDevice();
	if(!layer||!device)
	{
		fprintf(stderr,"no Metal device\n");
		return 0;
	}
	layer.device=device;
	layer.pixelFormat=MTLPixelFormatBGRA8Unorm;   //not sRGB: the colors go out as GL wrote them
	layer.framebufferOnly=YES;
	layer.displaySyncEnabled=vsync?YES:NO;
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if(SDL_GetWindowWMInfo(window,&wm)) nswindow=wm.info.cocoa.window;
	return 1;
}

void RDestroyContext()
{
	RSokolShutdown();
	drawable=nil;
	layer=nil;
	device=nil;
	nswindow=nil;
	if(metalview) SDL_Metal_DestroyView(metalview);
	metalview=0;
	if(framepool) objc_autoreleasePoolPop(framepool);
	framepool=0;
}

void RDrawableSize(int *w,int *h)
{
	*w=*h=0;
	if(window) SDL_Metal_GetDrawableSize(window,w,h);
}

void RSetSwapInterval(int on)
{
	vsync=on;
	if(layer) layer.displaySyncEnabled=on?YES:NO;
}

const char *RBackendName()
{
	return "sokol_gfx Metal";
}

sg_environment RSokolEnvironment()
{
	sg_environment env;
	memset(&env,0,sizeof(env));
	env.defaults.color_format=SG_PIXELFORMAT_BGRA8;
	env.defaults.depth_format=SG_PIXELFORMAT_NONE;
	env.defaults.sample_count=1;
	env.metal.device=(__bridge const void *)device;
	return env;
}

sg_swapchain RSokolSwapchain()
{
	sg_swapchain sc;
	memset(&sc,0,sizeof(sc));
	int w,h;
	RDrawableSize(&w,&h);
	if(w>0&&h>0&&(layer.drawableSize.width!=w||layer.drawableSize.height!=h))
		layer.drawableSize=CGSizeMake(w,h);
	//hidden (behind other windows, the display asleep): no drawable comes back, and
	//nextDrawable would wait a second for each
	if(nswindow&&!(nswindow.occlusionState&NSWindowOcclusionStateVisible))
	{
		sc.invalid=true;
		return sc;
	}
	//the same when the compositor stops taking frames while the window still looks
	//visible (a VM without a display): after a long wait, skip half a second of frames
	//so that the game runs on
	if(presentpause&&!SDL_TICKS_PASSED(SDL_GetTicks(),presentpause))
	{
		sc.invalid=true;
		return sc;
	}
	presentpause=0;
	Uint32 asked=SDL_GetTicks();
	drawable=[layer nextDrawable];
	if(SDL_GetTicks()-asked>100) presentpause=SDL_GetTicks()+500;
	if(!drawable)
	{
		sc.invalid=true;
		return sc;
	}
	sc.width=(int)drawable.texture.width;
	sc.height=(int)drawable.texture.height;
	sc.sample_count=1;
	sc.color_format=SG_PIXELFORMAT_BGRA8;
	sc.depth_format=SG_PIXELFORMAT_NONE;
	sc.metal.current_drawable=(__bridge const void *)drawable;
	return sc;
}

void RSokolPresentNative()
{
	drawable=nil;   //presented by sokol_gfx at the end of the pass
}

int RSokolOriginTopLeft()
{
	return 1;
}

int RSokolSoftwareRenderer()
{
	return 0;
}

//copies a region of a (private) texture to a shared buffer and waits: the frame was
//committed before, and the queue runs command buffers in order
static void ReadTexture(sg_image image,int x,int y,int w,int h,int pixelsize,unsigned char *out)
{
	@autoreleasepool
	{
		sg_mtl_image_info info=sg_mtl_query_image_info(image);
		id<MTLTexture> tex=(__bridge id<MTLTexture>)info.tex[info.active_slot];
		id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
		if(!tex||!queue) return;
		NSUInteger rowbytes=(NSUInteger)w*pixelsize;
		id<MTLBuffer> buf=[device newBufferWithLength:rowbytes*h options:MTLResourceStorageModeShared];
		id<MTLCommandBuffer> cb=[queue commandBuffer];
		id<MTLBlitCommandEncoder> blit=[cb blitCommandEncoder];
		[blit copyFromTexture:tex sourceSlice:0 sourceLevel:0
			sourceOrigin:MTLOriginMake(x,tex.height-y-h,0) sourceSize:MTLSizeMake(w,h,1)
			toBuffer:buf destinationOffset:0 destinationBytesPerRow:rowbytes destinationBytesPerImage:rowbytes*h];
		[blit endEncoding];
		[cb commit];
		[cb waitUntilCompleted];
		//top-down there, bottom-up for the game
		const unsigned char *src=(const unsigned char *)buf.contents;
		for(int r=0;r<h;r++)
			memcpy(out+(size_t)(h-1-r)*rowbytes,src+(size_t)r*rowbytes,rowbytes);
	}
}

void RSokolReadColor(sg_image image,int x,int y,int w,int h,unsigned char *rgba)
{
	ReadTexture(image,x,y,w,h,4,rgba);
}

int RSokolReadDepth(sg_image image,int x,int y,float *depth)
{
	ReadTexture(image,x,y,1,1,4,(unsigned char *)depth);   //Depth32Float
	return 1;
}

static void WriteDump(int frame,int w,int h,const unsigned char *rgba,int topdown);

//HK_DUMPFRAMES: a copy of the frame, blitted in a command buffer of its own after the
//frame's and read once the GPU is done with it, so that the frames keep their pacing
struct PendingDump
{
	id<MTLCommandBuffer> cb;
	id<MTLBuffer> buf;
	int w,h,frame;
};
static std::vector<PendingDump> pendingdumps;

static void DumpQueue(sg_image image,int w,int h,int frame)
{
	sg_mtl_image_info info=sg_mtl_query_image_info(image);
	id<MTLTexture> tex=(__bridge id<MTLTexture>)info.tex[info.active_slot];
	id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
	if(!tex||!queue) return;
	NSUInteger rowbytes=(NSUInteger)w*4;
	PendingDump d;
	d.buf=[device newBufferWithLength:rowbytes*h options:MTLResourceStorageModeShared];
	d.cb=[queue commandBuffer];
	id<MTLBlitCommandEncoder> blit=[d.cb blitCommandEncoder];
	[blit copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
		sourceSize:MTLSizeMake(w,h,1) toBuffer:d.buf destinationOffset:0
		destinationBytesPerRow:rowbytes destinationBytesPerImage:rowbytes*h];
	[blit endEncoding];
	[d.cb commit];
	d.w=w;
	d.h=h;
	d.frame=frame;
	pendingdumps.push_back(d);
}

static void DumpPoll(int wait)
{
	size_t k=0;
	for(size_t i=0;i<pendingdumps.size();i++)
	{
		PendingDump &d=pendingdumps[i];
		if(wait) [d.cb waitUntilCompleted];
		if(d.cb.status<MTLCommandBufferStatusCompleted)
		{
			pendingdumps[k++]=d;
			continue;
		}
		WriteDump(d.frame,d.w,d.h,(const unsigned char *)d.buf.contents,1);
	}
	pendingdumps.resize(k);
}

static void NewFramePool()
{
	if(framepool) objc_autoreleasePoolPop(framepool);
	framepool=objc_autoreleasePoolPush();
}

//---- D3D11 ----------------------------------------------------------------------
#elif defined(SOKOL_D3D11)
#include <SDL_syswm.h>

static ID3D11Device *device;
static ID3D11DeviceContext *context;
static IDXGISwapChain *swapchain;
static ID3D11RenderTargetView *rtv;
static int swapw,swaph;
static ID3D11Texture2D *depthstaging;   //the depth buffer's copy for RSokolReadDepth
static D3D11_TEXTURE2D_DESC depthstagingdesc;

unsigned RWindowFlags()
{
	return 0;
}

static void CreateRenderTarget()
{
	ID3D11Texture2D *back=0;
	if(SUCCEEDED(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void **)&back))&&back)
	{
		device->CreateRenderTargetView(back,0,&rtv);
		back->Release();
	}
}

void RDrawableSize(int *w,int *h)
{
	*w=*h=0;
	if(!window) return;
#if SDL_VERSION_ATLEAST(2,26,0)
	SDL_GetWindowSizeInPixels(window,w,h);
#else
	SDL_GetWindowSize(window,w,h);
#endif
}

//a hardware device, else WARP (a VM or a CI runner without a GPU); flip model, else
//the older blt model (Windows 7, 8)
int RCreateContext(SDL_Window *w)
{
	window=w;
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if(!SDL_GetWindowWMInfo(window,&wm))
	{
		fprintf(stderr,"SDL_GetWindowWMInfo failed: %s\n",SDL_GetError());
		return 0;
	}
	RDrawableSize(&swapw,&swaph);
	DXGI_SWAP_CHAIN_DESC sd;
	memset(&sd,0,sizeof(sd));
	sd.BufferDesc.Width=swapw;
	sd.BufferDesc.Height=swaph;
	sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow=wm.info.win.window;
	sd.Windowed=TRUE;
	sd.SampleDesc.Count=1;
	UINT flags=0;
#ifndef NDEBUG
	flags|=D3D11_CREATE_DEVICE_DEBUG;   //validation messages, when the SDK layers are installed
#endif
	static const D3D_DRIVER_TYPE types[2]={D3D_DRIVER_TYPE_HARDWARE,D3D_DRIVER_TYPE_WARP};
	HRESULT hr=E_FAIL;
	for(int attempt=0;attempt<8&&FAILED(hr);attempt++)
	{
		int flip=!(attempt&1),debug=!(attempt&2);
		if(debug&&!flags) continue;
		sd.SwapEffect=flip?DXGI_SWAP_EFFECT_FLIP_DISCARD:DXGI_SWAP_EFFECT_DISCARD;
		sd.BufferCount=flip?2:1;
		hr=D3D11CreateDeviceAndSwapChain(0,types[attempt/4],0,debug?flags:0,0,0,D3D11_SDK_VERSION,
			&sd,&swapchain,&device,0,&context);
	}
	if(FAILED(hr))
	{
		fprintf(stderr,"D3D11CreateDeviceAndSwapChain failed: 0x%08lx\n",(unsigned long)hr);
		return 0;
	}
	//SDL switches to full screen itself
	IDXGIFactory *factory=0;
	if(SUCCEEDED(swapchain->GetParent(__uuidof(IDXGIFactory),(void **)&factory))&&factory)
	{
		factory->MakeWindowAssociation(wm.info.win.window,DXGI_MWA_NO_ALT_ENTER);
		factory->Release();
	}
	CreateRenderTarget();
	return rtv!=0;
}

void RDestroyContext()
{
	RSokolShutdown();
	if(depthstaging) depthstaging->Release();
	if(rtv) rtv->Release();
	if(swapchain) swapchain->Release();
	if(context) context->Release();
	if(device) device->Release();
	depthstaging=0;
	rtv=0;
	swapchain=0;
	context=0;
	device=0;
}

void RSetSwapInterval(int on)
{
	vsync=on;
}

const char *RBackendName()
{
	return "sokol_gfx D3D11";
}

sg_environment RSokolEnvironment()
{
	sg_environment env;
	memset(&env,0,sizeof(env));
	env.defaults.color_format=SG_PIXELFORMAT_RGBA8;
	env.defaults.depth_format=SG_PIXELFORMAT_NONE;
	env.defaults.sample_count=1;
	env.d3d11.device=device;
	env.d3d11.device_context=context;
	return env;
}

sg_swapchain RSokolSwapchain()
{
	sg_swapchain sc;
	memset(&sc,0,sizeof(sc));
	int w,h;
	RDrawableSize(&w,&h);
	if(w>0&&h>0&&(w!=swapw||h!=swaph))
	{
		if(rtv) rtv->Release();
		rtv=0;
		swapchain->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0);
		swapw=w;
		swaph=h;
		CreateRenderTarget();
	}
	if(!rtv)
	{
		sc.invalid=true;
		return sc;
	}
	sc.width=swapw;
	sc.height=swaph;
	sc.sample_count=1;
	sc.color_format=SG_PIXELFORMAT_RGBA8;
	sc.depth_format=SG_PIXELFORMAT_NONE;
	sc.d3d11.render_view=rtv;
	return sc;
}

void RSokolPresentNative()
{
	swapchain->Present(vsync?1:0,0);
}

int RSokolOriginTopLeft()
{
	return 1;
}

//WARP (no GPU) draws the Ultra graphics at a fair speed: not counted as one
int RSokolSoftwareRenderer()
{
	return 0;
}

void RSokolReadColor(sg_image image,int x,int y,int w,int h,unsigned char *rgba)
{
	ID3D11Texture2D *src=(ID3D11Texture2D *)sg_d3d11_query_image_info(image).tex2d;
	if(!src) return;
	D3D11_TEXTURE2D_DESC desc;
	src->GetDesc(&desc);
	int texh=(int)desc.Height;
	desc.Width=w;
	desc.Height=h;
	desc.MipLevels=1;
	desc.ArraySize=1;
	desc.Usage=D3D11_USAGE_STAGING;
	desc.BindFlags=0;
	desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
	desc.MiscFlags=0;
	ID3D11Texture2D *staging=0;
	if(FAILED(device->CreateTexture2D(&desc,0,&staging))||!staging) return;
	D3D11_BOX box={(UINT)x,(UINT)(texh-y-h),0,(UINT)(x+w),(UINT)(texh-y),1};
	context->CopySubresourceRegion(staging,0,0,0,0,src,0,&box);
	D3D11_MAPPED_SUBRESOURCE m;
	if(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&m)))
	{
		//top-down there, bottom-up for the game
		for(int r=0;r<h;r++)
			memcpy(rgba+(size_t)(h-1-r)*w*4,(const unsigned char *)m.pData+(size_t)r*m.RowPitch,(size_t)w*4);
		context->Unmap(staging,0);
	}
	staging->Release();
}

//a depth buffer only copies whole (CopySubresourceRegion can't take a box of it)
int RSokolReadDepth(sg_image image,int x,int y,float *depth)
{
	ID3D11Texture2D *src=(ID3D11Texture2D *)sg_d3d11_query_image_info(image).tex2d;
	if(!src) return 0;
	D3D11_TEXTURE2D_DESC desc;
	src->GetDesc(&desc);
	if(desc.Format!=DXGI_FORMAT_D32_FLOAT&&desc.Format!=DXGI_FORMAT_R32_TYPELESS&&desc.Format!=DXGI_FORMAT_R32_FLOAT)
		return 0;
	if(!depthstaging||depthstagingdesc.Width!=desc.Width||depthstagingdesc.Height!=desc.Height)
	{
		if(depthstaging) depthstaging->Release();
		depthstaging=0;
		depthstagingdesc=desc;
		depthstagingdesc.Format=DXGI_FORMAT_R32_FLOAT;   //same R32 family: CopyResource allows it
		depthstagingdesc.Usage=D3D11_USAGE_STAGING;
		depthstagingdesc.BindFlags=0;
		depthstagingdesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		depthstagingdesc.MiscFlags=0;
		if(FAILED(device->CreateTexture2D(&depthstagingdesc,0,&depthstaging))) depthstaging=0;
	}
	if(!depthstaging) return 0;
	context->CopyResource(depthstaging,src);
	D3D11_MAPPED_SUBRESOURCE m;
	if(FAILED(context->Map(depthstaging,0,D3D11_MAP_READ,0,&m))) return 0;
	int row=(int)desc.Height-1-y;
	memcpy(depth,(const unsigned char *)m.pData+(size_t)row*m.RowPitch+(size_t)x*4,4);
	context->Unmap(depthstaging,0);
	return 1;
}

static void WriteDump(int frame,int w,int h,const unsigned char *rgba,int topdown);

//HK_DUMPFRAMES: read at once (the frame was committed)
static void DumpQueue(sg_image image,int w,int h,int frame)
{
	std::vector<unsigned char> rgba((size_t)w*h*4);
	RSokolReadColor(image,0,0,w,h,rgba.data());
	WriteDump(frame,w,h,rgba.data(),0);
}

static void DumpPoll(int wait)
{
	(void)wait;
}

static void NewFramePool()
{
}

//---- GL 4.1 core ----------------------------------------------------------------
#elif defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)

static SDL_GLContext glcontext;
static GLuint readfbo;

unsigned RWindowFlags()
{
#if defined(SOKOL_GLES3)
	//WebGL2 (sokol-shdc's glsl300es); no depth, the frame is drawn offscreen, and
	//no alpha: the page would show through what the game leaves transparent
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
#else
	//4.1: what sokol-shdc's GLSL (glsl410) and macOS need; no depth here, the frame
	//is drawn offscreen
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
	return SDL_WINDOW_OPENGL;
}

int RCreateContext(SDL_Window *w)
{
	window=w;
	glcontext=SDL_GL_CreateContext(window);
	if(!glcontext)
	{
		fprintf(stderr,"SDL_GL_CreateContext (4.1 core) failed: %s\n",SDL_GetError());
		return 0;
	}
	SDL_GL_MakeCurrent(window,glcontext);
	return 1;
}

void RDestroyContext()
{
	if(readfbo) glDeleteFramebuffers(1,&readfbo);
	readfbo=0;
	RSokolShutdown();
	if(glcontext) SDL_GL_DeleteContext(glcontext);
	glcontext=0;
}

void RDrawableSize(int *w,int *h)
{
	*w=*h=0;
	if(window) SDL_GL_GetDrawableSize(window,w,h);
}

void RSetSwapInterval(int on)
{
	vsync=on;
#if !defined(SOKOL_GLES3)
	//web: the browser's animation frames pace the game (SDL would make this the
	//main loop's timing, before there is one)
	SDL_GL_SetSwapInterval(on);
#endif
}

const char *RBackendName()
{
#if defined(SOKOL_GLES3)
	return "sokol_gfx WebGL2";
#else
	return "sokol_gfx GL 4.1 core";
#endif
}

sg_environment RSokolEnvironment()
{
	sg_environment env;
	memset(&env,0,sizeof(env));
	env.defaults.color_format=SG_PIXELFORMAT_RGBA8;
	env.defaults.depth_format=SG_PIXELFORMAT_NONE;
	env.defaults.sample_count=1;
	return env;
}

sg_swapchain RSokolSwapchain()
{
	sg_swapchain sc;
	memset(&sc,0,sizeof(sc));
	RDrawableSize(&sc.width,&sc.height);
	sc.sample_count=1;
	sc.color_format=SG_PIXELFORMAT_RGBA8;
	sc.depth_format=SG_PIXELFORMAT_NONE;
	sc.gl.framebuffer=0;   //the window's
	return sc;
}

void RSokolPresentNative()
{
	SDL_GL_SwapWindow(window);
}

int RSokolOriginTopLeft()
{
	return 0;
}

//Mesa's llvmpipe, softpipe (swrast): their anisotropic filtering takes seconds a
//frame (measured: 42 s against 13 ms without, llvmpipe, 400x300)
int RSokolSoftwareRenderer()
{
	const char *r=(const char *)glGetString(GL_RENDERER);
	return r&&(strstr(r,"llvmpipe")||strstr(r,"softpipe")||strstr(r,"swrast"));
}

//glReadPixels through a framebuffer of our own on the image (sokol_gfx then
//forgets what it thinks GL's state is)
static void ReadGL(sg_image image,GLenum attachment,int x,int y,int w,int h,GLenum format,GLenum type,void *out)
{
	sg_gl_image_info info=sg_gl_query_image_info(image);
	if(!readfbo) glGenFramebuffers(1,&readfbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER,readfbo);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,0,0);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER,attachment,info.tex_target,info.tex[info.active_slot],0);
	glReadBuffer(attachment==GL_COLOR_ATTACHMENT0?GL_COLOR_ATTACHMENT0:GL_NONE);
	glPixelStorei(GL_PACK_ALIGNMENT,1);
	glReadPixels(x,y,w,h,format,type,out);
	glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
	sg_reset_state_cache();
}

void RSokolReadColor(sg_image image,int x,int y,int w,int h,unsigned char *rgba)
{
	ReadGL(image,GL_COLOR_ATTACHMENT0,x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
}

int RSokolReadDepth(sg_image image,int x,int y,float *depth)
{
#if defined(SOKOL_GLES3)
	//WebGL2 reads no depth back: the sun keeps its last depth (1, never hidden)
	(void)image;(void)x;(void)y;(void)depth;
	return 0;
#else
	ReadGL(image,GL_DEPTH_ATTACHMENT,x,y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,depth);
	return 1;
#endif
}

static void WriteDump(int frame,int w,int h,const unsigned char *rgba,int topdown);

//HK_DUMPFRAMES: read at once (the frame was committed)
static void DumpQueue(sg_image image,int w,int h,int frame)
{
	std::vector<unsigned char> rgba((size_t)w*h*4);
	RSokolReadColor(image,0,0,w,h,rgba.data());
	WriteDump(frame,w,h,rgba.data(),0);
}

static void DumpPoll(int wait)
{
	(void)wait;
}

static void NewFramePool()
{
}

#else
#error "HK_RENDER_SOKOL: define SOKOL_METAL, SOKOL_D3D11, SOKOL_GLCORE or SOKOL_GLES3"
#endif

//HK_DUMPFRAMES=N saves N presented frames from the HK_DUMPFRAMES_START-th (default 300)
//as HK_DUMPFRAMES_DIR/frameNNNNN.ppm, shrunk HK_DUMPFRAMES_SCALE times (default 2):
//what the window got, with VSync and the frame pacing kept (HK_SHOT reads back
//without VSync), to catch what changes from one frame to the next
static int dumpcount=-1,dumpstart,dumpscale;
static const char *dumpdir;

//rgba: rows top-down (Metal's blit) or bottom-up (RSokolReadColor)
static void WriteDump(int frame,int w,int h,const unsigned char *rgba,int topdown)
{
	char path[1024];
	snprintf(path,sizeof(path),"%s/frame%05d.ppm",dumpdir,frame);
	FILE *f=fopen(path,"wb");
	if(!f) return;
	int s=dumpscale,ow=w/s,oh=h/s;
	fprintf(f,"P6\n%d %d\n255\n",ow,oh);
	std::vector<unsigned char> row((size_t)ow*3);
	for(int y=0;y<oh;y++)
	{
		int sy=topdown?y*s:h-1-y*s;
		for(int x=0;x<ow;x++)
		{
			const unsigned char *p=rgba+((size_t)sy*w+(size_t)x*s)*4;
			row[x*3]=p[0];
			row[x*3+1]=p[1];
			row[x*3+2]=p[2];
		}
		fwrite(row.data(),1,row.size(),f);
	}
	fclose(f);
}

void RSokolDumpFrame(sg_image image,int w,int h)
{
	static int frame;
	if(dumpcount<0)
	{
		const char *n=getenv("HK_DUMPFRAMES");
		dumpcount=n?atoi(n):0;
		dumpstart=getenv("HK_DUMPFRAMES_START")?atoi(getenv("HK_DUMPFRAMES_START")):300;
		dumpscale=getenv("HK_DUMPFRAMES_SCALE")?atoi(getenv("HK_DUMPFRAMES_SCALE")):2;
		if(dumpscale<1) dumpscale=1;
		dumpdir=getenv("HK_DUMPFRAMES_DIR")?getenv("HK_DUMPFRAMES_DIR"):".";
	}
	if(dumpcount<=0) return;
	frame++;
	if(frame>=dumpstart&&frame<dumpstart+dumpcount) DumpQueue(image,w,h,frame);
	DumpPoll(frame>=dumpstart+dumpcount-1);
	if(frame==dumpstart+dumpcount-1) fprintf(stderr,"HK_DUMPFRAMES: %d frames in %s\n",dumpcount,dumpdir);
}

void RPresent()
{
	RFlush();   //the end of the frame may still be batched
	RSokolEndFrame();
	NewFramePool();
}
