// The game's main(), on SDL2 everywhere: window, OpenGL context, input and main
// loop. Input is translated to the game's key codes (hkkeys.h, the Windows
// virtual-key values of the original code).


#include "gl.h"
#include "platform.h"
#include "hkkeys.h"
#include "hktime.h"
#include "fileio.h"
#include "cfgfile.h"
#include "hugekilla.h"
#include "land.h"
#include "redrawing.h"
#include "vector.h"
#include "playing.h"
#include "multiplay.h"
#include "sound.h"
#include "ui.h"
#include "voice.h"
#include <SDL.h>
#include <time.h>   //time(), the seed of rand()

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

void HKQuit()
{
	quitrequested = 1;
}

void HKWindowSize(int *w, int *h)
{
	*w = *h = 0;
	if (window) SDL_GL_GetDrawableSize(window, w, h);
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

void HKSwapWindow()
{
	DebugScreenshot();
	if (window) SDL_GL_SwapWindow(window);
}

// The window is created high-DPI aware: its size (and the mouse) is in points, the
// GL drawable in pixels (2x on Retina). Everything draws in the viewport, so the
// layout stays the same and only gets sharper.
static float pixelscale = 1;

static void UpdateViewport()
{
	int w, h, ww, wh;
	SDL_GL_GetDrawableSize(window, &w, &h);
	SDL_GetWindowSize(window, &ww, &wh);
	pixelscale = ww > 0 && w > ww ? (float)w / ww : 1;
	glViewport(0, 0, w, h);
}

// pixels per point of the window: line widths and point sizes are in pixels
float HKPixelScale()
{
	return pixelscale;
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

// The game's key code (hkkeys.h) for an SDL key. Letters follow the active layout
// (as VK codes do on Windows); everything else follows the physical key, so
// the digit row still gives '1'..'9' on AZERTY keyboards, as on Windows.
static int SDLKeyToKey(const SDL_Keysym &ks)
{
	if (ks.sym >= SDLK_a && ks.sym <= SDLK_z) return 'A' + (ks.sym - SDLK_a);
	SDL_Scancode sc = ks.scancode;
	if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
	if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
	if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return KEY_F1 + (sc - SDL_SCANCODE_F1);
	if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return KEY_NUMPAD0 + 1 + (sc - SDL_SCANCODE_KP_1);
	switch (sc)
	{
	case SDL_SCANCODE_0: return '0';
	case SDL_SCANCODE_KP_0: return KEY_NUMPAD0;
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER: return KEY_RETURN;
	case SDL_SCANCODE_ESCAPE: return KEY_ESCAPE;
	case SDL_SCANCODE_BACKSPACE: return KEY_BACK;
	case SDL_SCANCODE_TAB: return KEY_TAB;
	case SDL_SCANCODE_SPACE: return KEY_SPACE;
	case SDL_SCANCODE_LSHIFT:
	case SDL_SCANCODE_RSHIFT: return KEY_SHIFT;
	case SDL_SCANCODE_LCTRL:
	case SDL_SCANCODE_RCTRL:
	case SDL_SCANCODE_LGUI:
	case SDL_SCANCODE_RGUI: return KEY_CONTROL;
	case SDL_SCANCODE_LALT:
	case SDL_SCANCODE_RALT: return KEY_MENU;
	case SDL_SCANCODE_UP: return KEY_UP;
	case SDL_SCANCODE_DOWN: return KEY_DOWN;
	case SDL_SCANCODE_LEFT: return KEY_LEFT;
	case SDL_SCANCODE_RIGHT: return KEY_RIGHT;
	case SDL_SCANCODE_INSERT: return KEY_INSERT;
	case SDL_SCANCODE_DELETE: return KEY_DELETE;
	case SDL_SCANCODE_HOME: return KEY_HOME;
	case SDL_SCANCODE_END: return KEY_END;
	case SDL_SCANCODE_PAGEUP: return KEY_PRIOR;
	case SDL_SCANCODE_PAGEDOWN: return KEY_NEXT;
	case SDL_SCANCODE_CAPSLOCK: return KEY_CAPITAL;
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
	// Debug aid: HK_KEYLOG=1 prints every key the game receives (Windows VK code)
	if (getenv("HK_KEYLOG") && !repeat)
		fprintf(stderr, "key down: vk %d ('%c')\n", vk, vk >= 32 && vk < 127 ? vk : '?');
	if (OptionsHandleKey(vk) || ServerSetupHandleKey(vk) || BuildMenuHandleKey(vk) || SpawnScreenHandleKey(vk))
		return;
	if (vk == KEY_ESCAPE && CloseCredits())
		return;
	if (vk == KEY_ESCAPE && !GetLocalPlayer()->talking)
	{
		if (lockedcursor == 1)
			LockCursor(0);
		else if (GetLocalPlayer()->playstate != 0)
			LockCursor(1);
		return;
	}
	if (SpawnScreenBlocksKey(vk))
		return;
	if (vk == KEY_F6 && !repeat)
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
		// moved to a screen of another scale: same size in points, not in pixels
		if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)
			UpdateViewport();
		if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			VoicePushToTalk(0);   // its key-up would go to another window
		if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST && lockedcursor == 1 && !getenv("HK_SHOT"))
			LockCursor(0);
		break;
	case SDL_KEYDOWN:
	{
		if (HandleJoinScreenKey(ev.key.keysym.sym)) break;
		int vk = SDLKeyToKey(ev.key.keysym);
		if (!vk) break;
		HKSetKeyDown(vk, 1);
		KeyDown(vk, ev.key.repeat);
		break;
	}
	case SDL_KEYUP:
	{
		int vk = SDLKeyToKey(ev.key.keysym);
		if (!vk) break;
		HKSetKeyDown(vk, 0);
		HandleKeyUp(vk, (1u << 30) | (1u << 31));
		break;
	}
	case SDL_TEXTINPUT:
		HandleJoinScreenText(ev.text.text);
		OptionsHandleText(ev.text.text);
		ServerSetupHandleText(ev.text.text);
		ChatHandleText(ev.text.text);
		ConsoleHandleText(ev.text.text);
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
			HKSetKeyDown(KEY_LBUTTON, 1);
			if (IsJoinScreenOpen())
			{
				JoinScreenClick(ev.button.clicks);
				break;
			}
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
			HandleKey(KEY_LBUTTON, 0);
		}
		else if (ev.button.button == SDL_BUTTON_RIGHT)
		{
			HKSetKeyDown(KEY_RBUTTON, 1);
			if (OptionsHandleKey(KEY_RBUTTON) || BuildMenuHandleKey(KEY_RBUTTON) || SpawnScreenBlocksKey(KEY_RBUTTON)) break;
			HandleKey(KEY_RBUTTON, 0);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		if (ev.button.button == SDL_BUTTON_LEFT)
		{
			HKSetKeyDown(KEY_LBUTTON, 0);
			HandleKeyUp(KEY_LBUTTON, 0);
		}
		else if (ev.button.button == SDL_BUTTON_RIGHT)
		{
			HKSetKeyDown(KEY_RBUTTON, 0);
			HandleKeyUp(KEY_RBUTTON, 0);
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
		if (menu == BUILDING && !consolevisible)
		{
			// a trackpad or a smooth-scrolling mouse sends fractions of a notch, often
			// rounded to 0 in wheel.y: one step per whole notch of preciseY
			static float notches;
			float y = ev.wheel.preciseY;
			if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
			notches += y;
			if (notches >= 1 || notches <= -1)
			{
				BuildPreviewWheel(notches > 0 ? 1 : -1, (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI)) != 0);
				notches = 0;
			}
			break;
		}
		if (consolevisible && GetLocalPlayer()->playstate != 0)
		{
			ConsoleScroll(ud * 3);
			break;
		}
		if (IsSpawnScreenOpen()) break;   // no weapon change before playing
		if (ud < 0) SwitchGun(50);
		if (ud > 0) SwitchGun(51);
		break;
	}
	}
}

