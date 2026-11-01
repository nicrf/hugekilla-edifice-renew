//Clocks and sleeping (src/platform/system_sdl.cpp), in place of the Win32
//GetTickCount, QueryPerformanceCounter and Sleep.

#ifndef HKTIME_H
#define HKTIME_H

#include <stdint.h>

uint32_t HKTicks();           //milliseconds, wraps after 49 days: compare differences
int64_t HKPerfCounter();      //high resolution counter, HKPerfFrequency() per second
int64_t HKPerfFrequency();
uint32_t HKPerfMillis();      //HKPerfCounter in ms, wrapped to 32 bits like the game clock
void HKSleep(uint32_t ms);

#endif
