// Gamepads, through SDL_GameController: plugged and unplugged at any time, mapped by
// SDL's own database (plus a gamecontrollerdb.txt in the user or data folder, for
// the pads it doesn't know). In the game the left stick walks (analog: playing.cpp
// SetPadMove), the right one looks (as mouse counts, with a dead zone and a response
// curve), and the buttons press the keys their action has (pad_* in control.ini), so
// they do exactly what the keyboard does. Over the menus the D-pad or the left stick
// moves the focus (ui.cpp), A clicks and B goes back. On the web the browser's
// Gamepad API comes through the same SDL calls (Emscripten's SDL2).
//
// The state is kept from the events, not polled from the device: HK_PADTEST (a debug
// aid, see BUILD.md) plays scripted presses through a virtual controller of SDL, or as
// pushed events where there is none, down the same path.

#include "gamepad.h"
#include "platform.h"
#include "hkkeys.h"
#include "hktime.h"
#include "fileio.h"
#include "cfgfile.h"
#include "playing.h"
#include "multiplay.h"
#include "ui.h"
#include "inputlogic.h"
#include "touch.h"
#include <SDL.h>
#include <math.h>

enum
{
	PA_FIRE, PA_JETPACK, PA_JUMP, PA_CLAIM, PA_RELOAD, PA_BUILD, PA_WEAPNEXT, PA_WEAPPREV,
	PA_ZOOM, PA_HANGGLIDE, PA_SCORES, PA_TALK, PA_TALKTEAM, PA_PUSHTOTALK, PA_MENU,
	PA_COUNT
};

struct PadActionDef
{
	const char *label;
	const char *inikey;
	const char *def;    // default input, as in control.ini
	int *key;           // the key it presses (controls), 0: done here
};

static const PadActionDef actions[PA_COUNT] =
{
	{"Fire", "pad_fire", "righttrigger", &controls.fire},
	{"Jetpack", "pad_jetpack", "lefttrigger", &controls.jetpack},
	{"Jump", "pad_jump", "a", &controls.jump},
	{"Claim object", "pad_claim", "b", &controls.claim},
	{"Reload", "pad_reload", "x", &controls.reload},
	{"Build", "pad_build", "y", &controls.build},
	{"Next weapon", "pad_weapnext", "rightshoulder", 0},
	{"Prev. weapon", "pad_weapprev", "leftshoulder", 0},
	{"Zoom", "pad_zoom", "rightstick", &controls.zoom},
	{"Hang glide", "pad_hangglide", "", &controls.hangglide},
	{"Scores", "pad_scores", "back", &controls.showscores},
	{"Talk", "pad_talk", "dpup", &controls.talk},
	{"Team talk", "pad_talkteam", "dpleft", &controls.talkteam},
	{"Push to talk", "pad_pushtotalk", "dpdown", &controls.pushtotalk},
	{"Menu", "pad_menu", "start", 0},
};

// inputs: SDL's buttons, then the two triggers (axes, pressed past half way)
#define IN_LT SDL_CONTROLLER_BUTTON_MAX
#define IN_RT (SDL_CONTROLLER_BUTTON_MAX + 1)
#define NUMINPUTS (SDL_CONTROLLER_BUTTON_MAX + 2)

static const char *inputlabels[NUMINPUTS] =
{
	"A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB",
	"D-pad up", "D-pad down", "D-pad left", "D-pad right", "Misc", "P1", "P2", "P3", "P4", "Touchpad",
	"LT", "RT"
};

PadSettings padsettings = {1, .18f, 1.6f, 0, 1};
static int bound[PA_COUNT];          // input of each action, -1: none
static int capturing = -1;

#define MAXPADS 8
static SDL_GameController *pads[MAXPADS];
static SDL_GameController *lastpad;  // the one last used: rumble, its name in Options
static int numpads;
static int inputdown[NUMINPUTS];
static int ingame[NUMINPUTS];        // its press went to the game: so does its release
static float axes[SDL_CONTROLLER_AXIS_MAX];
static float lookx, looky;           // fractions of mouse counts not yet sent
static InRepeat repeats[4];
static uint32_t lastframe;
static short lastlife;

