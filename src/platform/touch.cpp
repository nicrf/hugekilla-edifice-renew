// Touch screens: an on-screen gamepad, mainly for the web version on phones and
// tablets (native touch screens too). It shows at the first touch of a finger (not a
// trackpad's: only direct touch devices) and goes when the mouse, the keyboard or a
// gamepad is used again (Options -> Gamepad: always, never, and its size).
//
// In the game: a finger down on the left side holds a move stick there (analog, as a
// gamepad's: SetPadMove), one on the right side looks around as it drags, and the
// buttons press their action's key (fire, jump, jetpack, reload, weapon, zoom, claim,
// build, scores, menu), so they do what the keyboard does. All at once: each finger
// has its own part. Fire also looks as it drags, to aim while shooting. In the menus
// a tap clicks where it lifts (a drag scrolls the build menu instead).
//
// Sizes are in window points, which the system scales to the screen (CSS pixels on
// the web): inputlogic.h InTouchLayoutFor, tested by tests/input_test.cpp.

#include "touch.h"
#include "gamepad.h"
#include "renderer.h"
#include "platform.h"
#include "hkkeys.h"
#include "hktime.h"
#include "cfgfile.h"
#include "playing.h"
#include "ui.h"
#include "inputlogic.h"
#include <SDL.h>
#include <math.h>

TouchSettings touchsettings = {1, 1};

enum { ROLE_NONE, ROLE_TAP, ROLE_STICK, ROLE_LOOK, ROLE_BUTTON };

struct Finger
{
	int used;
	SDL_FingerID id;
	int role;
	int button;         // ROLE_BUTTON: which (TB_*), and the key its press sent
	int key;
	float x, y;         // window points
	float downx, downy;
	float stickx, sticky;   // ROLE_STICK: the stick's center
	int dragged;        // ROLE_TAP: moved too far to be a tap
	float scroll;       // ROLE_TAP over the build menu: points dragged, not yet scrolled
};

#define MAXFINGERS 10
static Finger fingers[MAXFINGERS];
static int shown;
static int moving;               // the stick walks the player (SetPadMove)
static float lookx, looky;       // fractions of mouse counts not yet sent
static uint32_t lasttap;
static float lasttapx, lasttapy;

void TouchLoadConfig()
{
	CfgFile ini;
	ini.Load(UserFile("control.ini"));
	if (ini.GetValues("touchcontrols")) touchsettings.mode = ini.GetValuei("touchcontrols");
	if (ini.GetValues("touchsize")) touchsettings.size = ini.GetValuei("touchsize") / 100.f;
	if (touchsettings.mode < 0 || touchsettings.mode > 2) touchsettings.mode = 1;
	if (touchsettings.size < .5f || touchsettings.size > 2) touchsettings.size = 1;
}

int TouchIniValues(const char **keys, char values[][32], int max)
{
	if (max < 2) return 0;
	keys[0] = "touchcontrols";
	snprintf(values[0], 32, "%d", touchsettings.mode);
	keys[1] = "touchsize";
	snprintf(values[1], 32, "%d", (int)(touchsettings.size * 100 + .5f));
	return 2;
}

int TouchShown()
{
	return touchsettings.mode == 2 || (shown && touchsettings.mode != 0);
}

void TouchHide()
{
	if (touchsettings.mode != 2) shown = 0;
}

static void WindowPoints(float *w, float *h)
{
	int pw, ph;
	HKWindowSize(&pw, &ph);
	*w = pw / HKPixelScale();
	*h = ph / HKPixelScale();
	if (*w < 1) *w = 1;
	if (*h < 1) *h = 1;
}

static void Layout(InTouchLayout *l)
{
	float w, h;
	WindowPoints(&w, &h);
	InTouchLayoutFor(w, h, touchsettings.size, l);
}

// the on-screen controls take the fingers: playing, the mouse captured, no menu up
static int Controls()
{
	return TouchShown() && HKInGame() && !consolevisible && !IsSpawnScreenOpen();
}

// the pause menu over a game: its Menu button resumes
static int Paused()
{
	return GetLocalPlayer()->playstate != 0 && !HKInGame() && !IsBuildMenuOpen();
}

