//Clocks, the held keys and message boxes on SDL2 (hktime.h, hkkeys.h, platform.h),
//in place of the Win32 calls of the original code and their emulation.

#include "hktime.h"
#include "hkkeys.h"
#include "platform.h"
#include <SDL.h>
#include <stdlib.h>

uint32_t HKTicks()
{
	return SDL_GetTicks();
}

// Debug aid: HK_FIXEDCLOCK=N makes the game clock (HKPerfCounter, HKPerfMillis) advance
// N ms per frame drawn instead of following real time, so camera, clouds and
// animations are the same from one run to the next (renderer pixel-diffs, M13)
static int fixedstep = getenv("HK_FIXEDCLOCK") ? atoi(getenv("HK_FIXEDCLOCK")) : 0;
static int64_t fixedframes;

void HKFrameDrawn()
{
	fixedframes++;
}

int64_t HKPerfCounter()
{
	if (fixedstep > 0)
		return (int64_t)(1000000 + fixedframes * fixedstep) * HKPerfFrequency() / 1000;
	return (int64_t)SDL_GetPerformanceCounter();
}

int64_t HKPerfFrequency()
{
	return (int64_t)SDL_GetPerformanceFrequency();
}

//the counter starts at boot: after 49.7 days its milliseconds no longer fit in 32
//bits, and the double -> DWORD conversion the game did was undefined from then on
uint32_t HKPerfMillis()
{
	if (fixedstep > 0)
		return (uint32_t)(1000000 + fixedframes * fixedstep);
	return (uint32_t)(uint64_t)((double)HKPerfCounter() / (double)HKPerfFrequency() * 1000);
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