static void PadTestInit();
static void PadTestFrame();
static int padtest;                  // HK_PADTEST: scripted, with or without a real pad

static const char *InputIniName(int in)
{
	if (in == IN_LT) return "lefttrigger";
	if (in == IN_RT) return "righttrigger";
	const char *s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)in);
	return s ? s : "";
}

static int InputFromName(const char *name)
{
	if (!name || !name[0]) return -1;
	char word[40];
	CopyIniString(word, name, sizeof(word));
	for (char *c = word; *c; c++)
		if (*c == ' ') *c = 0;
	if (!strcmp(word, "lefttrigger")) return IN_LT;
	if (!strcmp(word, "righttrigger")) return IN_RT;
	SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(word);
	return b == SDL_CONTROLLER_BUTTON_INVALID ? -1 : (int)b;
}

void PadLoadConfig()
{
	CfgFile ini;
	ini.Load(UserFile("control.ini"));
	for (int a = 0; a < PA_COUNT; a++)
	{
		// a line without a value unbinds; no line at all (an older control.ini): the default
		const char *v = ini.GetValues(actions[a].inikey);
		bound[a] = InputFromName(v ? v : actions[a].def);
	}
	if (ini.GetValues("pad_sensitivity")) padsettings.sensitivity = ini.GetValuef("pad_sensitivity");
	if (ini.GetValues("pad_deadzone")) padsettings.deadzone = ini.GetValuef("pad_deadzone");
	if (ini.GetValues("pad_curve")) padsettings.curve = ini.GetValuef("pad_curve");
	if (ini.GetValues("pad_invert")) padsettings.invert = ini.GetValuei("pad_invert") != 0;
	if (ini.GetValues("pad_rumble")) padsettings.rumble = ini.GetValuei("pad_rumble") != 0;
	// out of range values (a hand-edited file) would make the pad useless
	if (padsettings.sensitivity < .1f || padsettings.sensitivity > 5) padsettings.sensitivity = 1;
	if (padsettings.deadzone < 0 || padsettings.deadzone > .6f) padsettings.deadzone = .18f;
	if (padsettings.curve < 1 || padsettings.curve > 3) padsettings.curve = 1.6f;
}

int PadIniValues(const char **keys, char values[][32], int max)
{
	int n = 0;
	for (int a = 0; a < PA_COUNT && n < max; a++, n++)
	{
		keys[n] = actions[a].inikey;
		snprintf(values[n], 32, "%s", bound[a] >= 0 ? InputIniName(bound[a]) : "");
	}
	const char *names[5] = {"pad_sensitivity", "pad_deadzone", "pad_curve", "pad_invert", "pad_rumble"};
	for (int i = 0; i < 5 && n < max; i++, n++)
	{
		keys[n] = names[i];
		if (i == 0) snprintf(values[n], 32, "%g", padsettings.sensitivity);
		if (i == 1) snprintf(values[n], 32, "%g", padsettings.deadzone);
		if (i == 2) snprintf(values[n], 32, "%g", padsettings.curve);
		if (i == 3) snprintf(values[n], 32, "%d", padsettings.invert);
		if (i == 4) snprintf(values[n], 32, "%d", padsettings.rumble);
	}
	return n;
}

int PadActionCount()
{
	return PA_COUNT;
}

const char *PadActionLabel(int action)
{
	return action >= 0 && action < PA_COUNT ? actions[action].label : "";
}

const char *PadActionInput(int action)
{
	if (action < 0 || action >= PA_COUNT || bound[action] < 0) return "(none)";
	return inputlabels[bound[action]];
}

void PadCapture(int action)
{
	capturing = action >= 0 && action < PA_COUNT ? action : -1;
}

int PadCapturing()
{
	return capturing;
}

int PadConnected()
{
	return numpads;
}

const char *PadName()
{
	const char *n = lastpad ? SDL_GameControllerName(lastpad) : 0;
	return n ? n : "";
}