// Game data (ini files, textures, sounds) is loaded with relative paths, so
// run from the game data folder (data/ in the source tree).
static char userdir[1024];

// Settings the game rewrites (control.ini, net.ini, sv.ini) and screenshots live
// in the per-user folder (SDL_GetPrefPath), never in the data folder: that keeps
// the signed .app intact and survives updates. Defaults come from data/.
static void InitUserDir()
{
	// Debug aid: HK_USERDIR=<folder/> keeps test runs away from the real settings
	if (const char *dir = getenv("HK_USERDIR"))
		snprintf(userdir, sizeof(userdir), "%s", dir);
	else
	{
		char *pref = SDL_GetPrefPath("", "Edifice");
		if (!pref)
			return;                       // fall back to the data folder
		snprintf(userdir, sizeof(userdir), "%s", pref);
		SDL_free(pref);
	}
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

const char *UserFile(const char *name)
{
	static char paths[4][1200];
	static int next;
	if (!userdir[0]) return name;     // no user folder: data folder (working directory)
	char *p = paths[next++ & 3];
	snprintf(p, sizeof(paths[0]), "%s%s", userdir, name);
	return p;
}

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
		if (HKFileExists(probe) && HKChangeDir(candidates[i])) return;
	}
	if (!HKFileExists("control.ini"))
		fprintf(stderr, "Edifice data folder not found; pass its path as the first argument.\n");
}

