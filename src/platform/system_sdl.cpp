//Clocks, the held keys and message boxes on SDL2 (hktime.h, hkkeys.h, platform.h),
//in place of the Win32 calls of the original code and their emulation.

#include "hktime.h"
#include "hkkeys.h"
#include "platform.h"
#include <SDL.h>

uint32_t HKTicks()
{
	return SDL_GetTicks();
}

int64_t HKPerfCounter()
{
	return (int64_t)SDL_GetPerformanceCounter();
}

int64_t HKPerfFrequency()
{
	return (int64_t)SDL_GetPerformanceFrequency();
}

void HKSleep(uint32_t ms)
{
	SDL_Delay(ms);
}

void HKMessageBox(const char *caption, const char *text, int error)
{
	fprintf(stderr, "%s: %s\n", caption ? caption : "", text ? text : "");
	SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
		caption ? caption : "", text ? text : "", SDL_GL_GetCurrentWindow());
}

// Keyboard ----------------------------------------------------------------

static unsigned char keydown[256];

void HKSetKeyDown(int key, int down)
{
	if (key >= 0 && key < 256) keydown[key] = (unsigned char)(down != 0);
}

int HKKeyDown(int key)
{
	return key >= 0 && key < 256 && keydown[key];
}
