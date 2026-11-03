//The per-user settings folder (platform.h): where the game rewrites control.ini,
//net.ini and sv.ini, saves its maps and screenshots, never the data folder (that
//keeps the signed .app intact and survives updates). Defaults come from data/. The
//platform layer chooses the folder: SDL's pref path (platform_sdl.cpp), the
//dedicated server's (platform_server.cpp), HK_USERDIR for both.

#include "platform.h"
#include "fileio.h"
#include <stdio.h>

static char userdir[1024];

void HKSetUserDir(const char *dir)
{
	snprintf(userdir, sizeof(userdir), "%s", dir ? dir : "");
	if (!userdir[0]) return;
	const char *defaults[3] = {"control.ini", "net.ini", "sv.ini"};
	for (int i = 0; i < 3; i++)
	{
		char dst[1200];
		snprintf(dst, sizeof(dst), "%s%s", userdir, defaults[i]);
		if (HKFileExists(dst)) continue;
		// UTF-8 user folder (SDL_GetPrefPath): not for the ANSI fopen of Windows
		FILE *in = HKFileOpen(defaults[i], "rb");
		FILE *out = in ? HKFileOpen(dst, "wb") : 0;
		char buf[4096];
		size_t n;
		while (in && out && (n = fread(buf, 1, sizeof(buf), in)) > 0)
			fwrite(buf, 1, n, out);
		if (in) fclose(in);
		if (out) fclose(out);
	}
}

const char *HKUserDir()
{
	return userdir;
}

const char *UserFile(const char *name)
{
	static char paths[4][1200];
	static int next;
	if (!userdir[0]) return name;     // no user folder: data folder (working directory)
	char *p = paths[next++ & 3];
	snprintf(p, sizeof(paths[0]), "%s%s", userdir, name);
	return p;
}
