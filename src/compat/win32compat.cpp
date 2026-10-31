// POSIX/SDL2 implementations of the Win32 calls declared in win32compat.h.

#ifndef _WIN32

#include "win32compat.h"
#include <SDL.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>

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

// Converts a Win32 path ('\\' separators) to a POSIX one. The data was made
// on Windows, where file names ignore case ("land.ini" vs "Land.ini",
// "wall3.BMP"); when the exact path does not exist, each component is matched
// case-insensitively against the directory listing (first match wins).
// Components that match nothing (new files, wildcard patterns) are kept as is.
char *HKPath(const char *in, char *out, size_t outsize)
{
	size_t i = 0;
	for (; in[i] && i + 1 < outsize; i++)
		out[i] = in[i] == '\\' ? '/' : in[i];
	out[i] = 0;

	struct stat st;
	if (!out[0] || stat(out, &st) == 0) return out;

	char *p = out;
	if (*p == '/') p++;
	while (*p)
	{
		char *end = strchr(p, '/');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		char dirpath[1024];
		if (p == out) strcpy(dirpath, ".");
		else if (p == out + 1) strcpy(dirpath, "/");
		else
		{
			size_t n = (size_t)(p - out - 1);     // parent, without the trailing '/'
			if (n >= sizeof(dirpath)) break;
			memcpy(dirpath, out, n);
			dirpath[n] = 0;
		}
		int matched = 0;
		if (DIR *dir = opendir(dirpath))
		{
			struct dirent *e;
			while ((e = readdir(dir)))
				if (strlen(e->d_name) == len && !strncasecmp(e->d_name, p, len))
				{
					memcpy(p, e->d_name, len);
					matched = 1;
					break;
				}
			closedir(dir);
		}
		if (!end || !matched) break;              // no match: the rest cannot exist
		p = end + 1;
	}
	return out;
}

// Files -------------------------------------------------------------------

HANDLE CreateFile(LPCTSTR name, DWORD access, DWORD, void *, DWORD disposition, DWORD, HANDLE)
{
	char path[1024];
	HKPath(name, path, sizeof(path));
	const char *mode = "rb";
	if (access & GENERIC_WRITE)
	{
		// OPEN_ALWAYS keeps existing content; the game always rewrites from 0.
		if (disposition == OPEN_EXISTING) mode = "r+b";
		else if (disposition == CREATE_ALWAYS) mode = "wb";
		else
		{
			FILE *f = fopen(path, "r+b");
			if (!f) f = fopen(path, "w+b");
			return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
		}
	}
	FILE *f = fopen(path, mode);
	return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}

BOOL ReadFile(HANDLE file, void *buf, DWORD toread, DWORD *read, void *)
{
	if (!file || file == INVALID_HANDLE_VALUE) { if (read) *read = 0; return FALSE; }
	size_t n = fread(buf, 1, toread, (FILE *)file);
	if (read) *read = (DWORD)n;
	return TRUE;
}

BOOL WriteFile(HANDLE file, const void *buf, DWORD towrite, DWORD *written, void *)
{
	if (!file || file == INVALID_HANDLE_VALUE) { if (written) *written = 0; return FALSE; }
	size_t n = fwrite(buf, 1, towrite, (FILE *)file);
	if (written) *written = (DWORD)n;
	return n == towrite;
}

DWORD GetFileSize(HANDLE file, DWORD *high)
{
	if (high) *high = 0;
	if (!file || file == INVALID_HANDLE_VALUE) return (DWORD)-1;
	struct stat st;
	if (fstat(fileno((FILE *)file), &st)) return (DWORD)-1;
	return (DWORD)st.st_size;
}

BOOL CloseHandle(HANDLE file)
{
	if (!file || file == INVALID_HANDLE_VALUE) return FALSE;
	fclose((FILE *)file);
	return TRUE;
}

struct FindState
{
	DIR *dir;
	char dirpath[1024];
	char pattern[256];
};

static BOOL FindFill(FindState *fs, WIN32_FIND_DATA *fd)
{
	struct dirent *ent;
	while ((ent = readdir(fs->dir)))
	{
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
		if (fnmatch(fs->pattern, ent->d_name, FNM_CASEFOLD)) continue;
		char full[1400];
		snprintf(full, sizeof(full), "%s/%s", fs->dirpath, ent->d_name);
		struct stat st;
		int isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
		fd->dwFileAttributes = isdir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
		strncpy(fd->cFileName, ent->d_name, MAX_PATH - 1);
		fd->cFileName[MAX_PATH - 1] = 0;
		return TRUE;
	}
	return FALSE;
}

