//touch screens (touch.cpp): an on-screen gamepad in the game (a move stick, a look
//area, buttons), shown once a finger touches the screen; taps click the menus

#ifndef TOUCHH
#define TOUCHH

union SDL_Event;

#define TOUCH_TESTID 0x7e57    //the touch device of HK_PADTEST's finger steps

int TouchHandleEvent(const SDL_Event &ev);   //1 if it was a finger event it used
void TouchFrame();             //each frame, after the events (and PadFrame): the stick
void DrawTouchControls();      //over a 2D frame (UIFrameEnd)
int TouchShown();              //the controls are on the screen
void TouchHide();              //a mouse, keyboard or pad is in use (unless always shown)
void TouchLoadConfig();        //touchcontrols and touchsize of control.ini

//Options -> Gamepad
struct TouchSettings
{
	int mode;       //0 never shown, 1 at the first touch, 2 always
	float size;     //1 = 100%
};
extern TouchSettings touchsettings;
int TouchIniValues(const char **keys,char values[][32],int max);

#endif
