// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#if !defined(AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_)
#define AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers


// Windows Header Files:
#include <windows.h>
#include <winuser.h>
// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <stdint.h>
#include "win32compat.h"	// SDL redirects (Windows) / Win32 emulation (POSIX)
#ifdef _WIN32
#include <GL/gl.h>
#include <GL/glu.h>
#else
#include "gl/gl.h"		// src/compat shim (GL comes from win32compat.h)
#include "gl/glu.h"
#endif

// Local Header Files
const char *UserFile(const char *name);	// per-user settings folder (platform_sdl.cpp)
FILE *HKFileOpen(const char *path, const char *mode);	// fopen with a UTF-8 path, also on Windows
int HKMakeDir(const char *path);	// 1 if the folder exists or was created
int HKListDir(const char *dir, char (*names)[64], int max);	// file names in a folder (names < 64)
#include "math.h"

#include "cfgfile.h"
#include "redrawing.h"
#include "time.h"
//#include "gl/gl.h"
#include "vector.h"
#include "hugekilla.h"
#include "playing.h"
#include "gameplay.h"
// TODO: reference additional headers your program requires here

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_)