static void Message(const char *fmt, const char *arg)
{
	char text[300];
	snprintf(text, sizeof(text), fmt, arg);
	ConsoleDisplayText(text);
}

static void OpenPad(int device)
{
	if (!SDL_IsGameController(device))
	{
		// a joystick SDL has no mapping for: its GUID is what a gamecontrollerdb.txt line needs
		char guid[64];
		SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device), guid, sizeof(guid));
		const char *name = SDL_JoystickNameForIndex(device);
		fprintf(stderr, "gamepad: no mapping for \"%s\" (GUID %s): add it to gamecontrollerdb.txt\n",
			name ? name : "?", guid);
		return;
	}
	SDL_GameController *c = SDL_GameControllerOpen(device);
	if (!c) return;
	for (int i = 0; i < MAXPADS; i++)
		if (pads[i] == c)
			return;   // already open (an added event for a pad opened at startup)
	for (int i = 0; i < MAXPADS; i++)
		if (!pads[i])
		{
			pads[i] = c;
			numpads++;
			lastpad = c;
			const char *name = SDL_GameControllerName(c);
			Message("Gamepad connected: %s", name ? name : "?");
			return;
		}
	SDL_GameControllerClose(c);
}

static void InputChanged(int in, int down);

static void ReleaseAll()
{
	for (int i = 0; i < NUMINPUTS; i++)
		InputChanged(i, 0);
	for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
		axes[i] = 0;
}

static void ClosePad(SDL_JoystickID id)
{
	for (int i = 0; i < MAXPADS; i++)
	{
		if (!pads[i] || SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) != id) continue;
		const char *name = SDL_GameControllerName(pads[i]);
		Message("Gamepad disconnected: %s", name ? name : "?");
		if (lastpad == pads[i]) lastpad = 0;
		SDL_GameControllerClose(pads[i]);
		pads[i] = 0;
		numpads--;
		ReleaseAll();   // nothing stays held (firing, walking) when it goes
		break;
	}
	for (int i = 0; i < MAXPADS && !lastpad; i++)
		lastpad = pads[i];
}

static SDL_GameController *PadOf(SDL_JoystickID id)
{
	for (int i = 0; i < MAXPADS; i++)
		if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id)
			return pads[i];
	return 0;
}

void PadInit()
{
	SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");   // A is the bottom button on every pad
	// scripted runs often play in a window without the focus, whose pad events SDL drops
	if (getenv("HK_PADTEST")) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
	{
		fprintf(stderr, "gamepad: %s\n", SDL_GetError());
		return;
	}
	// mappings for pads SDL doesn't know: gamecontrollerdb.txt (the community's file,
	// github.com/mdqinc/SDL_GameControllerDB) in the user folder, or in data/
	const char *dbs[2] = {UserFile("gamecontrollerdb.txt"), "gamecontrollerdb.txt"};
	for (int i = 0; i < 2; i++)
	{
		if (!HKFileExists(dbs[i])) continue;
		long len = 0;
		char *text = HKReadFile(dbs[i], &len);   // UTF-8 user folder: not SDL's fopen
		if (!text) continue;
		int n = SDL_GameControllerAddMappingsFromRW(SDL_RWFromConstMem(text, (int)len), 1);
		delete[] text;
		if (n > 0) fprintf(stderr, "gamepad: %d mappings from %s\n", n, dbs[i]);
		break;
	}
	SDL_GameControllerEventState(SDL_ENABLE);
	// the pads already plugged in also come as SDL_CONTROLLERDEVICEADDED events
	lastframe = HKPerfMillis();
	PadTestInit();
}

void PadShutdown()
{
	for (int i = 0; i < MAXPADS; i++)
		if (pads[i])
		{
			SDL_GameControllerClose(pads[i]);
			pads[i] = 0;
		}
	numpads = 0;
	lastpad = 0;
}

// weapons in turn, around from the last to the first, as pads' bumpers usually do
void PadNextWeapon(int dir)
{
	Player *me = GetLocalPlayer();
	int before = me->weapon;
	SwitchGun(dir > 0 ? 51 : 50);
	if (me->weapon != before) return;
	for (int i = dir > 0 ? 0 : 6; i >= 0 && i <= 6; i += dir)
		if (me->owngun[i])
		{
			if (i != before) SwitchGun(i);
			return;
		}
}

