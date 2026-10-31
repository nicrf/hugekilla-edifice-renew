// SDL2 replacement for the Win32 parts of hugekilla.cpp: window, OpenGL
// context, input and main loop. Input is translated to Windows virtual-key
// codes so the rest of the game sees the same events as before.

#ifndef HK_WIN32_NATIVE

#include "StdAfx.h"
#include "land.h"
#include "redrawing.h"
#include "vector.h"
#include "playing.h"
#include "multiplay.h"
#include "sound.h"
#include "ui.h"
#include <SDL.h>

extern GraphicsPrefs graphicsprefs;
extern DWORD curtime;
extern int consolevisible;

HINSTANCE hInst;
HWND hWnd;
HDC hdc;
HBRUSH backbrush;
int lockedcursor;
Vector mousepos;
char status[10][200];
int timetotick = 10;

static SDL_Window *window;
static SDL_GLContext glcontext;
static int quitrequested;
static int fullscreen;
static int vsync = 1;
static int mousedx, mousedy;

HWND GetHWND()
{
	return hWnd;
}

HDC GetGlHDC()
{
	return hdc;
}

BOOL DestroyWindow(HWND)
{
	quitrequested = 1;
	return TRUE;
}

BOOL GetClientRect(HWND, RECT *rect)
{
	int w = 0, h = 0;
	if (window) SDL_GL_GetDrawableSize(window, &w, &h);
	rect->left = 0;
	rect->top = 0;
	rect->right = w;
	rect->bottom = h;
	return TRUE;
}

static void UpdateViewport()
{
	int w, h;
	SDL_GL_GetDrawableSize(window, &w, &h);
	glViewport(0, 0, w, h);
}

void SetScreenMode()
{
	SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	UpdateViewport();
}

void HKSetFullscreen(int on)
{
	fullscreen = on != 0;
	SetScreenMode();
}

int HKGetFullscreen()
{
	return fullscreen;
}

void HKSetVSync(int on)
{
	vsync = on != 0;
	SDL_GL_SetSwapInterval(vsync);
}

int HKGetVSync()
{
	return vsync;
}

void GetMouseChange(int &x, int &y)
{
	if (lockedcursor == 0)
	{
		x = 0;
		y = 0;
		return;
	}
	x = mousedx;
	y = mousedy;
	mousedx = 0;
	mousedy = 0;
}

void LockCursor(int lock)
{
	if (lock == 0)
	{
		if (lockedcursor == 1)
		{
			SDL_SetRelativeMouseMode(SDL_FALSE);
			lockedcursor = 0;
		}
	}
	else
	{
		if (lockedcursor == 0)
		{
			lockedcursor = 1;
			mousedx = 0;
			mousedy = 0;
			SDL_SetRelativeMouseMode(SDL_TRUE);
		}
	}
}

