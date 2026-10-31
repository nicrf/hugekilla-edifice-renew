// Minimal Win32 emulation so the original sources build on macOS (and other
// POSIX systems) on top of SDL2 + legacy OpenGL. Only what the game uses.

#ifndef WIN32COMPAT_H
#define WIN32COMPAT_H

#ifdef _WIN32

// Windows: the real Win32 API is available. Only the calls that must go
// through SDL (the window is an SDL_Window, not an HWND) are redirected.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <stdint.h>
#include <GL/gl.h>
#include <GL/glu.h>

#undef MessageBox
#define SwapBuffers HK_SwapBuffers
#define DestroyWindow HK_DestroyWindow
#define GetClientRect HK_GetClientRect
#define GetKeyState HK_GetKeyState
#define GetAsyncKeyState HK_GetAsyncKeyState
#define MessageBox HK_MessageBox
#define BeginPaint HK_BeginPaint
#define EndPaint HK_EndPaint

char *HKPath(const char *in, char *out, size_t outsize);
BOOL SwapBuffers(HDC dc);
BOOL DestroyWindow(HWND wnd);
BOOL GetClientRect(HWND wnd, RECT *rect);
int MessageBox(HWND wnd, LPCTSTR text, LPCTSTR caption, UINT type);
short GetKeyState(int vk);
short GetAsyncKeyState(int vk);
void HKSetKeyDown(int vk, int down);
static inline HDC BeginPaint(HWND, PAINTSTRUCT *ps) { ps->hdc = 0; return 0; }
static inline BOOL EndPaint(HWND, const PAINTSTRUCT *) { return TRUE; }

typedef ULONG in_addr_t;
#ifndef R_OK
#define R_OK 4
#endif
#define access _access
#define chdir _chdir

#else // POSIX

#define GL_SILENCE_DEPRECATION

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

// Basic types. DWORD must stay 32 bits: it is written to save files and sent
// over the network inside packed structs.
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef int BOOL;
typedef unsigned int UINT;
typedef int32_t LONG;
typedef char TCHAR;
typedef char *LPSTR;
typedef const char *LPCTSTR;
typedef const char *LPCSTR;
typedef void *HANDLE;
typedef void *HWND;
typedef void *HDC;
typedef void *HINSTANCE;
typedef void *HGLRC;
typedef void *HBRUSH;
typedef intptr_t LRESULT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;

#define CALLBACK
#define APIENTRY
#define TRUE 1
#define FALSE 0

typedef struct { LONG x, y; } POINT;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef union { struct { DWORD LowPart; LONG HighPart; } u; int64_t QuadPart; } LARGE_INTEGER;

#define ZeroMemory(d, n) memset((d), 0, (n))
#define CopyMemory(d, s, n, ...) memcpy((d), (s), (n))
#define MoveMemory(d, s, n) memmove((d), (s), (n))
#define FillMemory(d, n, v) memset((d), (v), (n))

#define stricmp strcasecmp
#define _stricmp strcasecmp
#define strcmpi strcasecmp
#define _snprintf snprintf

char *itoa(int value, char *str, int radix);
#define _itoa itoa

// Files -------------------------------------------------------------------

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define CREATE_ALWAYS 2
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define MAX_PATH 260

// Converts a game path ("textures\\foo.bmp") into a host path in place-safe
// storage; returns out.
char *HKPath(const char *in, char *out, size_t outsize);

HANDLE CreateFile(LPCTSTR name, DWORD access, DWORD share, void *sec, DWORD disposition, DWORD flags, HANDLE templ);
BOOL ReadFile(HANDLE file, void *buf, DWORD toread, DWORD *read, void *overlapped);
BOOL WriteFile(HANDLE file, const void *buf, DWORD towrite, DWORD *written, void *overlapped);
DWORD GetFileSize(HANDLE file, DWORD *high);
BOOL CloseHandle(HANDLE file);

typedef struct
{
	DWORD dwFileAttributes;
	char cFileName[MAX_PATH];
} WIN32_FIND_DATA;

HANDLE FindFirstFile(LPCTSTR pattern, WIN32_FIND_DATA *fd);
BOOL FindNextFile(HANDLE find, WIN32_FIND_DATA *fd);
BOOL FindClose(HANDLE find);

#define MOVEFILE_REPLACE_EXISTING 1
BOOL MoveFileEx(LPCTSTR from, LPCTSTR to, DWORD flags);

// Bitmaps (LoadImage on a .bmp, read back through GetObject) --------------

#define IMAGE_BITMAP 0
#define LR_CREATEDIBSECTION 0x2000
#define LR_LOADFROMFILE 0x10

typedef struct
{
	LONG bmType;
	LONG bmWidth;
	LONG bmHeight;
	LONG bmWidthBytes;
	WORD bmPlanes;
	WORD bmBitsPixel;
	void *bmBits;
} BITMAP;

void *LoadImage(HINSTANCE inst, LPCTSTR name, UINT type, int cx, int cy, UINT flags);
int GetObject(void *obj, int size, void *out);
BOOL DeleteObject(void *obj);

// Misc --------------------------------------------------------------------

#define MB_OK 0
#define MB_ICONERROR 0x10
int MessageBox(HWND wnd, LPCTSTR text, LPCTSTR caption, UINT type);

BOOL QueryPerformanceFrequency(LARGE_INTEGER *freq);
BOOL QueryPerformanceCounter(LARGE_INTEGER *count);
DWORD GetTickCount();
void Sleep(DWORD ms);

// Present the back buffer (replaces SwapBuffers(hdc)).
BOOL SwapBuffers(HDC dc);

// Ends the SDL main loop (replaces DestroyWindow on the main window).
BOOL DestroyWindow(HWND wnd);
BOOL GetClientRect(HWND wnd, RECT *rect);

// GDI calls only reached by the dedicated-server text console: no-ops here.
typedef struct { HDC hdc; } PAINTSTRUCT;
#define TRANSPARENT 1
static inline HDC BeginPaint(HWND, PAINTSTRUCT *ps) { ps->hdc = 0; return 0; }
static inline BOOL EndPaint(HWND, PAINTSTRUCT *) { return TRUE; }
static inline int FillRect(HDC, const RECT *, HBRUSH) { return 1; }
static inline DWORD SetTextColor(HDC, DWORD) { return 0; }
static inline int SetBkMode(HDC, int) { return 0; }
static inline BOOL TextOut(HDC, int, int, LPCTSTR, int) { return TRUE; }

// Keyboard: Windows virtual-key codes, fed by the SDL event loop. ----------

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ALT VK_MENU
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B

short GetKeyState(int vk);
short GetAsyncKeyState(int vk);
void HKSetKeyDown(int vk, int down);

// Winsock -----------------------------------------------------------------

typedef int SOCKET;
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define WSA_FLAG_OVERLAPPED 0
#define MAKELONG(a, b) ((DWORD)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define MAKEWPARAM(l, h) MAKELONG(l, h)
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD)(l) >> 16))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
typedef struct { int unused; } WSADATA;

static inline int WSAStartup(WORD, WSADATA *) { return 0; }
static inline int WSACleanup() { return 0; }
SOCKET WSASocket(int af, int type, int protocol, void *info, int group, DWORD flags);
static inline int closesocket(SOCKET s) { return close(s); }

#endif // _WIN32

#endif