static void DoAction(int a, int down)
{
	switch (a)
	{
	case PA_MENU:
		HKInjectKey(KEY_ESCAPE, down);
		return;
	case PA_WEAPNEXT:
	case PA_WEAPPREV:
		if (!down) return;
		// placing a structure: they turn it, as the mouse wheel does
		if (menu == BUILDING) BuildPreviewWheel(a == PA_WEAPNEXT ? -1 : 1, 0);
		else PadNextWeapon(a == PA_WEAPNEXT ? 1 : -1);
		return;
	case PA_CLAIM:
		if (menu == BUILDING)
		{
			HKInjectKey(KEY_RBUTTON, down);   // cancels placing it, as a right click
			return;
		}
		break;
	}
	if (actions[a].key && *actions[a].key)
		HKInjectKey(*actions[a].key, down);
}

// the game has the pad: walking, looking, the actions
static int PadInGame()
{
	return HKInGame() && !consolevisible && !GetLocalPlayer()->talking;
}

static void GameInput(int in, int down)
{
	// the spawn screen: A plays, like Enter (the fire action also does)
	if (down && in == SDL_CONTROLLER_BUTTON_A && IsSpawnScreenOpen())
	{
		HKInjectKey(KEY_RETURN, 1);
		HKInjectKey(KEY_RETURN, 0);
		return;
	}
	for (int a = 0; a < PA_COUNT; a++)
		if (bound[a] == in) DoAction(a, down);
}

static void MenuInput(int in)
{
	if (in == SDL_CONTROLLER_BUTTON_A)
		UIFocusActivate();
	// B, or the menu button (Start): back, as Esc (closes a screen, resumes the game,
	// closes the chat or the console)
	else if (in == SDL_CONTROLLER_BUTTON_B || in == bound[PA_MENU])
	{
		HKInjectKey(KEY_ESCAPE, 1);
		HKInjectKey(KEY_ESCAPE, 0);
	}
	else if (in == bound[PA_BUILD] && IsBuildMenuOpen())
	{
		HKInjectKey(controls.build, 1);   // closes it, like the key that opened it
		HKInjectKey(controls.build, 0);
	}
}

static void InputChanged(int in, int down)
{
	if (in < 0 || in >= NUMINPUTS || inputdown[in] == down) return;
	inputdown[in] = down;
	if (down) TouchHide();   // the pad is in use: the touch controls go
	if (!down)
	{
		if (ingame[in])
		{
			ingame[in] = 0;
			GameInput(in, 0);
		}
		return;
	}
	if (capturing >= 0)
	{
		// rebinding in Options: this input does that now, and nothing else does
		for (int a = 0; a < PA_COUNT; a++)
			if (bound[a] == in) bound[a] = -1;
		bound[capturing] = in;
		capturing = -1;
		return;
	}
	if (PadInGame())
	{
		ingame[in] = 1;
		GameInput(in, 1);
	}
	else if (HKInGame() && (in == bound[PA_MENU] || in == SDL_CONTROLLER_BUTTON_B))
	{
		HKInjectKey(KEY_ESCAPE, 1);   // closes the chat or the console
		HKInjectKey(KEY_ESCAPE, 0);
	}
	else if (!HKInGame())
		MenuInput(in);
}

int PadHandleEvent(const SDL_Event &ev)
{
	switch (ev.type)
	{
	case SDL_CONTROLLERDEVICEADDED:
		OpenPad(ev.cdevice.which);
		return 1;
	case SDL_CONTROLLERDEVICEREMOVED:
		ClosePad(ev.cdevice.which);
		return 1;
	case SDL_JOYDEVICEADDED:
		// one with a mapping also comes as SDL_CONTROLLERDEVICEADDED
		if (!SDL_IsGameController(ev.jdevice.which)) OpenPad(ev.jdevice.which);
		return 1;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		if (SDL_GameController *c = PadOf(ev.cbutton.which)) lastpad = c;
		InputChanged(ev.cbutton.button, ev.type == SDL_CONTROLLERBUTTONDOWN);
		return 1;
	case SDL_CONTROLLERAXISMOTION:
		if (ev.caxis.axis < SDL_CONTROLLER_AXIS_MAX)
		{
			float v = ev.caxis.value / 32767.f;
			axes[ev.caxis.axis] = v < -1 ? -1 : v;
			if (fabsf(v) > .5f)
				if (SDL_GameController *c = PadOf(ev.caxis.which)) lastpad = c;
		}
		return 1;
	}
	return 0;
}

