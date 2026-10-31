//options screen opened from the main menu: key bindings, video and audio settings.
//everything is applied immediately and written back to control.ini when closed.

#ifndef _WIN32

#include "stdafx.h"
#include "redrawing.h"
#include "playing.h"
#include "sound.h"
#include "ui.h"

extern Controls controls;
extern GraphicsPrefs graphicsprefs;
extern Vector mousepos;

void HKSetFullscreen(int on);
int HKGetFullscreen();
void HKSetVSync(int on);
int HKGetVSync();

int showoptions;
static int optionstab;     //0 controls, 1 video/audio
static int capturing=-1;   //binding row waiting for a key
static float volume=1;

struct Binding
{
	const char *label;
	const char *inikey;
	int *key;
};

static Binding bindings[]=
{
	{"Forward","forward",&controls.forward},
	{"Back","back",&controls.back},
	{"Strafe left","lstrafe",&controls.lstrafe},
	{"Strafe right","rstrafe",&controls.rstrafe},
	{"Fire","fire",&controls.fire},
	{"Jump","jump",&controls.jump},
	{"Jetpack","jetpack",&controls.jetpack},
	{"Hang glide","hangglide",&controls.hangglide},
	{"Zoom","zoom",&controls.zoom},
	{"Reload","reload",&controls.reload},
	{"Claim object","claim",&controls.claim},
	{"Scores","showscores",&controls.showscores},
	{"Talk","talk",&controls.talk},
	{"Team talk","talkteam",&controls.talkteam},
	{"Screenshot","screenshot",&controls.screenshot},
};
#define NUMBINDINGS (int)(sizeof(bindings)/sizeof(bindings[0]))

#define NUMSETTINGS 6
static const char *settinglabels[NUMSETTINGS]=
{
	"Fullscreen","VSync","Draw distance","Mouse sensitivity","Invert mouse","Volume"
};

//layout, in screen units (-1..1)

#define PANELX1 -.95f
#define PANELX2 .95f
#define PANELY1 -.92f
#define PANELY2 .85f
#define ROWTOP .58f
#define ROWHEIGHT .07f
#define LABELX -.85f
#define VALUEX .15f
#define CHARW .04f

static float RowY(int row)
{
	return ROWTOP-row*ROWHEIGHT;
}

static int RowAt(float y,int numrows)
{
	for(int i=0;i<numrows;i++)
		if(y>=RowY(i)-.01f&&y<RowY(i)+ROWHEIGHT-.01f)
			return i;
	return -1;
}

static int Inside(float x1,float y1,float x2,float y2)
{
	return mousepos.x>x1&&mousepos.x<x2&&mousepos.y>y1&&mousepos.y<y2;
}

//name of a key as written in control.ini, 0 if it can't be stored there

static const char *KeyName(int vk)
{
	static char letter[2];
	if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9'))
	{
		letter[0]=(char)(vk>='A'&&vk<='Z'?vk+32:vk);
		letter[1]=0;
		return letter;
	}
	switch(vk)
	{
	case VK_MENU: return "alt";
	case VK_CONTROL: return "ctrl";
	case VK_SHIFT: return "shift";
	case VK_TAB: return "tab";
	case VK_SPACE: return "space";
	case VK_LBUTTON: return "lbutton";
	case VK_RBUTTON: return "rbutton";
	case VK_UP: return "up";
	case VK_DOWN: return "down";
	case VK_LEFT: return "left";
	case VK_RIGHT: return "right";
	}
	return 0;
}

//replaces "key=value" in control.ini, or appends it

