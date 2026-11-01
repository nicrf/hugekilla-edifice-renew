//options screen opened from the main menu: key bindings, video and audio settings.
//everything is applied immediately and written back to control.ini when closed.


#include "gl.h"
#include "platform.h"
#include "fileio.h"
#include "cfgfile.h"
#include "redrawing.h"
#include "playing.h"
#include "sound.h"
#include "ui.h"
#include "voice.h"
#include "hkkeys.h"

int showoptions;
static int optionstab;     //0 controls, 1 video/audio, 2 player
static int capturing=-1;   //binding row waiting for a key
static float volume=1;
static char playername[32];
static int editingname;

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
	{"Push to talk","pushtotalk",&controls.pushtotalk},
	{"Screenshot","screenshot",&controls.screenshot},
};
#define NUMBINDINGS (int)(sizeof(bindings)/sizeof(bindings[0]))

#define NUMSETTINGS 8
static const char *settinglabels[NUMSETTINGS]=
{
	"Fullscreen","VSync","Draw distance","Mouse sensitivity","Invert mouse","Volume",
	"Voice volume","Microphone"
};

//layout, in screen units (-1..1), like the server setup screen (serversetup.cpp)

#define PANELX1 -.95f
#define PANELX2 .95f
#define PANELY1 -.92f
#define PANELY2 .9f
#define TITLEH .13f
#define ROWTOP .72f            //top of the first row
#define ROWHEIGHT .075f
#define LABELX -.85f
#define VALUEX .05f
#define VALUEX2 .85f
#define ARROWW .08f            //"<" and ">" at both ends of a stepped value
#define ROWX1 (LABELX-.04f)    //a row's click and hover area
#define ROWX2 (PANELX2-.04f)
#define TAB_CONTROLS -.5f,.79f,-.22f,.88f
#define TAB_VIDEO -.2f,.79f,.08f,.88f
#define TAB_PLAYER .1f,.79f,.38f,.88f
#define BTN_CLOSE .6f,-.87f,.9f,-.76f

//bottom of a row
static float RowY(int row)
{
	return ROWTOP-(row+1)*ROWHEIGHT;
}

static int RowAt(float y,int numrows)
{
	for(int i=0;i<numrows;i++)
		if(y>=RowY(i)&&y<RowY(i)+ROWHEIGHT)
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
	case KEY_MENU: return "alt";
	case KEY_CONTROL: return "ctrl";
	case KEY_SHIFT: return "shift";
	case KEY_TAB: return "tab";
	case KEY_SPACE: return "space";
	case KEY_LBUTTON: return "lbutton";
	case KEY_RBUTTON: return "rbutton";
	case KEY_UP: return "up";
	case KEY_DOWN: return "down";
	case KEY_LEFT: return "left";
	case KEY_RIGHT: return "right";
	}
	return 0;
}

//replaces "key=value" lines in an ini file of the data folder, or appends them

void SetIniValues(const char *filename,const char *keys[],const char *values[],int count)
{
	long len=0;
	char *data=HKReadFile(filename,&len);
	if(!data)
	{
		data=new char[1];
		data[0]=0;
	}

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
	FILE *file=HKFileOpen(filename,"wb");
	if(file)
	{
		fwrite(out,1,outlen,file);
		fclose(file);
	}
	delete []data;
	delete []out;
	delete []done;
}

static void SaveOptions()
{
	const char *keys[NUMBINDINGS+8];
	const char *values[NUMBINDINGS+8];
	char numbers[8][32];
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
	sprintf(numbers[6],"%d",(int)(VoiceGetVolume()*100+.5f));
	sprintf(numbers[7],"%d",VoiceGetMic());
	const char *numberkeys[8]={"changeres","vsync","drawdistance","mousesensitivity","mousereverse","volume",
		"voicevolume","voicemic"};
	for(int i=0;i<8;i++)
	{
		keys[n]=numberkeys[i];
		values[n]=numbers[i];
		n++;
	}
	SetIniValues(UserFile("control.ini"),keys,values,n);
	for(int i=0;i<firstnumber;i++)
		free((void *)values[i]);

	const char *netkeys[1]={"name"};
	const char *netvalues[1]={playername};
	SetIniValues(UserFile("net.ini"),netkeys,netvalues,1);
}