// Windows virtual-key code for an SDL key. Letters follow the active layout
// (as VK codes do on Windows); everything else follows the physical key, so
// the digit row still gives '1'..'9' on AZERTY keyboards, as on Windows.
static int KeyToVK(const SDL_Keysym &ks)
{
	if (ks.sym >= SDLK_a && ks.sym <= SDLK_z) return 'A' + (ks.sym - SDLK_a);
	SDL_Scancode sc = ks.scancode;
	if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
	if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
	if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return VK_F1 + (sc - SDL_SCANCODE_F1);
	if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return VK_NUMPAD0 + 1 + (sc - SDL_SCANCODE_KP_1);
	switch (sc)
	{
	case SDL_SCANCODE_0: return '0';
	case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
	case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
	case SDL_SCANCODE_BACKSPACE: return VK_BACK;
	case SDL_SCANCODE_TAB: return VK_TAB;
	case SDL_SCANCODE_SPACE: return VK_SPACE;
	case SDL_SCANCODE_LSHIFT:
	case SDL_SCANCODE_RSHIFT: return VK_SHIFT;
	case SDL_SCANCODE_LCTRL:
	case SDL_SCANCODE_RCTRL:
	case SDL_SCANCODE_LGUI:
	case SDL_SCANCODE_RGUI: return VK_CONTROL;
	case SDL_SCANCODE_LALT:
	case SDL_SCANCODE_RALT: return VK_MENU;
	case SDL_SCANCODE_UP: return VK_UP;
	case SDL_SCANCODE_DOWN: return VK_DOWN;
	case SDL_SCANCODE_LEFT: return VK_LEFT;
	case SDL_SCANCODE_RIGHT: return VK_RIGHT;
	case SDL_SCANCODE_INSERT: return VK_INSERT;
	case SDL_SCANCODE_DELETE: return VK_DELETE;
	case SDL_SCANCODE_HOME: return VK_HOME;
	case SDL_SCANCODE_END: return VK_END;
	case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
	case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
	case SDL_SCANCODE_CAPSLOCK: return VK_CAPITAL;
	// OEM keys (US layout positions)
	case SDL_SCANCODE_SEMICOLON: return 186;
	case SDL_SCANCODE_EQUALS: return 187;
	case SDL_SCANCODE_COMMA: return 188;
	case SDL_SCANCODE_MINUS: return 189;
	case SDL_SCANCODE_PERIOD: return 190;
	case SDL_SCANCODE_SLASH: return 191;
	case SDL_SCANCODE_GRAVE:
	case SDL_SCANCODE_NONUSBACKSLASH: return 192;  // console key ('`', or '<'/'@' on ISO Macs)
	case SDL_SCANCODE_LEFTBRACKET: return 219;
	case SDL_SCANCODE_BACKSLASH: return 220;
	case SDL_SCANCODE_RIGHTBRACKET: return 221;
	case SDL_SCANCODE_APOSTROPHE: return 222;
	default: return 0;
	}
}

static void KeyDown(int vk, int repeat)
{
	DWORD flags = repeat ? (1u << 30) : 0;
	if (OptionsHandleKey(vk) || ServerSetupHandleKey(vk) || BuildMenuHandleKey(vk))
		return;
	if (vk == VK_ESCAPE && CloseCredits())
		return;
	if (vk == VK_ESCAPE && !GetLocalPlayer()->talking)
	{
		if (lockedcursor == 1)
			LockCursor(0);
		else if (GetLocalPlayer()->playstate != 0)
			LockCursor(1);
		return;
	}
	if (vk == VK_F6 && !repeat)
	{
		fullscreen = 1 - fullscreen;
		SetScreenMode();
	}
	HandleKey(vk, flags);
}

