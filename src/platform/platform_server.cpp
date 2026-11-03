//The platform layer of edifice-server (CMake EDIFICE_DEDICATED), in place of
//platform_sdl.cpp, system_sdl.cpp, gamepad.cpp and touch.cpp: main() runs the
//dedicated server (dedicated.cpp), the clocks are the C++ standard library's, the
//folders found without SDL, and the window, input and gamepad calls do nothing.
//No SDL at all: the program builds and runs where there is no display or sound.

#include "platform.h"
#include "hktime.h"
#include "hkkeys.h"
#include "fileio.h"
#include "gamepad.h"
#include "touch.h"
#include "dedicated.h"
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

int lockedcursor;
Vector mousepos;
char status[10][200];
int timetotick = 10;

// Clocks ----------------------------------------------------------------------

static const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

uint32_t HKTicks()
{
	return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
}

void HKFrameDrawn()
{
}

int64_t HKPerfCounter()
{
	return (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t HKPerfFrequency()
{
	return 1000000000;
}

uint32_t HKPerfMillis()
{
	return (uint32_t)(uint64_t)(HKPerfCounter() / 1000000);
}

void HKSleep(uint32_t ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void HKMessageBox(const char *caption, const char *text, int)
{
	fprintf(stderr, "%s: %s\n", caption ? caption : "", text ? text : "");
}

// No window, no input -----------------------------------------------------------

static unsigned char keydown[256];

void HKSetKeyDown(int key, int down)
{
	if (key >= 0 && key < 256) keydown[key] = (unsigned char)(down != 0);
}

int HKKeyDown(int key)
{
	return key >= 0 && key < 256 && keydown[key];
}

void HKQuit() { DedicatedQuit(); }
void HKWindowSize(int *w, int *h) { *w = *h = 0; }
void HKSaveFrame(const char *) {}
void HKSwapWindow() {}
float HKPixelScale() { return 1; }
void SetScreenMode() {}
void HKSetFullscreen(int) {}
int HKGetFullscreen() { return 0; }
void HKSetVSync(int) {}
int HKGetVSync() { return 0; }
void GetMouseChange(int &x, int &y) { x = y = 0; }
void LockCursor(int) {}
int HKInGame() { return 0; }
void HKInjectKey(int, int) {}
void HKInjectClick(int, int) {}
void HKInjectLook(int, int) {}

PadSettings padsettings = {1, .15f, 1.5f, 0, 1};
void PadInit() {}
void PadShutdown() {}
int PadHandleEvent(const SDL_Event &) { return 0; }
void PadFrame() {}
void PadLoadConfig() {}
int PadConnected() { return 0; }
void PadNextWeapon(int) {}
const char *PadName() { return ""; }
int PadActionCount() { return 0; }
const char *PadActionLabel(int) { return ""; }
const char *PadActionInput(int) { return ""; }
void PadCapture(int) {}
int PadCapturing() { return -1; }
int PadIniValues(const char **, char (*)[32], int) { return 0; }

TouchSettings touchsettings = {0, 1};
int TouchHandleEvent(const SDL_Event &) { return 0; }
void TouchFrame() {}
void DrawTouchControls() {}
int TouchShown() { return 0; }
void TouchHide() {}
void TouchLoadConfig() {}
int TouchIniValues(const char **, char (*)[32], int) { return 0; }

// Folders -------------------------------------------------------------------------

// where the program is, for data/ next to it
static std::filesystem::path ExeDir()
{
	std::error_code err;
#ifdef _WIN32
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(0, path, MAX_PATH);
	if (n > 0 && n < MAX_PATH) return std::filesystem::path(path).parent_path();
#elif defined(__APPLE__)
	char path[4096];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size) == 0) return std::filesystem::canonical(path, err).parent_path();
#else
	std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", err);
	if (!err) return self.parent_path();
#endif
	return std::filesystem::path();
}

// the data folder: the first argument, HUGEKILLA_DATA (both through --data), data/
// next to the program, the source tree's in Debug builds
static void EnterDataDir()
{
	std::string candidates[3];
	int n = 0;
	if (const char *env = getenv("HUGEKILLA_DATA")) candidates[n++] = env;
	std::filesystem::path exe = ExeDir();
	if (!exe.empty()) candidates[n++] = (exe / "data").u8string();
#ifdef HK_SOURCE_DATA_DIR
	candidates[n++] = HK_SOURCE_DATA_DIR;
#endif
	for (int i = 0; i < n; i++)
		if (HKFileExists((candidates[i] + "/control.ini").c_str()) && HKChangeDir(candidates[i].c_str())) return;
	if (!HKFileExists("control.ini"))
		fprintf(stderr, "Edifice data folder not found: --data <folder> (or HUGEKILLA_DATA).\n");
}

// the user folder of the game (SDL_GetPrefPath("", "Edifice") on the same system), or
// HK_USERDIR (--userdir), created if needed
static void InitUserDir()
{
	std::string dir;
	if (const char *env = getenv("HK_USERDIR"))
		dir = env;
	else
	{
#ifdef _WIN32
		const char *base = getenv("APPDATA");
		if (base) dir = std::string(base) + "\\Edifice\\";
#elif defined(__APPLE__)
		const char *home = getenv("HOME");
		if (home) dir = std::string(home) + "/Library/Application Support/Edifice/";
#else
		const char *xdg = getenv("XDG_DATA_HOME");
		const char *home = getenv("HOME");
		if (xdg && *xdg) dir = std::string(xdg) + "/Edifice/";
		else if (home) dir = std::string(home) + "/.local/share/Edifice/";
#endif
	}
	if (dir.empty()) return;   // the data folder then
	char last = dir[dir.size() - 1];
	if (last != '/' && last != '\\') dir += '/';
	std::error_code err;
	std::filesystem::create_directories(std::filesystem::u8path(dir), err);
	HKSetUserDir(dir.c_str());
}

int main(int argc, char **argv)
{
	int exitcode;
	DedicatedArgs(argc, argv, 1, &exitcode);
	if (exitcode >= 0) return exitcode;
	EnterDataDir();
	InitUserDir();
	return DedicatedRun();
}
