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

struct Button
{
	int handler;
	int tex;
	int texpos;
	float x1,y1,x2,y2;
};