static Finger *FingerOf(SDL_FingerID id)
{
	for (int i = 0; i < MAXFINGERS; i++)
		if (fingers[i].used && fingers[i].id == id) return &fingers[i];
	return 0;
}

// a button's press: the key of its action, or what it does placing a structure;
// returns the key to release with the finger (0: none)
static int ButtonDown(int b)
{
	int vk = 0;
	switch (b)
	{
	case TB_FIRE: vk = controls.fire; break;   // placing a structure: builds it
	case TB_JUMP: vk = controls.jump; break;
	case TB_JETPACK: vk = controls.jetpack; break;
	case TB_RELOAD:
		if (menu == BUILDING)
		{
			BuildPreviewWheel(1, 0);   // turns the structure
			return 0;
		}
		vk = controls.reload;
		break;
	case TB_WEAPON:
		if (menu != BUILDING) PadNextWeapon(1);
		return 0;
	case TB_ZOOM: vk = controls.zoom; break;   // held: dragging sets the zoom, as the mouse
	case TB_CLAIM: vk = menu == BUILDING ? KEY_RBUTTON : controls.claim; break;   // cancels placing
	case TB_BUILD: vk = controls.build; break;
	case TB_SCORES: vk = controls.showscores; break;
	case TB_MENU: vk = KEY_ESCAPE; break;
	}
	if (vk) HKInjectKey(vk, 1);
	return vk;
}

static void Look(float dx, float dy)
{
	// about 80 degrees for a drag across a phone's height, as mouse counts (Play()
	// turns .003 rad a count times the mouse sensitivity)
	float counts = .0045f / (.003f * controls.mousesensitivity);
	HKInjectLook(InTakeCounts(&lookx, dx * counts), InTakeCounts(&looky, dy * counts));
}

static void ToMenu(float x, float y, float w, float h)
{
	mousepos = Vector(x / w * 2 - 1, 1 - y / h * 2, 0);
}

int TouchHandleEvent(const SDL_Event &ev)
{
	if (ev.type != SDL_FINGERDOWN && ev.type != SDL_FINGERMOTION && ev.type != SDL_FINGERUP) return 0;
	// a trackpad's fingers (macOS) aren't on the screen: the mouse handles those
	if (ev.tfinger.touchId == TOUCH_TESTID ? !getenv("HK_PADTEST") :
		SDL_GetTouchDeviceType(ev.tfinger.touchId) != SDL_TOUCH_DEVICE_DIRECT)
		return 0;
	float w, h;
	WindowPoints(&w, &h);
	float x = ev.tfinger.x * w, y = ev.tfinger.y * h;
	InTouchLayout l;
	InTouchLayoutFor(w, h, touchsettings.size, &l);
	Finger *f = FingerOf(ev.tfinger.fingerId);

	if (ev.type == SDL_FINGERDOWN)
	{
		if (touchsettings.mode != 0) shown = 1;
		UIFocusShow(0);
		if (!f)
			for (int i = 0; i < MAXFINGERS && !f; i++)
				if (!fingers[i].used) f = &fingers[i];
		if (!f) return 1;
		memset(f, 0, sizeof(*f));
		f->used = 1;
		f->id = ev.tfinger.fingerId;
		f->x = f->downx = x;
		f->y = f->downy = y;
		int hit = InTouchHit(&l, x, y);
		if (Controls())
		{
			if (hit >= 0)
			{
				f->role = ROLE_BUTTON;
				f->button = hit;
				f->key = ButtonDown(hit);
			}
			else if (x < l.stickzone)
			{
				f->role = ROLE_STICK;
				InTouchStickCenter(&l, x, y, &f->stickx, &f->sticky);
			}
			else
				f->role = ROLE_LOOK;
		}
		else if (HKInGame() && IsSpawnScreenOpen())
		{
			HKInjectKey(KEY_RETURN, 1);   // a tap anywhere plays, as a click does
			HKInjectKey(KEY_RETURN, 0);
		}
		else if (TouchShown() && Paused() && hit == TB_MENU)
		{
			HKInjectKey(KEY_ESCAPE, 1);   // resumes
			HKInjectKey(KEY_ESCAPE, 0);
		}
		else if (!HKInGame())
		{
			f->role = ROLE_TAP;
			ToMenu(x, y, w, h);   // hovered while held, clicked when lifted
		}
		return 1;
	}
	if (!f) return 1;
	float dx = x - f->x, dy = y - f->y;
	f->x = x;
	f->y = y;
	if (ev.type == SDL_FINGERMOTION)
	{
		switch (f->role)
		{
		case ROLE_LOOK:
			if (HKInGame()) Look(dx, dy);
			break;
		case ROLE_BUTTON:
			if ((f->button == TB_FIRE || f->button == TB_ZOOM) && HKInGame()) Look(dx, dy);   // aims while held
			break;
		case ROLE_TAP:
			ToMenu(x, y, w, h);
			if (fabsf(x - f->downx) + fabsf(y - f->downy) > 12) f->dragged = 1;
			if (IsBuildMenuOpen())
			{
				// dragged up: the list goes up, to show what is below
				f->scroll += dy;
				while (f->scroll <= -24) { BuildMenuWheel(-1); f->scroll += 24; }
				while (f->scroll >= 24) { BuildMenuWheel(1); f->scroll -= 24; }
			}
			break;
		}
		return 1;
	}
	// up
	if (f->role == ROLE_BUTTON && f->key) HKInjectKey(f->key, 0);
	if (f->role == ROLE_TAP && !f->dragged && !HKInGame())
	{
		// two taps at the same place: a double click (join a game of the list)
		uint32_t now = HKTicks();
		int clicks = now - lasttap < 450 && fabsf(x - lasttapx) + fabsf(y - lasttapy) < 30 ? 2 : 1;
		lasttap = clicks == 2 ? 0 : now;
		lasttapx = x;
		lasttapy = y;
		ToMenu(x, y, w, h);
		HKInjectClick(1, clicks);
		HKInjectClick(0);
	}
	f->used = 0;
	return 1;
}

