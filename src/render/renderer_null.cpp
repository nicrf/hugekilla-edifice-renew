//No GPU: the backend of the dedicated server (edifice-server, CMake EDIFICE_DEDICATED),
//which has no window. The renderer runs headless there (RSetHeadless), so none of this
//is called: it only stands in for renderer_sokol.cpp or renderer_gl.cpp and their
//texture and context files (rbackend.h, textures.h, rcontext.h) at link time.

#include "rbackend.h"
#include "rcontext.h"
#include "textures.h"
#include <string.h>

void RBackendInit()
{
}

void RBackendDraw(const RState &,const RVertex *,const RCmd *,int)
{
}

void RBackendClear(const RState &,int,int)
{
}

void RBackendReadPixels(int,int,int w,int h,int,unsigned char *out)
{
	memset(out,0,(size_t)w*h*3);
}

void RBackendReadDepth(int,int,float *)
{
}

int RBackendUltraSupported()
{
	return 0;
}

void RBackendUltraMark(int,const RState &,const RUltraScene *)
{
}

unsigned TexName(int)
{
	return 0;
}

void TexBackendBind(int)
{
}

void TexUpload(int,int,int,int,const void *,TexFilter)
{
}

void TexShutdown()
{
}

unsigned RWindowFlags()
{
	return 0;
}

int RCreateContext(SDL_Window *)
{
	return 0;
}

void RDestroyContext()
{
}

void RDrawableSize(int *w,int *h)
{
	*w=*h=0;
}

void RPresent()
{
}

void RSetSwapInterval(int)
{
}

const char *RBackendName()
{
	return "no renderer";
}