// the left stick or the D-pad held in a direction, for the menus
static int MenuDirection(int dir)
{
	float x = axes[SDL_CONTROLLER_AXIS_LEFTX], y = axes[SDL_CONTROLLER_AXIS_LEFTY];
	switch (dir)
	{
	case IN_LEFT: return inputdown[SDL_CONTROLLER_BUTTON_DPAD_LEFT] || (x < -.55f && fabsf(x) > fabsf(y));
	case IN_RIGHT: return inputdown[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] || (x > .55f && fabsf(x) > fabsf(y));
	case IN_UP: return inputdown[SDL_CONTROLLER_BUTTON_DPAD_UP] || (y < -.55f && fabsf(y) >= fabsf(x));
	default: return inputdown[SDL_CONTROLLER_BUTTON_DPAD_DOWN] || (y > .55f && fabsf(y) >= fabsf(x));
	}
}

void PadFrame()
{
	uint32_t now = HKPerfMillis();
	float dt = (float)(now - lastframe);
	lastframe = now;
	if (dt > 100) dt = 100;   // a hitch (loading a map) must not spin the view
	if (padtest) PadTestFrame();
	if (!numpads && !padtest) return;

	// triggers as buttons, with some hysteresis
	InputChanged(IN_LT, InTrigger(axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT], inputdown[IN_LT]));
	InputChanged(IN_RT, InTrigger(axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT], inputdown[IN_RT]));

	int game = PadInGame();
	Player *me = GetLocalPlayer();
	float x, y;
	// walking: no curve, the stick's travel is the speed
	InStick(axes[SDL_CONTROLLER_AXIS_LEFTX], axes[SDL_CONTROLLER_AXIS_LEFTY], padsettings.deadzone, 1, &x, &y);
	InMove move = InStickMove(x, y);
	if (game && !IsSpawnScreenOpen() && (x != 0 || y != 0))
		SetPadMove(1, move.fwd, move.strafe, move.up, move.down, move.left, move.right);
	else
		SetPadMove(0, 0, 0, 0, 0, 0, 0);

	// looking: rad/s at full tilt, sent as mouse counts (Play() turns .003 rad per
	// count times the mouse sensitivity, which the pad has its own of)
	InStick(axes[SDL_CONTROLLER_AXIS_RIGHTX], axes[SDL_CONTROLLER_AXIS_RIGHTY], padsettings.deadzone, padsettings.curve, &x, &y);
	if (game && (x != 0 || y != 0))
	{
		float counts = 3.2f * padsettings.sensitivity / (.003f * controls.mousesensitivity) * dt / 1000;
		float dy = y * counts * .7f;          // up and down a little slower, as pads usually do
		if (padsettings.invert) dy = -dy;
		if (controls.mousereverse) dy = -dy;  // Play() reverses the mouse's: not the pad's
		HKInjectLook(InTakeCounts(&lookx, x * counts), InTakeCounts(&looky, dy));
	}
	else
		lookx = looky = 0;

	// the menus: a step at once, then repeated while held
	for (int d = 0; d < 4; d++)
	{
		if (!InRepeatStep(&repeats[d], !HKInGame() && MenuDirection(d), now)) continue;
		if (!UIFocusMove(d) && IsBuildMenuOpen() && (d == IN_UP || d == IN_DOWN))
			BuildMenuWheel(d == IN_UP ? 1 : -1);   // past the list's visible end: scroll it
	}

	// a short rumble when hit, stronger for more damage
	if (me->playstate == 10 && me->life < lastlife && padsettings.rumble && lastpad)
	{
		float strength = (lastlife - me->life) / 40.f;
		if (strength > 1) strength = 1;
		Uint16 s = (Uint16)(16000 + 49000 * strength);
		SDL_GameControllerRumble(lastpad, s, (Uint16)(s / 2), 120 + (Uint32)(200 * strength));
	}
	lastlife = me->life;
}