int main(int argc, char **argv)
{
	EnterDataDir(argc, argv);

	// Windows: per-monitor DPI aware, with window sizes in scaled points like on macOS,
	// instead of a blurry bitmap stretched by the system (before SDL_Init)
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	InitUserDir();

	CfgFile controls;
	controls.Load(UserFile("control.ini"));
	fullscreen = controls.GetValuei("changeres") != 0;
	int width = controls.GetValuei("screenwidth");
	int height = controls.GetValuei("screenheight");
	if (width <= 0 || height <= 0)
	{
		width = 1024;
		height = 700;
	}
	graphicsprefs.stereoseparation = controls.GetValuef("stereoseparation");

	// Debug aid: HK_SEED=N makes the world (land, sky, spawn) the same every run
	srand(getenv("HK_SEED") ? (unsigned)atoi(getenv("HK_SEED")) : (unsigned)time(NULL));

	// Legacy (compatibility) profile: the renderer uses fixed-function GL.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	window = SDL_CreateWindow("Edifice", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
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
	// HK_SHOT captures: no vsync, whose swap waits forever while the display sleeps (macOS)
	if (getenv("HK_SHOT")) vsync = 0;
	SDL_GL_SetSwapInterval(vsync);
	UpdateViewport();
	SDL_StartTextInput();

	InitConsole();
	{
		char version[100];
		snprintf(version, sizeof(version), "Edifice %s (protocol %d)", EDIFICE_VERSION_STRING, EDIFICE_PROTOCOL_VERSION);
		ConsoleDisplayText(version);
	}

	LoadStartupTextures();

	consolevisible = 1;
	InitSound();
	VoiceInit();
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
	RedrawSplash(TEX_SPLASH, 0, 1);
	PlaySound(0, 1);

	// Debug aid: HK_AUTOSTART=1 starts a local server right away.
	if (getenv("HK_AUTOSTART"))
	{
		// HK_MAPFILE=<path to a .map>: the server starts with this map instead of a random land
		char err[300];
		const char *map = getenv("HK_MAPFILE");
		if (!map || !LoadMapPath(map, err, sizeof(err)))
		{
			if (map) fprintf(stderr, "HK_MAPFILE: %s\n", err);
			StartServer();
		}
	}
	// Debug aid: HK_ZOOM_TEST=1 queues real SDL events: Q down, a little mouse
	// jitter, Q up, through the same path as the keyboard (quick x3 zoom).
	if (getenv("HK_ZOOM_TEST"))
	{
		SDL_Event e;
		SDL_zero(e);
		e.type = SDL_KEYDOWN;
		e.key.state = SDL_PRESSED;
		e.key.keysym.sym = SDLK_q;
		e.key.keysym.scancode = SDL_SCANCODE_Q;
		SDL_PushEvent(&e);
		SDL_Event m;
		SDL_zero(m);
		m.type = SDL_MOUSEMOTION;
		m.motion.xrel = 2;
		m.motion.yrel = -3;
		SDL_PushEvent(&m);
		e.type = SDL_KEYUP;
		e.key.state = SDL_RELEASED;
		SDL_PushEvent(&e);
	}
	// Debug aid: HK_JOIN=<host> joins that server as soon as it answers (a game
	// client against a local HK_AUTOSTART server).
	int autojoin = 0;
	if (const char *host = getenv("HK_JOIN"))
	{
		JoinNetOpen();
		autojoin = JoinNetQuery(host);
	}
	// Debug aid: HK_LOBBYJOIN=<game code> looks the game up on the lobby (net.ini
	// lobby=) and joins it, through the join screen's Internet tab (tests/net_lobby.py).
	if (const char *code = getenv("HK_LOBBYJOIN"))
		JoinScreenJoinCode(code);
	// Debug aid: HK_MENU=options|credits|join|internet opens that screen at startup.
	if (const char *m = getenv("HK_MENU"))
	{
		if (!strcmp(m, "options")) OpenOptions();
		if (!strcmp(m, "options2")) { OpenOptions(); mousepos = Vector(-.06f, .83f, 0); OptionsClick(); }
		if (!strcmp(m, "credits")) { mousepos = Vector(0, -.92f, 0); MenuMouseDown(); }
		if (!strcmp(m, "join")) OpenJoinScreen();
		if (!strcmp(m, "internet")) JoinScreenJoinCode("");   // the Internet tab
		if (!strcmp(m, "joindemo"))
		{
			OpenJoinScreen();
			JoinNetDebugAdd("Nicolas's fort", "192.168.1.20", EDIFICE_VERSION_STRING, EDIFICE_PROTOCOL_VERSION, 12, 5, 0);
			JoinNetDebugAdd("Sandbox builders", "192.168.1.31", EDIFICE_VERSION_STRING, EDIFICE_PROTOCOL_VERSION, 38, 2, 1);
			JoinNetDebugAdd("Old school server", "82.64.10.7", "old", 1, 95, -1, 0);
			JoinNetDebugAdd("Edifice EU #1", "51.15.200.12", EDIFICE_VERSION_STRING, EDIFICE_PROTOCOL_VERSION, 140, 11, 0);
			JoinNetDebugAdd("Future server", "203.0.113.5", "0.4.0", EDIFICE_PROTOCOL_VERSION + 1, 260, 3, 0);
			mousepos = Vector(0, .3f, 0);
			JoinScreenClick(1);
		}
		if (!strcmp(m, "server")) OpenServerSetup();
		if (!strcmp(m, "build")) { StartServer(); OpenBuildMenu(); }
		if (!strcmp(m, "scores")) { StartServer(); GetLocalPlayer()->scores = 1; }
		if (!strcmp(m, "chat"))
		{
			StartServer();
			ChatAddLine("Nicolas", 1, 0, "salut tout le monde, on fait une partie ?");
			ChatAddLine("Bob", 2, 0, "ok mais je construis d'abord un fort pres du drapeau bleu, couvrez-moi pendant ce temps svp");
			ChatAddLine("Alice", 2, 1, "je prends le jetpack, rdv a la tour");
			GetLocalPlayer()->talking = 2;
			ChatHandleText("Ca marche, j'arrive \xc3\xa0 la tour");
		}
		if (!strcmp(m, "serverstart")) { OpenServerSetup(); ServerSetupHandleKey(KEY_RETURN); }
		if (!strcmp(m, "player")) { OpenOptions(); mousepos = Vector(.24f, .83f, 0); OptionsClick(); }
	}

	// Debug aid: HK_CONSOLE=1 starts a local server and opens the console after a few
	// commands, so it has content; HK_CONSOLE=scroll also scrolls back, HK_CONSOLE=notify
	// leaves it closed (last lines over the game), any other <text> is left at the prompt.
	if (const char *c = getenv("HK_CONSOLE"))
	{
		StartServer();
		const char *cmds[] = { "help", "maps", "gravity", "loadmap ../nope", "drawdistance 60" };
		for (int i = 0; i < 5; i++) ConsoleExecute(cmds[i]);
		// HK_CONSOLE_RUN="savemap a;loadmap a": more commands, ';'-separated (map tests)
		if (const char *run = getenv("HK_CONSOLE_RUN"))
		{
			char cmd[256];
			while (*run)
			{
				size_t n = strcspn(run, ";");
				snprintf(cmd, sizeof(cmd), "%.*s", (int)n, run);
				if (cmd[0]) ConsoleExecute(cmd);
				run += n + (run[n] == ';');
			}
		}
		if (!strcmp(c, "scroll")) ConsoleScroll(12);
		else if (strcmp(c, "1") && strcmp(c, "notify")) ConsoleSetInput(c);
		consolevisible = strcmp(c, "notify") != 0;
	}

	DWORD nexttimertick = 0;
	while (!quitrequested)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
			HandleEvent(ev);
		if (quitrequested) break;
		// HK_JOIN: joins the first server that answered, like a click on the join screen
		if (autojoin && GetLocalPlayer()->playstate == 0 && JoinGameCount() > 0)
		{
			char reason[200];
			autojoin = 0;
			if (!JoinNetConnect(0, reason, sizeof(reason)))
				fprintf(stderr, "HK_JOIN: %s\n", reason);
		}

		curtime = HKPerfMillis();

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
			HKSleep(4);
		// The build menu frees the mouse but keeps drawing the game behind it.
		if (GetLocalPlayer()->playstate == 0 || (lockedcursor == 0 && !IsBuildMenuOpen()))
		{
			consoleoverlay = 0;   // the console log draws its own frames while loading
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
	VoiceShutdown();
	UnInitSound();
	TexShutdown();
	SDL_GL_DeleteContext(glcontext);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

