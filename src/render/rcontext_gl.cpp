//The window's OpenGL 2.1 context (rcontext.h), for the fixed-function backend
//(renderer_gl.cpp): a compatibility context through SDL, swapped by SDL.

#include "rcontext.h"
#include "renderer.h"
#include <SDL.h>
#include <stdio.h>

static SDL_Window *window;
static SDL_GLContext glcontext;

unsigned RWindowFlags()
{
	//legacy (compatibility) profile: the renderer uses fixed-function GL
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	return SDL_WINDOW_OPENGL;
}

int RCreateContext(SDL_Window *w)
{
	window = w;
	glcontext = SDL_GL_CreateContext(window);
	if (!glcontext)
	{
		fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		return 0;
	}
	SDL_GL_MakeCurrent(window, glcontext);
	return 1;
}

void RDestroyContext()
{
	if (glcontext) SDL_GL_DeleteContext(glcontext);
	glcontext = 0;
}

void RDrawableSize(int *w, int *h)
{
	*w = *h = 0;
	if (window) SDL_GL_GetDrawableSize(window, w, h);
}

void RPresent()
{
	RFlush();   //the end of the frame may still be batched
	if (window) SDL_GL_SwapWindow(window);
}

void RSetSwapInterval(int vsync)
{
	SDL_GL_SetSwapInterval(vsync);
}

const char *RBackendName()
{
	return "OpenGL";
}