HANDLE FindFirstFile(LPCTSTR pattern, WIN32_FIND_DATA *fd)
{
	char path[1024];
	HKPath(pattern, path, sizeof(path));
	FindState *fs = new FindState;
	char *slash = strrchr(path, '/');
	if (slash)
	{
		*slash = 0;
		strcpy(fs->dirpath, path[0] ? path : "/");
		strncpy(fs->pattern, slash + 1, sizeof(fs->pattern) - 1);
	}
	else
	{
		strcpy(fs->dirpath, ".");
		strncpy(fs->pattern, path, sizeof(fs->pattern) - 1);
	}
	fs->pattern[sizeof(fs->pattern) - 1] = 0;
	fs->dir = opendir(fs->dirpath);
	if (!fs->dir || !FindFill(fs, fd))
	{
		if (fs->dir) closedir(fs->dir);
		delete fs;
		return INVALID_HANDLE_VALUE;
	}
	return (HANDLE)fs;
}

BOOL FindNextFile(HANDLE find, WIN32_FIND_DATA *fd)
{
	if (!find || find == INVALID_HANDLE_VALUE) return FALSE;
	FindState *fs = (FindState *)find;
	if (!fs->dir) return FALSE;
	if (FindFill(fs, fd)) return TRUE;
	// The game never calls FindClose, so release the directory once exhausted.
	closedir(fs->dir);
	fs->dir = 0;
	return FALSE;
}

BOOL FindClose(HANDLE find)
{
	if (!find || find == INVALID_HANDLE_VALUE) return FALSE;
	FindState *fs = (FindState *)find;
	if (fs->dir) closedir(fs->dir);
	delete fs;
	return TRUE;
}

BOOL MoveFileEx(LPCTSTR from, LPCTSTR to, DWORD)
{
	char a[1024], b[1024];
	HKPath(from, a, sizeof(a));
	HKPath(to, b, sizeof(b));
	return rename(a, b) == 0;
}

// Bitmaps -----------------------------------------------------------------

// Returns a BITMAP laid out like a Win32 DIB section: bottom-up rows, BGR,
// each row padded to 4 bytes.
void *LoadImage(HINSTANCE, LPCTSTR name, UINT, int, int, UINT)
{
	HANDLE file = CreateFile(name, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
	if (file == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(file, 0);
	if (size < 54 || size == (DWORD)-1) { CloseHandle(file); return 0; }
	unsigned char *data = new unsigned char[size];
	DWORD read;
	ReadFile(file, data, size, &read, 0);
	CloseHandle(file);
	if (read != size || data[0] != 'B' || data[1] != 'M') { delete[] data; return 0; }

	uint32_t offbits, compression;
	int32_t width, height;
	uint16_t bpp;
	memcpy(&offbits, data + 10, 4);
	memcpy(&width, data + 18, 4);
	memcpy(&height, data + 22, 4);
	memcpy(&bpp, data + 28, 2);
	memcpy(&compression, data + 30, 4);
	int topdown = height < 0;
	if (topdown) height = -height;
	if (compression != 0 || width <= 0 || height <= 0 || (bpp != 8 && bpp != 24 && bpp != 32))
	{
		delete[] data;
		return 0;
	}
	LONG stride = ((width * bpp + 31) / 32) * 4;
	if ((size_t)offbits + (size_t)stride * height > size) { delete[] data; return 0; }

	BITMAP *bm = (BITMAP *)malloc(sizeof(BITMAP) + (size_t)stride * height);
	bm->bmType = 0;
	bm->bmWidth = width;
	bm->bmHeight = height;
	bm->bmWidthBytes = stride;
	bm->bmPlanes = 1;
	bm->bmBitsPixel = bpp;
	bm->bmBits = bm + 1;
	unsigned char *dst = (unsigned char *)bm->bmBits;
	for (int y = 0; y < height; y++)
	{
		int srcrow = topdown ? height - 1 - y : y;
		memcpy(dst + (size_t)y * stride, data + offbits + (size_t)srcrow * stride, stride);
	}
	delete[] data;
	return bm;
}

int GetObject(void *obj, int size, void *out)
{
	if (!obj || size < (int)sizeof(BITMAP)) return 0;
	memcpy(out, obj, sizeof(BITMAP));
	return sizeof(BITMAP);
}

BOOL DeleteObject(void *obj)
{
	free(obj);
	return TRUE;
}

// Misc --------------------------------------------------------------------

int MessageBox(HWND, LPCTSTR text, LPCTSTR caption, UINT type)
{
	fprintf(stderr, "%s: %s\n", caption ? caption : "", text ? text : "");
	SDL_ShowSimpleMessageBox((type & MB_ICONERROR) ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
		caption ? caption : "", text ? text : "", SDL_GL_GetCurrentWindow());
	return 1;
}

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
