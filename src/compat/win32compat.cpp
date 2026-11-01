// SDL2 implementations of the Win32 calls declared in win32compat.h. On
// POSIX they replace the Win32 API; on Windows only the few calls that must
// go through SDL (presenting, quitting, message boxes, keyboard state).

#include "win32compat.h"
#include "gl.h"   //HK_SHOT reads the back buffer
#include <SDL.h>

#ifndef _WIN32
char *itoa(int value, char *str, int radix)
{
	char tmp[40];
	char *p = tmp;
	unsigned int v;
	int neg = 0;
	if (radix < 2 || radix > 36) { str[0] = 0; return str; }
	if (radix == 10 && value < 0) { neg = 1; v = (unsigned int)(-(int64_t)value); }
	else v = (unsigned int)value;
	do
	{
		int d = v % radix;
		*p++ = (char)(d < 10 ? '0' + d : 'a' + d - 10);
		v /= radix;
	} while (v);
	char *out = str;
	if (neg) *out++ = '-';
	while (p > tmp) *out++ = *--p;
	*out = 0;
	return str;
}

#endif // !_WIN32

// Misc --------------------------------------------------------------------

int MessageBox(HWND, LPCTSTR text, LPCTSTR caption, UINT type)
{
	fprintf(stderr, "%s: %s\n", caption ? caption : "", text ? text : "");
	SDL_ShowSimpleMessageBox((type & MB_ICONERROR) ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
		caption ? caption : "", text ? text : "", SDL_GL_GetCurrentWindow());
	return 1;
}

#ifndef _WIN32
BOOL QueryPerformanceFrequency(LARGE_INTEGER *freq)
{
	freq->QuadPart = (int64_t)SDL_GetPerformanceFrequency();
	return TRUE;
}

BOOL QueryPerformanceCounter(LARGE_INTEGER *count)
{
	count->QuadPart = (int64_t)SDL_GetPerformanceCounter();
	return TRUE;
}

DWORD GetTickCount()
{
	return SDL_GetTicks();
}

void Sleep(DWORD ms)
{
	SDL_Delay(ms);
}

#endif // !_WIN32

// Debug aid: HK_SHOT=file.ppm saves the HK_SHOT_FRAME-th frame (default 120).
static void DebugScreenshot()
{
	static int frame;
	static const char *shot = getenv("HK_SHOT");
	if (!shot) return;
	int target = getenv("HK_SHOT_FRAME") ? atoi(getenv("HK_SHOT_FRAME")) : 120;
	if (++frame != target) return;
	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	int w = vp[2], h = vp[3];
	unsigned char *px = new unsigned char[(size_t)w * h * 3];
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadBuffer(GL_BACK);
	glReadPixels(vp[0], vp[1], w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
	FILE *f = fopen(shot, "wb");
	if (f)
	{
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		for (int y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
		fclose(f);
		fprintf(stderr, "saved %s (%dx%d)\n", shot, w, h);
	}
	delete[] px;
}

BOOL SwapBuffers(HDC)
{
	DebugScreenshot();
	SDL_Window *win = SDL_GL_GetCurrentWindow();
	if (win) SDL_GL_SwapWindow(win);
	return TRUE;
}

// Keyboard ----------------------------------------------------------------

static unsigned char keydown[256];

void HKSetKeyDown(int vk, int down)
{
	if (vk >= 0 && vk < 256) keydown[vk] = (unsigned char)(down != 0);
}

short GetKeyState(int vk)
{
	if (vk < 0 || vk >= 256) return 0;
	// Same bit pattern Windows returns for a held key (callers test bit 8).
	return keydown[vk] ? (short)0xFF80 : 0;
}

short GetAsyncKeyState(int vk)
{
	return GetKeyState(vk);
}

#ifndef _WIN32
// Sockets -----------------------------------------------------------------

SOCKET WSASocket(int af, int type, int protocol, void *, int, DWORD)
{
	SOCKET s = socket(af, type, protocol);
	if (s < 0) return 0;
	int reuse = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	return s;
}

#endif // !_WIN32