void TouchFrame()
{
	Finger *stick = 0;
	for (int i = 0; i < MAXFINGERS && !stick; i++)
		if (fingers[i].used && fingers[i].role == ROLE_STICK) stick = &fingers[i];
	InTouchLayout l;
	Layout(&l);
	if (stick && HKInGame() && !IsSpawnScreenOpen())
	{
		float sx, sy, x, y;
		InTouchStick(stick->stickx, stick->sticky, l.stick.r, stick->x, stick->y, &sx, &sy);
		InStick(sx, sy, .12f, 1, &x, &y);
		InMove m = InStickMove(x, y);
		SetPadMove(x != 0 || y != 0, m.fwd, m.strafe, m.up, m.down, m.left, m.right);
		moving = 1;
	}
	else if (moving)
	{
		SetPadMove(0, 0, 0, 0, 0, 0, 0);
		moving = 0;
	}
}

//---- drawing ----

static float vw, vh;   // window points, for the conversions below

static float X(float x) { return x / vw * 2 - 1; }
static float Y(float y) { return 1 - y / vh * 2; }

static void Disc(float cx, float cy, float r, float red, float green, float blue, float a)
{
	const int n = 32;
	RDisable(RCAP_TEXTURE);
	RColor4f(red, green, blue, a);
	RBegin(RP_TRIANGLES);
	for (int i = 0; i < n; i++)
	{
		float a0 = i * 6.2831853f / n, a1 = (i + 1) * 6.2831853f / n;
		RVertex2f(X(cx), Y(cy));
		RVertex2f(X(cx + cosf(a0) * r), Y(cy + sinf(a0) * r));
		RVertex2f(X(cx + cosf(a1) * r), Y(cy + sinf(a1) * r));
	}
	REnd();
	REnable(RCAP_TEXTURE);
}