//---- HK_PADTEST: scripted pad input (see BUILD.md) ----
//
// "frame:op:args" steps separated by ';', at the Nth frame (Frame() calls):
//   F:press:NAME       NAME down, up 4 frames later (SDL's button names, lefttrigger, righttrigger)
//   F:down:NAME / F:up:NAME
//   F:axis:NAME:V      leftx, lefty, rightx, righty, lefttrigger, righttrigger to V (-1..1, triggers 0..1)
//   F:log              prints the state on stderr ("padtest: ...")
//   F:unplug           takes the virtual controller away (and F:plug brings it back)
//   F:shot:NAME        saves the frame as NAME.ppm in HK_PADSHOTS (a folder; default: the user folder)
//   F:finger:N:down|move|up:X:Y   finger N of a touch screen at X,Y (0..1 of the window)
// Through a virtual controller (SDL_JoystickAttachVirtual), so SDL's mapping and
// events are those of a real pad; where SDL has none, as pushed SDL events.

#define MAXSTEPS 128
struct PadStep
{
	int frame;
	char op[8];
	char name[24];
	float value;
	int finger;          // finger steps: which, and where
	float x, y;
};
static PadStep steps[MAXSTEPS];
static int numsteps;
static int testframe;
static SDL_Joystick *virtualpad;

