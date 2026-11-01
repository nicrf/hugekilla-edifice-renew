//window, GL context, input and user folder: platform_sdl.cpp (with HK_WIN32_NATIVE,
//never built, the original Win32 code of hugekilla.cpp defines them instead)

#ifndef PLATFORMH
#define PLATFORMH

#include "win32compat.h"
#include "vector.h"

extern HINSTANCE hInst;
extern HWND hWnd;
extern HDC hdc;
extern HBRUSH backbrush;
extern Vector mousepos;     //mouse over the menus, -1..1 from the window center, y up

HWND GetHWND();
HDC GetGlHDC();
float HKPixelScale();       //pixels per window point (2 on Retina)
void SetScreenMode();
void HKSetFullscreen(int on);
int HKGetFullscreen();
void HKSetVSync(int on);
int HKGetVSync();
void GetMouseChange(int &x,int &y);
void LockCursor(int lock);
const char *UserFile(const char *name);   //name in the per-user settings folder

#endif
