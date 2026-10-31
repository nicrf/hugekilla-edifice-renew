void DrawMenu();
void MenuMouseDown();
void HandleButton(int handler);

struct Button
{
	int handler;
	int tex;
	int texpos;
	float x1,y1,x2,y2;
};