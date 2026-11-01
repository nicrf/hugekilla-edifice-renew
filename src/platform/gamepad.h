//gamepads (gamepad.cpp, SDL_GameController): the sticks walk and look, the buttons
//do what the pad_* lines of control.ini bind them to, and over the menus the D-pad
//or the left stick moves the focus (ui.cpp), A clicks and B goes back

#ifndef GAMEPADH
#define GAMEPADH

union SDL_Event;

void PadInit();                        //after SDL_Init and LoadControls
void PadShutdown();
int PadHandleEvent(const SDL_Event &ev);   //1 if it was a controller event
void PadFrame();                       //each frame, after the events: sticks, triggers, repeats
void PadLoadConfig();                  //pad_* of control.ini (LoadControls)
int PadConnected();                    //open gamepads
const char *PadName();                 //of the last one used, or ""

//the Gamepad tab of Options (options.cpp)
struct PadSettings
{
	float sensitivity;   //look speed, 1: about 180 degrees a second at full tilt
	float deadzone;      //0..1 of the stick's travel
	float curve;         //response curve exponent: 1 linear, 2 finer near the center
	int invert;          //pushing up looks down
	int rumble;          //on a hit
};
extern PadSettings padsettings;
int PadActionCount();
const char *PadActionLabel(int action);
const char *PadActionInput(int action);    //what it is bound to, as shown ("A", "RT", "(none)")
void PadCapture(int action);           //the next button pressed binds it (-1: stop)
int PadCapturing();                    //the action waiting for a button, -1 if none
int PadIniValues(const char **keys,char values[][32],int max);   //pad_* lines to save

#endif
