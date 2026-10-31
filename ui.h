void DrawMenu();
void MenuMouseDown();
void HandleButton(int handler);
int CloseCredits();
#ifndef _WIN32
void OpenOptions();
int IsOptionsOpen();
int OptionsHandleKey(int vk);
void OptionsClick();
void DrawOptions();
void LoadOptions();
#endif

struct Button
{
	int handler;
	int tex;
	int texpos;
	float x1,y1,x2,y2;
};