static void HandleEvent(const SDL_Event &ev)
{
	switch (ev.type)
	{
	case SDL_QUIT:
		quitrequested = 1;
		break;
	case SDL_WINDOWEVENT:
		if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			UpdateViewport();
		if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST && lockedcursor == 1 && !getenv("HK_SHOT"))
			LockCursor(0);
		break;
	case SDL_KEYDOWN:
	{
		if (HandleJoinScreenKey(ev.key.keysym.sym)) break;
		int vk = KeyToVK(ev.key.keysym);
		if (!vk) break;
		HKSetKeyDown(vk, 1);
		KeyDown(vk, ev.key.repeat);
		break;
	}
	case SDL_KEYUP:
	{
		int vk = KeyToVK(ev.key.keysym);
		if (!vk) break;
		HKSetKeyDown(vk, 0);
		HandleKeyUp(vk, (1u << 30) | (1u << 31));
		break;
	}
	case SDL_TEXTINPUT:
		HandleJoinScreenText(ev.text.text);
		OptionsHandleText(ev.text.text);
		ServerSetupHandleText(ev.text.text);
		break;
	case SDL_MOUSEMOTION:
	{
		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		mousepos.x = (float)ev.motion.x / w * 2 - 1;
		mousepos.y = (float)ev.motion.y / h * -2 + 1;
		if (lockedcursor)
		{
			mousedx += ev.motion.xrel;
			mousedy += ev.motion.yrel;
		}
		break;
	}
	case SDL_MOUSEBUTTONDOWN:
		if (ev.button.button == SDL_BUTTON_LEFT)
		{
			HKSetKeyDown(VK_LBUTTON, 1);
			if (IsBuildMenuOpen())
			{
				BuildMenuClick();
				break;
			}
			if (GetLocalPlayer()->playstate == 0 || lockedcursor == 0)
			{
				MenuMouseDown();
				break;
			}
			HandleKey(VK_LBUTTON, 0);
		}
		else if (ev.button.button == SDL_BUTTON_RIGHT)
		{
			HKSetKeyDown(VK_RBUTTON, 1);
			if (OptionsHandleKey(VK_RBUTTON) || BuildMenuHandleKey(VK_RBUTTON)) break;
			HandleKey(VK_RBUTTON, 0);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		if (ev.button.button == SDL_BUTTON_LEFT)
		{
			HKSetKeyDown(VK_LBUTTON, 0);
			HandleKeyUp(VK_LBUTTON, 0);
		}
		else if (ev.button.button == SDL_BUTTON_RIGHT)
		{
			HKSetKeyDown(VK_RBUTTON, 0);
			HandleKeyUp(VK_RBUTTON, 0);
		}
		break;
	case SDL_MOUSEWHEEL:
	{
		int ud = ev.wheel.y;
		if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ud = -ud;
		if (IsBuildMenuOpen())
		{
			BuildMenuWheel(ud);
			break;
		}
		if (ud < 0) SwitchGun(50);
		if (ud > 0) SwitchGun(51);
		break;
	}
	}
}

// Game data (ini files, textures, sounds) is loaded with relative paths, so
// run from the game data folder (data/ in the source tree).
static void EnterDataDir(int argc, char **argv)
{
	const char *candidates[4] = {0, 0, 0, 0};
	int n = 0;
	if (argc > 1) candidates[n++] = argv[1];
	if (getenv("HUGEKILLA_DATA")) candidates[n++] = getenv("HUGEKILLA_DATA");
	char *base = SDL_GetBasePath();
	static char bundled[2048];
	if (base)
	{
		// Inside an .app bundle: Contents/Resources/data; elsewhere data/ next to the executable
		snprintf(bundled, sizeof(bundled), "%s/data", base);
		candidates[n++] = bundled;
		SDL_free(base);
	}
#ifdef HK_SOURCE_DATA_DIR
	candidates[n++] = HK_SOURCE_DATA_DIR;
#endif
	for (int i = 0; i < n; i++)
	{
		char probe[2100];
		snprintf(probe, sizeof(probe), "%s/control.ini", candidates[i]);
		if (access(probe, R_OK) == 0 && chdir(candidates[i]) == 0) return;
	}
	if (access("control.ini", R_OK) != 0)
		fprintf(stderr, "Edifice data folder not found; pass its path as the first argument.\n");
}