//reads the settings LoadControls doesn't know about (called at startup)

void LoadOptions()
{
	CfgFile ini;
	if(!ini.Load(UserFile("control.ini"))) return;
	float dd=ini.GetValuef("drawdistance");
	if(dd>0) graphicsprefs.drawdistance=dd;
	if(ini.GetValues("vsync")) HKSetVSync(ini.GetValuei("vsync"));
	if(ini.GetValues("volume")) volume=ini.GetValuei("volume")/100.0f;
	SetMasterVolume(volume);
	if(ini.GetValues("voicevolume")) VoiceSetVolume(ini.GetValuei("voicevolume")/100.0f);
	if(ini.GetValues("voicemic")) VoiceSetMic(ini.GetValuei("voicemic"));
}

void OpenOptions()
{
	showoptions=1;
	capturing=-1;
	editingname=0;
	CfgFile net;
	playername[0]=0;
	if(net.Load(UserFile("net.ini"))&&net.GetValues("name"))
		CopyIniString(playername,net.GetValues("name"),sizeof(playername));
	if(!playername[0]) strcpy(playername,"Player");
}

static void CloseOptions()
{
	showoptions=0;
	capturing=-1;
	editingname=0;
	if(!playername[0]) strcpy(playername,"Player");
	SaveOptions();
}

//copies an ini value (ends at the line end) and keeps printable ASCII only

void CopyIniString(char *dst,const char *src,int size)
{
	int n=0;
	for(;*src&&*src!='\r'&&*src!='\n'&&n<size-1;src++)
		if(*src>=32&&*src<127) dst[n++]=*src;
	dst[n]=0;
}

//appends typed text to a field: printable ASCII only, never '=' (ini separator)

void AppendText(char *field,int size,const char *text)
{
	int len=strlen(field);
	for(;*text&&len<size-1;text++)
	{
		if(*text<32||*text>=127||*text=='=') continue;
		field[len++]=*text;
	}
	field[len]=0;
}

void OptionsHandleText(const char *text)
{
	if(showoptions&&editingname)
		AppendText(playername,sizeof(playername),text);
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
	case 6: VoiceSetVolume(VoiceGetVolume()+dir*.1f); break;
	case 7: VoiceSetMic(!VoiceGetMic()); break;
	}
}

static int IsStepper(int row)
{
	return row==2||row==3||row==5||row==6;
}

static void SettingText(int row,char *text)
{
	switch(row)
	{
	case 0: strcpy(text,HKGetFullscreen()?"On":"Off"); break;
	case 1: strcpy(text,HKGetVSync()?"On":"Off"); break;
	case 2: sprintf(text,"%d",(int)graphicsprefs.drawdistance); break;
	case 3: sprintf(text,"%.2f",controls.mousesensitivity); break;
	case 4: strcpy(text,controls.mousereverse?"On":"Off"); break;
	case 5: sprintf(text,"%d%%",(int)(volume*100+.5f)); break;
	case 6: sprintf(text,"%d%%",(int)(VoiceGetVolume()*100+.5f)); break;
	case 7: strcpy(text,VoiceGetMic()?"On":"Off"); break;
	}
}

//key or mouse button pressed while the screen is open; returns 1 if used

int OptionsHandleKey(int vk)
{
	if(!showoptions) return 0;
	if(capturing>=0)
	{
		if(vk==KEY_ESCAPE)
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
	if(editingname)
	{
		int len=strlen(playername);
		if(vk==KEY_BACK&&len) playername[len-1]=0;
		if(vk==KEY_RETURN||vk==KEY_ESCAPE||vk==KEY_TAB) editingname=0;
		return 1;
	}
	if(vk==KEY_ESCAPE) CloseOptions();
	return 1;
}

void OptionsClick()
{
	if(capturing>=0)
	{
		OptionsHandleKey(KEY_LBUTTON);
		return;
	}
	editingname=0;
	if(Inside(TAB_CONTROLS)) { optionstab=0; return; }
	if(Inside(TAB_VIDEO)) { optionstab=1; return; }
	if(Inside(TAB_PLAYER)) { optionstab=2; return; }
	if(Inside(BTN_CLOSE)) { CloseOptions(); return; }
	if(mousepos.x<ROWX1||mousepos.x>ROWX2) return;
	if(optionstab==2)
	{
		if(RowAt(mousepos.y,1)==0) editingname=1;
		return;
	}
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
			//"<" (and left of it) decreases, anything else increases
			if(mousepos.x<VALUEX+ARROWW) ChangeSetting(row,-1);
			else ChangeSetting(row,1);
		}
		else
			ChangeSetting(row,1);
	}
}

