//window, GL context, input and user folder: platform_sdl.cpp (SDL2 on every platform);
//clocks: hktime.h, key codes: hkkeys.h, sockets: hksocket.h

#ifndef PLATFORMH
#define PLATFORMH

#include "hktypes.h"
#include "vector.h"

extern Vector mousepos;     //mouse over the menus, -1..1 from the window center, y up

float HKPixelScale();       //pixels per window point (2 on Retina)
void SetScreenMode();
void HKSetFullscreen(int on);
int HKGetFullscreen();
void HKSetVSync(int on);
int HKGetVSync();
void HKSwapWindow();        //presents the frame (HK_SHOT saves it first)
void HKQuit();              //ends the main loop
void HKWindowSize(int *w,int *h);   //of the GL drawable, in pixels
void GetMouseChange(int &x,int &y);
void LockCursor(int lock);
const char *UserFile(const char *name);   //name in the per-user settings folder
void HKMessageBox(const char *caption,const char *text,int error);   //also on stderr (system_sdl.cpp)

#endif
