//The game's key codes: the Windows virtual-key values of the original code, which
//the names of control.ini are read to (TextToKey) and the SDL events translated to
//(SDLKeyToKey, platform_sdl.cpp). Letters and digits are their ASCII capitals ('A',
//'1'), as on Windows; the punctuation keys keep the Windows OEM numbers (186-222).

#ifndef HKKEYS_H
#define HKKEYS_H

enum
{
	KEY_LBUTTON = 0x01,
	KEY_RBUTTON = 0x02,
	KEY_MBUTTON = 0x04,
	KEY_BACK = 0x08,
	KEY_TAB = 0x09,
	KEY_RETURN = 0x0D,
	KEY_SHIFT = 0x10,
	KEY_CONTROL = 0x11,
	KEY_MENU = 0x12,      //Alt
	KEY_ALT = KEY_MENU,
	KEY_PAUSE = 0x13,
	KEY_CAPITAL = 0x14,
	KEY_ESCAPE = 0x1B,
	KEY_SPACE = 0x20,
	KEY_PRIOR = 0x21,     //Page Up
	KEY_NEXT = 0x22,      //Page Down
	KEY_END = 0x23,
	KEY_HOME = 0x24,
	KEY_LEFT = 0x25,
	KEY_UP = 0x26,
	KEY_RIGHT = 0x27,
	KEY_DOWN = 0x28,
	KEY_INSERT = 0x2D,
	KEY_DELETE = 0x2E,
	KEY_NUMPAD0 = 0x60,
	KEY_F1 = 0x70,
	KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12
};

int HKKeyDown(int key);                //1 while the key (or mouse button) is held
void HKSetKeyDown(int key, int down);  //fed by the SDL event loop

#endif