//a tab of the title bar (the join screen's)
static void Tab(float x1,float y1,float x2,float y2,const char *label,int active)
{
	int over=!active&&Inside(x1,y1,x2,y2);
	UIRect(x1,y1,x2,y2,1,1,active?.5f:1,active?.2f:over?.14f:.06f);
	if(active) UIRect(x1,y1,x2,y1+.006f,1,1,.5f,.9f);
	if(active) glColor3f(1,1,.5f);
	else glColor3f(over?1:.7f,over?1:.7f,over?1:.7f);
	UIText((x1+x2-UITextWidth(label))/2,UITextY(y1,y2),label);
}

//label of a row, brighter under the mouse; returns the value's y1,y2
static void RowLabel(int row,const char *label,float &y1,float &y2)
{
	y1=RowY(row)+.008f;
	y2=RowY(row)+ROWHEIGHT-.008f;
	int hover=Inside(ROWX1,RowY(row),ROWX2,RowY(row)+ROWHEIGHT);
	if(hover) UIRect(ROWX1,RowY(row),ROWX2,RowY(row)+ROWHEIGHT,1,1,1,.06f);
	glColor3f(hover?1:.75f,hover?1:.75f,hover?1:.75f);
	UIText(LABELX,UITextY(y1,y2),label);
}

//value box with centered text
static void Value(float y1,float y2,const char *text,int highlight)
{
	UIRect(VALUEX,y1,VALUEX2,y2,0,0,0,.55f);
	if(highlight) glColor3f(1,1,.5f);
	else glColor3f(1,1,1);
	UIText((VALUEX+VALUEX2-UITextWidth(text))/2,UITextY(y1,y2),text);
}

void DrawOptions()
{
	if(!showoptions) return;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	UIPanel(PANELX1,PANELY1,PANELX2,PANELY2,TITLEH,"Options");
	Tab(TAB_CONTROLS,"Controls",optionstab==0);
	Tab(TAB_VIDEO,"Video/Audio",optionstab==1);
	Tab(TAB_PLAYER,"Player",optionstab==2);

	char text[100];
	float y1,y2;
	const char *help;
	if(optionstab==2)
	{
		RowLabel(0,"Name",y1,y2);
		UIField(VALUEX,y1,VALUEX2,y2,playername,editingname,"Player");
		help="Click the name to edit it";
	}
	else if(optionstab==0)
	{
		for(int i=0;i<NUMBINDINGS;i++)
		{
			RowLabel(i,bindings[i].label,y1,y2);
			const char *name=KeyName(*bindings[i].key);
			Value(y1,y2,i==capturing?"press a key...":name?name:"(none)",i==capturing);
		}
		help="Click an action, then a key (Esc: cancel)";
	}
	else
	{
		for(int i=0;i<NUMSETTINGS;i++)
		{
			RowLabel(i,settinglabels[i],y1,y2);
			SettingText(i,text);
			Value(y1,y2,text,0);
			if(IsStepper(i))
			{
				UIArrow(VALUEX,y1,VALUEX+ARROWW,y2,"<");
				UIArrow(VALUEX2-ARROWW,y1,VALUEX2,y2,">");
			}
		}
		help="Click a value to change it; \"<\" lowers it";
	}
	glColor3f(.6f,.6f,.6f);
	UITextFit(LABELX,UITextY(-.72f,-.64f,.04f),help,PANELX2-.05f-LABELX,.04f);
	glColor3f(.45f,.45f,.45f);
	UIText(LABELX,UITextY(-.87f,-.76f,.04f),"Esc: close (settings are saved)",.04f);
	UIButton(BTN_CLOSE,"Close",1);
	glColor3f(1,1,1);
}