static void Ring(float cx, float cy, float r, float t, float a)
{
	const int n = 40;
	RDisable(RCAP_TEXTURE);
	RColor4f(1, 1, 1, a);
	RBegin(RP_QUADS);
	for (int i = 0; i < n; i++)
	{
		float a0 = i * 6.2831853f / n, a1 = (i + 1) * 6.2831853f / n;
		RVertex2f(X(cx + cosf(a0) * r), Y(cy + sinf(a0) * r));
		RVertex2f(X(cx + cosf(a0) * (r - t)), Y(cy + sinf(a0) * (r - t)));
		RVertex2f(X(cx + cosf(a1) * (r - t)), Y(cy + sinf(a1) * (r - t)));
		RVertex2f(X(cx + cosf(a1) * r), Y(cy + sinf(a1) * r));
	}
	REnd();
	REnable(RCAP_TEXTURE);
}

// a label inside a circle: the menus' font, shrunk to fit
static void Label(const InCircle &c, const char *text, float a)
{
	float hpt = c.r > 30 ? 15 : 12;
	float h = hpt * 2 / vh;
	float maxw = c.r * 1.7f * 2 / vw;
	float tw = UITextWidth(text, h);
	if (tw > maxw)
	{
		h *= maxw / tw;
		tw = maxw;
	}
	RColor4f(1, 1, 1, a);
	UIText(X(c.x) - tw / 2, UITextY(Y(c.y) - h, Y(c.y) + h, h), text, h);
}

static void Button(const InTouchLayout &l, int b, const char *text, int pressed)
{
	const InCircle &c = l.b[b];
	float t = 2.5f * l.scale;
	if (b == TB_FIRE) Disc(c.x, c.y, c.r, 1, .35f, .3f, pressed ? .55f : .28f);
	else Disc(c.x, c.y, c.r, 1, 1, 1, pressed ? .4f : .14f);
	Ring(c.x, c.y, c.r, t, pressed ? .8f : .45f);
	Label(c, text, pressed ? 1 : .8f);
}

void DrawTouchControls()
{
	if (!TouchShown()) return;
	int ingame = Controls(), paused = Paused();
	if (!ingame && !paused) return;
	InTouchLayout l;
	Layout(&l);
	vw = l.w;
	vh = l.h;
	RPushAttrib(RATTR_ENABLE | RATTR_CURRENT | RATTR_COLOR);
	RDisable(RCAP_DEPTHTEST | RCAP_CULL);   // the game may leave culling on: discs turn clockwise
	REnable(RCAP_BLEND);
	RBlendFunc(RBLEND_ALPHA);
	REnable(RCAP_TEXTURE);
	if (paused)
	{
		Button(l, TB_MENU, "Resume", 0);
		RPopAttrib();
		RColor3f(1, 1, 1);
		return;
	}
	int pressed[TB_COUNT] = {0};
	Finger *stick = 0;
	for (int i = 0; i < MAXFINGERS; i++)
	{
		if (!fingers[i].used) continue;
		if (fingers[i].role == ROLE_BUTTON) pressed[fingers[i].button] = 1;
		if (fingers[i].role == ROLE_STICK) stick = &fingers[i];
	}
	// the stick: where the finger put it, else where it waits
	float cx = stick ? stick->stickx : l.stick.x, cy = stick ? stick->sticky : l.stick.y;
	Disc(cx, cy, l.stick.r, 1, 1, 1, stick ? .16f : .08f);
	Ring(cx, cy, l.stick.r, 2.5f * l.scale, stick ? .5f : .3f);
	float kx = cx, ky = cy;
	if (stick)
	{
		float sx, sy;
		InTouchStick(cx, cy, l.stick.r, stick->x, stick->y, &sx, &sy);
		kx += sx * l.stick.r;
		ky += sy * l.stick.r;
	}
	Disc(kx, ky, l.stick.r * .42f, 1, 1, 1, stick ? .45f : .22f);

	int building = menu == BUILDING;
	static const char *labels[TB_COUNT] = {"Fire", "Jump", "Jet", "Reload", "Weapon", "Zoom", "Claim", "Build", "Scores", "Menu"};
	for (int b = 0; b < TB_COUNT; b++)
	{
		const char *text = labels[b];
		if (building && b == TB_FIRE) text = "Place";
		if (building && b == TB_RELOAD) text = "Turn";
		if (building && b == TB_CLAIM) text = "Cancel";
		if (building && b == TB_WEAPON) continue;
		Button(l, b, text, pressed[b]);
	}
	RPopAttrib();
	RColor3f(1, 1, 1);
}