static void SetIniValues(const char *keys[],const char *values[],int count)
{
	HANDLE file=CreateFile("control.ini",GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	char *data=0;
	DWORD len=0;
	if(file!=INVALID_HANDLE_VALUE)
	{
		len=GetFileSize(file,0);
		data=new char[len+1];
		DWORD read;
		ReadFile(file,data,len,&read,0);
		len=read;
		CloseHandle(file);
	}
	else
	{
		data=new char[1];
	}
	data[len]=0;

	char *out=new char[len+count*200+16];
	int outlen=0;
	char *done=new char[count];
	memset(done,0,count);
	char *line=data;
	while(*line)
	{
		char *end=strchr(line,'\n');
		int linelen=end?(int)(end-line)+1:(int)strlen(line);
		int replaced=0;
		for(int k=0;k<count;k++)
		{
			int kl=strlen(keys[k]);
			if(!done[k]&&strncmp(line,keys[k],kl)==0&&(line[kl]=='='||line[kl]==' '))
			{
				outlen+=sprintf(&out[outlen],"%s=%s\r\n",keys[k],values[k]);
				done[k]=1;
				replaced=1;
				break;
			}
		}
		if(!replaced)
		{
			memcpy(&out[outlen],line,linelen);
			outlen+=linelen;
		}
		line+=linelen;
	}
	for(int k=0;k<count;k++)
	{
		if(done[k]) continue;
		if(outlen&&out[outlen-1]!='\n')
			outlen+=sprintf(&out[outlen],"\r\n");
		outlen+=sprintf(&out[outlen],"%s=%s\r\n",keys[k],values[k]);
	}
	file=CreateFile("control.ini",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
	if(file!=INVALID_HANDLE_VALUE)
	{
		DWORD written;
		WriteFile(file,out,outlen,&written,0);
		CloseHandle(file);
	}
	delete []data;
	delete []out;
	delete []done;
}

static void SaveOptions()
{
	const char *keys[NUMBINDINGS+6];
	const char *values[NUMBINDINGS+6];
	char numbers[6][32];
	int n=0;
	for(int i=0;i<NUMBINDINGS;i++)
	{
		const char *name=KeyName(*bindings[i].key);
		if(!name) continue;
		keys[n]=bindings[i].inikey;
		values[n]=strdup(name);
		n++;
	}
	int firstnumber=n;
	sprintf(numbers[0],"%d",HKGetFullscreen());
	sprintf(numbers[1],"%d",HKGetVSync());
	sprintf(numbers[2],"%g",graphicsprefs.drawdistance);
	sprintf(numbers[3],"%g",controls.mousesensitivity);
	sprintf(numbers[4],"%d",(int)controls.mousereverse);
	sprintf(numbers[5],"%d",(int)(volume*100+.5f));
	const char *numberkeys[6]={"changeres","vsync","drawdistance","mousesensitivity","mousereverse","volume"};
	for(int i=0;i<6;i++)
	{
		keys[n]=numberkeys[i];
		values[n]=numbers[i];
		n++;
	}
	SetIniValues(keys,values,n);
	for(int i=0;i<firstnumber;i++)
		free((void *)values[i]);
}

//reads the settings LoadControls doesn't know about (called at startup)

void LoadOptions()
{
	CfgFile ini;
	if(!ini.Load("control.ini")) return;
	float dd=ini.GetValuef("drawdistance");
	if(dd>0) graphicsprefs.drawdistance=dd;
	if(ini.GetValues("vsync")) HKSetVSync(ini.GetValuei("vsync"));
	if(ini.GetValues("volume")) volume=ini.GetValuei("volume")/100.0f;
	SetMasterVolume(volume);
}

void OpenOptions()
{
	showoptions=1;
	capturing=-1;
}

static void CloseOptions()
{
	showoptions=0;
	capturing=-1;
	SaveOptions();
}

int IsOptionsOpen()
{
	return showoptions;
}

static void ChangeSetting(int row,int dir)
{
	switch(row)
	{
	case 0: HKSetFullscreen(!HKGetFullscreen()); break;
	case 1: HKSetVSync(!HKGetVSync()); break;
	case 2:
		graphicsprefs.drawdistance+=dir*5;
		if(graphicsprefs.drawdistance<20) graphicsprefs.drawdistance=20;
		if(graphicsprefs.drawdistance>150) graphicsprefs.drawdistance=150;
		break;
	case 3:
		controls.mousesensitivity+=dir*.25f;
		if(controls.mousesensitivity<.25f) controls.mousesensitivity=.25f;
		if(controls.mousesensitivity>4) controls.mousesensitivity=4;
		break;
	case 4: controls.mousereverse=!controls.mousereverse; break;
	case 5:
		volume+=dir*.1f;
		if(volume<0) volume=0;
		if(volume>1) volume=1;
		SetMasterVolume(volume);
		break;
	}
}

static int IsStepper(int row)
{
	return row==2||row==3||row==5;
}

static void SettingText(int row,char *text)
{
	switch(row)
	{
	case 0: strcpy(text,HKGetFullscreen()?"On":"Off"); break;
	case 1: strcpy(text,HKGetVSync()?"On":"Off"); break;
	case 2: sprintf(text,"< %d >",(int)graphicsprefs.drawdistance); break;
	case 3: sprintf(text,"< %.2f >",controls.mousesensitivity); break;
	case 4: strcpy(text,controls.mousereverse?"On":"Off"); break;
	case 5: sprintf(text,"< %d%% >",(int)(volume*100+.5f)); break;
	}
}

//key or mouse button pressed while the screen is open; returns 1 if used

int OptionsHandleKey(int vk)
{
	if(!showoptions) return 0;
	if(capturing>=0)
	{
		if(vk==VK_ESCAPE)
		{
			capturing=-1;
			return 1;
		}
		if(KeyName(vk))
		{
			*bindings[capturing].key=vk;
			capturing=-1;
		}
		return 1;
	}
	if(vk==VK_ESCAPE) CloseOptions();
	return 1;
}

void OptionsClick()
{
	if(capturing>=0)
	{
		OptionsHandleKey(VK_LBUTTON);
		return;
	}
	if(Inside(-.85f,.68f,-.4f,.78f)) { optionstab=0; return; }
	if(Inside(-.3f,.68f,.3f,.78f)) { optionstab=1; return; }
	if(Inside(.55f,-.88f,.9f,-.78f)) { CloseOptions(); return; }
	if(mousepos.x<LABELX||mousepos.x>PANELX2) return;
	if(optionstab==0)
	{
		int row=RowAt(mousepos.y,NUMBINDINGS);
		if(row>=0) capturing=row;
	}
	else
	{
		int row=RowAt(mousepos.y,NUMSETTINGS);
		if(row<0) return;
		if(IsStepper(row))
		{
			//"<" decreases, anything right of the number increases
			if(mousepos.x<VALUEX+CHARW*1.5f) ChangeSetting(row,-1);
			else ChangeSetting(row,1);
		}
		else
			ChangeSetting(row,1);
	}
}

static void TabText(float x,const char *text,int active)
{
	if(active) glColor3f(1,1,.5f);
	else if(Inside(x-.02f,.68f,x+strlen(text)*CHARW,.78f)) glColor3f(1,1,1);
	else glColor3f(.6f,.6f,.6f);
	DrawText(x,.7f,text);
}

void DrawOptions()
{
	if(!showoptions) return;
	glDisable(GL_TEXTURE_2D);
	glColor4f(0,0,0,.88f);
	glBegin(GL_QUADS);
	glVertex2f(PANELX1,PANELY1);
	glVertex2f(PANELX2,PANELY1);
	glVertex2f(PANELX2,PANELY2);
	glVertex2f(PANELX1,PANELY2);
	glEnd();
	glEnable(GL_TEXTURE_2D);

	TabText(-.85f,"Controls",optionstab==0);
	TabText(-.3f,"Video/Audio",optionstab==1);

	char text[100];
	if(optionstab==0)
	{
		for(int i=0;i<NUMBINDINGS;i++)
		{
			int hover=Inside(LABELX,RowY(i)-.01f,PANELX2,RowY(i)+ROWHEIGHT-.01f);
			glColor3f(hover?1:.8f,hover?1:.8f,hover?1:.8f);
			DrawText(LABELX,RowY(i),bindings[i].label);
			if(i==capturing)
			{
				glColor3f(1,1,.5f);
				DrawText(VALUEX,RowY(i),"press a key...");
			}
			else
			{
				const char *name=KeyName(*bindings[i].key);
				glColor3f(1,1,1);
				DrawText(VALUEX,RowY(i),name?name:"(none)");
			}
		}
		glColor3f(.6f,.6f,.6f);
		DrawText(LABELX,-.84f,"Click an action, then a key");
	}
	else
	{
		for(int i=0;i<NUMSETTINGS;i++)
		{
			int hover=Inside(LABELX,RowY(i)-.01f,PANELX2,RowY(i)+ROWHEIGHT-.01f);
			glColor3f(hover?1:.8f,hover?1:.8f,hover?1:.8f);
			DrawText(LABELX,RowY(i),settinglabels[i]);
			SettingText(i,text);
			glColor3f(1,1,1);
			DrawText(VALUEX,RowY(i),text);
		}
	}
	if(Inside(.55f,-.88f,.9f,-.78f)) glColor3f(1,1,1);
	else glColor3f(.7f,.7f,.7f);
	DrawText(.6f,-.86f,"Close");
	glColor3f(1,1,1);
}

#endif // !_WIN32
