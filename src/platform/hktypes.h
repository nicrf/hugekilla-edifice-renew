//The integer names of the original Win32 code, and the C library its sources expect
//from their common header (StdAfx.h, then win32compat.h).
//DWORD, WORD and BYTE stay: DWORD is 32 bits in the packed network structs and the
//save files, and renaming their ~160 uses to uint32_t... would only be noise.

#ifndef HKTYPES_H
#define HKTYPES_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
//the very types of <windows.h> (an identical typedef may be repeated), which the
//socket code includes there
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>   //strcasecmp
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;
#endif

static_assert(sizeof(DWORD) == 4 && sizeof(WORD) == 2, "DWORD and WORD are sent over the network");

#endif