int main(int argc, char **argv)
{
	EnterDataDir(argc, argv);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	CfgFile controls;
	controls.Load("control.ini");
	fullscreen = controls.GetValuei("changeres") != 0;
	int width = controls.GetValuei("screenwidth");
	int height = controls.GetValuei("screenheight");
	if (width <= 0 || height <= 0)
	{
		width = 1024;
		height = 700;
	}
	graphicsprefs.stereoseparation = controls.GetValuef("stereoseparation");

	srand((unsigned)time(NULL));

	// Legacy (compatibility) profile: the renderer uses fixed-function GL.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	window = SDL_CreateWindow("Edifice", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
		(fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
	if (!window)
	{
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}
	glcontext = SDL_GL_CreateContext(window);
	if (!glcontext)
	{
		fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_MakeCurrent(window, glcontext);
	SDL_GL_SetSwapInterval(vsync);
	hWnd = (HWND)window;
	hdc = (HDC)window;
	UpdateViewport();
	SDL_StartTextInput();

	InitConsole();

	LoadTexture("textures\\fonts\\courier.pxr", 300);
	LoadTexture("textures\\startup.bmp", 800);

	consolevisible = 1;
	InitSound(hWnd);
	LoadSounds();
	LoadHighTextures();

	InitSocks();

	for (int i = 0; i < MAXPLAYERS; i++)
	{
		GetPlayer(i)->playstate = 0;
		GetPlayer(i)->dir = Vector(1, 0, 0);
		GetPlayer(i)->tilt = Vector(1, 0, 0);
		GetPlayer(i)->fwd = Vector(1, 0, 0);
		GetPlayer(i)->gunup = Vector(0, 1, 0);
		GetPlayer(i)->fzoom = 0;
	}
	LoadControls();
	LoadOptions();
	RedrawSplash(1, 0, 1);
	PlaySound(0, 1);

	// Debug aid: HK_AUTOSTART=1 starts a local server right away.
	if (getenv("HK_AUTOSTART")) StartServer();
	// Debug aid: HK_MENU=options|credits|join opens that screen at startup.
	if (const char *m = getenv("HK_MENU"))
	{
		if (!strcmp(m, "options")) OpenOptions();
		if (!strcmp(m, "options2")) { OpenOptions(); mousepos = Vector(-.2f, .72f, 0); OptionsClick(); }
		if (!strcmp(m, "credits")) { mousepos = Vector(0, -.92f, 0); MenuMouseDown(); }
		if (!strcmp(m, "join")) OpenJoinScreen();
		if (!strcmp(m, "server")) OpenServerSetup();
		if (!strcmp(m, "build")) { StartServer(); OpenBuildMenu(); }
		if (!strcmp(m, "scores")) { StartServer(); GetLocalPlayer()->scores = 1; }
		if (!strcmp(m, "serverstart")) { OpenServerSetup(); ServerSetupHandleKey(VK_RETURN); }
		if (!strcmp(m, "player")) { OpenOptions(); mousepos = Vector(.4f, .72f, 0); OptionsClick(); }
	}

	DWORD nexttimertick = 0;
	while (!quitrequested)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
			HandleEvent(ev);
		if (quitrequested) break;

		LARGE_INTEGER time;
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&time);
		curtime = ((double)time.QuadPart) / (double)freq.QuadPart * 1000;

		// Was a 200ms WM_TIMER: scrolls the status lines.
		if (curtime >= nexttimertick)
		{
			nexttimertick = curtime + 200;
			timetotick--;
			if (timetotick < 0)
			{
				timetotick = 5;
				for (int i = 0; i < 9; i++)
					strcpy(status[i], status[i + 1]);
			}
		}

		int redraw = 0;
		Uint32 flags = SDL_GetWindowFlags(window);
		if (((flags & SDL_WINDOW_INPUT_FOCUS) || getenv("HK_SHOT")) && !(flags & SDL_WINDOW_MINIMIZED))
			redraw = 1;
		else
			Sleep(4);
		// The build menu frees the mouse but keeps drawing the game behind it.
		if (GetLocalPlayer()->playstate == 0 || (lockedcursor == 0 && !IsBuildMenuOpen()))
		{
			if (redraw)
				DrawMenu();
			if (GetLocalPlayer()->playstate != 0)
				Playloop();
			SockLoop();
		}
		else
		{
			if (GetLocalPlayer()->playstate == 1)
			{
				Playloop();
				SockLoop();
				if (redraw) DrawConsole();
			}
			else
			{
				Playloop();
				SockLoop();
				if (redraw) Redraw();
			}
		}
	}

	CloseSocks();
	UnInitSound();
	SDL_GL_DeleteContext(glcontext);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

#endif // !HK_WIN32_NATIVE
