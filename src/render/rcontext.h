//The window's GPU side, for the platform layer (platform_sdl.cpp): what the window
//needs to be created with, the device or context on it, the drawable size and the
//presentation. One implementation per renderer backend: rcontext_gl.cpp (an OpenGL 2.1
//context through SDL), sokol_native.cpp (Metal, D3D11 or a GL 4.1 core context, for
//sokol_gfx).

#ifndef RCONTEXTH
#define RCONTEXTH

struct SDL_Window;

unsigned RWindowFlags();              //SDL_WINDOW_* the backend needs at SDL_CreateWindow
int RCreateContext(SDL_Window *window);   //0 on failure (message on stderr); RInit next
void RDestroyContext();               //after TexShutdown
void RDrawableSize(int *w,int *h);    //in pixels (2x the window on Retina)
void RPresent();                      //shows the frame drawn since the last one
void RSetSwapInterval(int vsync);
const char *RBackendName();           //for the console: "OpenGL", "sokol_gfx Metal"...

#endif
