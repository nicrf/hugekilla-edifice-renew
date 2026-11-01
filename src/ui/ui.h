void DrawMenu();
void MenuMouseDown();
void HandleButton(int handler);
int CloseCredits();
void DrawScoreboard();
void ChatAddLine(const char *name,int team,int teamonly,const char *text);
void ChatOpened(int key);
void ChatHandleText(const char *utf8);
void DrawChat();
void DrawVoiceHUD();
void DrawConsolePanel(int fullscreen);
void DrawConsoleNotify();
void ConsoleHandleText(const char *utf8);
void ConsoleScroll(int rows);
void ConsoleExecute(const char *cmd);
void ConsoleSetInput(const char *text);
int IsBuildMenuOpen();
void OpenBuildMenu();
void BuildMenuClick();
void BuildMenuWheel(int dir);
int BuildMenuHandleKey(int vk);
void DrawBuildMenu();
#ifndef HK_WIN32_NATIVE
void OpenOptions();
int IsOptionsOpen();
int OptionsHandleKey(int vk);
void OptionsClick();
void DrawOptions();
void LoadOptions();
void OptionsHandleText(const char *text);
void SetIniValues(const char *filename,const char *keys[],const char *values[],int count);
void CopyIniString(char *dst,const char *src,int size);
void AppendText(char *field,int size,const char *text);
void OpenServerSetup();
int IsServerSetupOpen();
void ServerSetupClick();
int ServerSetupHandleKey(int vk);
void ServerSetupHandleText(const char *text);
void DrawServerSetup();
#endif

//menu screen text and widgets (ui.cpp): the HUD font (texture 300) packed by the measured
//width of each glyph instead of DrawText's wide fixed cells; h is the height of a glyph
//cell (DrawText's is .07), y its bottom
#define UITEXTH .05f
float UIText(float x,float y,const char *text,float h=UITEXTH,int len=-1);   //returns the end x
float UITextWidth(const char *text,float h=UITEXTH,int len=-1);
float UITextFit(float x,float y,const char *text,float maxw,float h=UITEXTH);  //cut with "..."
float UITextY(float y1,float y2,float h=UITEXTH);   //y that centers a line between y1 and y2
int UIInside(float x1,float y1,float x2,float y2);
void UIRect(float x1,float y1,float x2,float y2,float r,float g,float b,float a);
void UIPanel(float x1,float y1,float x2,float y2,float titleh,const char *title);
int UIButton(float x1,float y1,float x2,float y2,const char *label,int enabled,int strong=0);
void UIField(float x1,float y1,float x2,float y2,const char *text,int focused,const char *placeholder=0);

struct Button
{
	int handler;
	int tex;
	int texpos;
	float x1,y1,x2,y2;
};