static void AttachVirtual()
{
	int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
	if (device >= 0) virtualpad = SDL_JoystickOpen(device);
	if (virtualpad)
	{
		// triggers rest at the bottom of their axis
		SDL_JoystickSetVirtualAxis(virtualpad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
		SDL_JoystickSetVirtualAxis(virtualpad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
	}
}

static void PadTestInit()
{
	const char *script = getenv("HK_PADTEST");
	if (!script || !script[0]) return;
	padtest = 1;
	while (*script && numsteps < MAXSTEPS - 1)
	{
		char step[80];
		size_t n = strcspn(script, "; ");
		snprintf(step, sizeof(step), "%.*s", (int)n, script);
		script += n + (script[n] != 0);
		PadStep *s = &steps[numsteps];
		memset(s, 0, sizeof(*s));
		if (sscanf(step, "%d:%7[a-z]:%23[a-z]:%f", &s->frame, s->op, s->name, &s->value) < 2) continue;
		if (!strcmp(s->op, "finger") &&
			sscanf(step, "%*d:finger:%d:%23[a-z]:%f:%f", &s->finger, s->name, &s->x, &s->y) < 4) continue;
		if (!strcmp(s->op, "press"))
		{
			// as a down now and an up later
			strcpy(s->op, "down");
			PadStep *up = &steps[++numsteps];
			*up = *s;
			strcpy(up->op, "up");
			up->frame += 4;
		}
		numsteps++;
	}
	AttachVirtual();
	fprintf(stderr, "padtest: %d steps, %s\n", numsteps, virtualpad ? "virtual controller" : "pushed events");
}

static void PushButton(int in, int down)
{
	if (in == IN_LT || in == IN_RT)
	{
		SDL_GameControllerAxis ax = in == IN_LT ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
		if (virtualpad) SDL_JoystickSetVirtualAxis(virtualpad, ax, down ? 32767 : -32768);
		else
		{
			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_CONTROLLERAXISMOTION;
			e.caxis.which = -1;
			e.caxis.axis = (Uint8)ax;
			e.caxis.value = down ? 32767 : 0;
			SDL_PushEvent(&e);
		}
		return;
	}
	if (virtualpad)
	{
		SDL_JoystickSetVirtualButton(virtualpad, in, down ? SDL_PRESSED : SDL_RELEASED);
		return;
	}
	SDL_Event e;
	SDL_zero(e);
	e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
	e.cbutton.which = -1;
	e.cbutton.button = (Uint8)in;
	e.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
	SDL_PushEvent(&e);
}

static void PushAxis(const char *name, float v)
{
	SDL_GameControllerAxis ax = SDL_GameControllerGetAxisFromString(name);
	if (ax == SDL_CONTROLLER_AXIS_INVALID) return;
	int trigger = ax == SDL_CONTROLLER_AXIS_TRIGGERLEFT || ax == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
	if (virtualpad)
	{
		float raw = trigger ? -32768 + v * 65535 : v * 32767;
		SDL_JoystickSetVirtualAxis(virtualpad, ax, (Sint16)fmaxf(-32768, fminf(32767, raw)));
		return;
	}
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_CONTROLLERAXISMOTION;
	e.caxis.which = -1;
	e.caxis.axis = (Uint8)ax;
	e.caxis.value = (Sint16)fmaxf(-32768, fminf(32767, v * 32767));
	SDL_PushEvent(&e);
}

static void PadTestLog()
{
	Player *me = GetLocalPlayer();
	float yaw = atan2f(me->fwd.x, me->fwd.z) * 180 / 3.14159265f;
	fprintf(stderr, "padtest: frame %d pads %d ingame %d playstate %d pos %.2f %.2f %.2f yaw %.1f tilt %.3f "
		"weapon %d firing %d jetpack %d zoom %.1f menu %d buildmenu %d up %d down %d left %d right %d focus %d "
		"options %d join %d setup %d touch %d\n",
		testframe, numpads, HKInGame(), me->playstate, me->pos.x, me->pos.y, me->pos.z, yaw, me->tilt.y,
		me->weapon, me->firing, me->jetpack, me->fzoom, menu, IsBuildMenuOpen(), me->up, me->down,
		me->lstrafe, me->rstrafe, UIFocusCount(), IsOptionsOpen(), IsJoinScreenOpen(), IsServerSetupOpen(), TouchShown());
}

static void PadTestFrame()
{
	testframe++;
	for (int i = 0; i < numsteps; i++)
	{
		PadStep *s = &steps[i];
		if (s->frame != testframe) continue;
		if (!strcmp(s->op, "log")) PadTestLog();
		else if (!strcmp(s->op, "finger"))
		{
			// as a touch screen's events: SDL has no virtual touch device
			SDL_Event e;
			SDL_zero(e);
			e.type = !strcmp(s->name, "down") ? SDL_FINGERDOWN : !strcmp(s->name, "up") ? SDL_FINGERUP : SDL_FINGERMOTION;
			e.tfinger.touchId = TOUCH_TESTID;
			e.tfinger.fingerId = s->finger;
			e.tfinger.x = s->x;
			e.tfinger.y = s->y;
			e.tfinger.pressure = 1;
			SDL_PushEvent(&e);
		}
		else if (!strcmp(s->op, "shot"))
		{
			char path[1100];
			const char *dir = getenv("HK_PADSHOTS");
			snprintf(path, sizeof(path), "%s%s%s.ppm", dir ? dir : UserFile(""), dir ? "/" : "", s->name);
			HKSaveFrame(path);
		}
		else if (!strcmp(s->op, "unplug") && virtualpad)
		{
			SDL_JoystickID id = SDL_JoystickInstanceID(virtualpad);
			SDL_JoystickClose(virtualpad);
			virtualpad = 0;
			for (int d = 0; d < SDL_NumJoysticks(); d++)
				if (SDL_JoystickGetDeviceInstanceID(d) == id) SDL_JoystickDetachVirtual(d);
		}
		else if (!strcmp(s->op, "plug") && !virtualpad) AttachVirtual();
		else if (!strcmp(s->op, "axis")) PushAxis(s->name, s->value);
		else if (!strcmp(s->op, "down") || !strcmp(s->op, "up"))
		{
			int in = InputFromName(s->name);
			if (in >= 0) PushButton(in, !strcmp(s->op, "down"));
		}
	}
}
