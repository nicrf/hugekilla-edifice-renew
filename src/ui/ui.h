void DrawMenu();
void MenuMouseDown();
void HandleButton(int handler);
int CloseCredits();
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