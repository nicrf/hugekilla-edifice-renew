

#include "gl.h"
#include "renderer.h"
#include "platform.h"
#include "fileio.h"
#include "cfgfile.h"
#include "hugekilla.h"
#include "gameplay.h"
#include "redrawing.h"
#include "stdio.h"
#include "multiplay.h"
#include "ui.h"
#include "hkkeys.h"
#include <ctype.h>

char consoletext[100][500];
char consolecommand[500];
char prevconsolecommand[40][500];
int curprevconsolecommand=-1;
int consolevisible;
DWORD consolescrolltime;

char consolevar[100][40];
void *consolevarptr[100];
int consolevartype[100];
char consolevarserver[100];
int numconsolevar;
int consolepartvisible;

//line colors and scrollback for DrawConsolePanel (below)
#define CONSOLELINES 100
#define CONSOLEMAXINPUT 250
enum { KIND_TEXT, KIND_ECHO, KIND_ERROR, KIND_VALUE, KIND_DIM };
static unsigned char consolekind[CONSOLELINES];
static int consolecount;            //lines used in consoletext (newest first)
static int consolescroll;           //rows scrolled back from the newest
static int consolecursor;           //insertion point in consolecommand
static int nextconsolekind=-1;      //kind of the next ConsoleDisplayText line, if forced
int consoleoverlay;                 //the game frame draws the console: no extra frame per line


void ScrollConsoleText()
{
	for(int i=99;i>0;i--)
	{
		strcpy(consoletext[i],consoletext[i-1]);
		consolekind[i]=consolekind[i-1];
	}
	consoletext[0][0]=0;
	consolekind[0]=KIND_TEXT;
	if(consolecount<CONSOLELINES) consolecount++;
	if(consolescroll) consolescroll++;   //keeps the lines being read in place
}

//errors show in red, texture loading is dimmed
static int ConsoleLineKind(const char *text)
{
	static const char *errors[]={"usage:","error","fail","cannot","can't","only server","only the server","not found","invalid","refused","timed out","no land"};
	char low[160];
	int n;
	for(n=0;text[n]&&n<159;n++) low[n]=tolower((unsigned char)text[n]);
	low[n]=0;
	for(int i=0;i<(int)(sizeof(errors)/sizeof(errors[0]));i++)
		if(strstr(low,errors[i])) return KIND_ERROR;
	if(!strncmp(low,"loaded ",7)) return KIND_DIM;
	return KIND_TEXT;
}

static void SetLineKind()
{
	consolekind[0]=nextconsolekind>=0?nextconsolekind:ConsoleLineKind(consoletext[0]);
}

void ConsoleDisplayText(char *text)
{
	static int log=getenv("HK_CONSOLE_LOG")!=0;   //debug aid: the console on stderr (tests)
	if(log) fprintf(stderr,"console: %s\n",text);
	int len=strlen(text);
	char *curtext=text;
	for(int i=0;i<len;i++)
	{
		if(text[i]=='\r')
		{
			if(consolepartvisible<6)consolepartvisible++;
			consolescrolltime=curtime+3000;
	
			text[i]=0;
			ScrollConsoleText();
			strcpy(consoletext[0],curtext);
			SetLineKind();
			curtext=&text[i+1];
		}
	}
	if(consolepartvisible<6)consolepartvisible++;
	consolescrolltime=curtime+3000;
	
	ScrollConsoleText();
	strcpy(consoletext[0],curtext);
	SetLineKind();
	nextconsolekind=-1;
			
	if(consolevisible&&!consoleoverlay) DrawConsole();

}

//savemap <name>, loadmap <name> (maps/<name>.map in the user folder), maps

static int IsCommand(const char *name)
{
	int len=strlen(name);
	return strncmp(consolecommand,name,len)==0&&(consolecommand[len]==' '||consolecommand[len]==0);
}

static void MapCommand(int save)
{
	char name[100];
	const char *arg=&consolecommand[7];
	while(*arg==' ') arg++;
	snprintf(name,sizeof(name),"%s",arg);
	int len=strlen(name);
	while(len&&name[len-1]==' ') name[--len]=0;
	char text[1500];
	if(!ValidMapName(name))
	{
		snprintf(text,sizeof(text),"usage: %s <name> (letters, digits, _ and -, at most %d)",save?"savemap":"loadmap",MAXMAPNAME);
		ConsoleDisplayText(text);
		return;
	}
	char err[1400];
	if(save)
	{
		if(!IsServer()&&!IsConnected())
		{
			ConsoleDisplayText("No land to save: start or join a game first");
			return;
		}
		HKMakeDir(UserFile("maps"));
		if(SaveMapPath(MapPath(name),err,sizeof(err)))
			snprintf(text,sizeof(text),"Saved map %s",MapPath(name));
		else
			snprintf(text,sizeof(text),"%s",err);
		ConsoleDisplayText(text);
	}
	else if(!LoadMapPath(MapPath(name),err,sizeof(err)))
		ConsoleDisplayText(err);
}

static void ListMapsCommand()
{
	static char names[64][MAXMAPNAME+1];
	int n=ListSavedMaps(names,64);
	char text[200];
	snprintf(text,sizeof(text),n?"%d saved map(s), loadmap <name>:":"No saved map (savemap <name>)",n);
	ConsoleDisplayText(text);
	for(int i=0;i<n;i++) ConsoleDisplayText(names[i]);
}

//commands for help and Tab completion (console variables are completed too)
static const char *commandhelp[][2]=
{
	{"help","this list"},
	{"clear","empty the console"},
	{"maps","list the saved maps"},
	{"savemap <name>","save the land to maps/<name>.map"},
	{"loadmap <name>","play on a saved map (starts a server)"},
	{"say <text>","chat to everyone"},
	{"team <0-2>","change team"},
	{"die","respawn"},
	{"givemoney","+1000 money"},
	{"createdome","dome where you stand"},
	{"kick <n>","server: remove player n"},
	{"credit <n>","server: give credit n to everyone"},
	{"resetgame","server: new round"},
	{"scorereset","server: reset the scores"},
	{"createplayer","server: add a dummy player"},
	{"moveunnecessarytextures","move unused textures away"},
};
#define NUMCOMMANDS (int)(sizeof(commandhelp)/sizeof(commandhelp[0]))

static void HelpCommand()
{
	char text[200];
	for(int i=0;i<NUMCOMMANDS;i++)
	{
		snprintf(text,sizeof(text),"  %-24s %s",commandhelp[i][0],commandhelp[i][1]);
		ConsoleDisplayText(text);
	}
	snprintf(text,sizeof(text),"%d variables: <name> shows one, <name> <value> sets it. Tab completes, PgUp/PgDn scroll.",numconsolevar);
	ConsoleDisplayText(text);
}

static void ClearCommand()
{
	for(int i=0;i<CONSOLELINES;i++) consoletext[i][0]=0;
	consolecount=0;
	consolescroll=0;
	consolepartvisible=0;
}

void ConsoleCommand()
{
	if(strlen(consolecommand)==0) return;
	consolescroll=0;
	nextconsolekind=KIND_ECHO;
	ConsoleDisplayText(consolecommand);
	int i;
	for(i=38;i>=0;i--)
	{
		strcpy(prevconsolecommand[i+1],prevconsolecommand[i]);
	}
	strcpy(prevconsolecommand[0],consolecommand);
	
	if(strcmp(consolecommand,"moveunnecessarytextures")==0)
	{
		MoveUnnecessaryTextures();
	}
	if(strncmp(consolecommand,"team",4)==0)
	{
		 ChangeTeam(GetLocalNum(),atoi(&consolecommand[4]));
	}
	if(strcmp(consolecommand,"createdome")==0)
	{
		 CreateInstallation(GetLocalPlayer()->pos,0);
	}
	if(strcmp(consolecommand,"resetgame")==0&&server)
	{
		GReset();
	}
	if(strcmp(consolecommand,"die")==0)
	{
		GetLocalPlayer()->life=-1;
		Killed(GetLocalNum());
	}
	if(strcmp(consolecommand,"scorereset")==0)
	{
		if(server) ScoreReset();
	}
	if(strcmp(consolecommand,"givemoney")==0)
	{
		GetLocalPlayer()->money+=1000;
	}

	if(strncmp(consolecommand,"kick",4)==0&&IsServer())
	{
		int num=atoi(&consolecommand[4]);
		if(num>0&&num<numplayers)
		{
			DeletePlayer(num,2);
		}
	}
	if(strncmp(consolecommand,"credit",6)==0&&IsServer())
	{
		int num=atoi(&consolecommand[6]);
		GiveCredit(-1,num,1);
	}
	
	if(strcmp(consolecommand,"createplayer")==0&&server)  //dummy player
	{
		Player *cur=GetPlayer(numplayers);
		cur->playstate=10;
		cur->timeout=curtime+10000000;
		cur->pos=GetLocalPlayer()->pos;
		strcpy(cur->name,"Player");
		cur->hanggliding=1;
		cur->hangglide=1;
		cur->jetpack=1;
		cur->tilt=GetLocalPlayer()->tilt;
		numplayers++;
	}
	if(IsCommand("savemap")) MapCommand(1);
	if(IsCommand("loadmap")) MapCommand(0);
	if(IsCommand("maps")) ListMapsCommand();
	if(IsCommand("help")) HelpCommand();
	if(IsCommand("clear")) ClearCommand();
	if(strncmp(consolecommand,"say",3)==0)
	{
		if(strlen(consolecommand)>4)
		{
			SendChatMessage(&consolecommand[4],0);
		}
	}
		
	char var[40];
	char *val=0;
	int len=strlen(consolecommand);
	for(i=0;i<len;i++)
	{
		if(consolecommand[i]==' '||consolecommand[i]==0) break;
		var[i]=consolecommand[i];
	}
	var[i]=0;
	if(consolecommand[i]!=0) val=&consolecommand[i+1];
	for(i=0;i<numconsolevar;i++)
	{
		if(strcmp(var,consolevar[i])==0)
		{
			if(val)
			{
				if(consolevarserver[i]&&!server)
				{
					ConsoleDisplayText("Only server can change value");
				}
				else
				{
					switch (consolevartype[i])
					{
					case 1:
						*(float *)consolevarptr[i]=atof(val);
						break;
					case 2:
						*(int *)consolevarptr[i]=atoi(val);
						break;
					case 3:
						strcpy((char *)consolevarptr[i],val);
						break;
					}
					ScrollConsoleText();
					consolekind[0]=KIND_VALUE;
					strcat(consoletext[0],var);
					strcat(consoletext[0]," changed to ");
					strcat(consoletext[0],val);
					SendServerVars(0);
				}
			}
			else
			{
				ScrollConsoleText();
				consolekind[0]=KIND_VALUE;
				strcat(consoletext[0],var);
				strcat(consoletext[0]," is ");
				switch (consolevartype[i])
				{
				case 1:
					{
						sprintf(&consoletext[0][strlen(consoletext[0])],"%f",*(float *)consolevarptr[i]);
					}
					break;
				case 2:
					sprintf(&consoletext[0][strlen(consoletext[0])],"%i",*(int *)consolevarptr[i]);
					break;
				case 3:
					strcpy(&consoletext[0][strlen(consoletext[0])],(char *)consolevarptr[i]);
					break;
				}
				
			}
		}
	}
	consolecommand[0]=0;
}
char GetRealKey(DWORD key,DWORD flags)
{
	if((key>='0'&&key<='z')||key==' '||key>=180||key==191)
	{
		if(key==190)key='.';
		if(key==188)key=',';
		if(key==191)key='?';
		if(key==222)key='\'';
		key&=~128;
		if(key=='=')key='-';
		if(key==';')key='=';
		if(key=='?')key='/';
		if(key==':')key=';';
		if(!HKKeyDown(KEY_SHIFT)&&(key>='A'&&key<='Z')) key+=32;
		if(HKKeyDown(KEY_SHIFT)) 
		{
			switch (key)
			{
			case '1': key='!'; break;
			case '2': key='@'; break;
			case '3': key='#'; break;
			case '4': key='$'; break;
			case '5': key='%'; break;
			case '6': key='^'; break;
			case '7': key='&'; break;
			case '8': key='*'; break;
			case '9': key='('; break;
			case '0': key=')'; break;
			case '-': key='_'; break;
			case '=': key='+'; break;
			case '[': key='{'; break;
			case ']': key='}'; break;
			case ';': key=':'; break;
			case '\'': key='"'; break;
			case ',': key='<'; break;
			case '.': key='>'; break;
			case '/': key='?'; break;
			case '\\': key='|'; break;
			}
		}
		return key;
	}
	return 0;
	
}

//Tab completion: names starting with what is typed. The first word completes to a
//command or console variable, the argument of loadmap to a saved map.
#define MAXMATCHES 64
static char matchnames[MAXMATCHES][40];
static int nummatches;
static int matchstart;              //where the completed word starts in consolecommand

static void AddMatch(const char *name,const char *prefix,int len)
{
	if(nummatches>=MAXMATCHES||strncmp(name,prefix,len)) return;
	for(int i=0;i<nummatches;i++)
		if(!strcmp(matchnames[i],name)) return;
	snprintf(matchnames[nummatches++],40,"%s",name);
}

static void FindMatches()
{
	nummatches=0;
	const char *space=strchr(consolecommand,' ');
	if(!space)
	{
		matchstart=0;
		int len=strlen(consolecommand);
		if(!len) return;
		for(int i=0;i<NUMCOMMANDS;i++)
		{
			char name[40];
			snprintf(name,sizeof(name),"%s",commandhelp[i][0]);
			char *end=strchr(name,' ');
			if(end) *end=0;
			AddMatch(name,consolecommand,len);
		}
		for(int i=0;i<numconsolevar;i++) AddMatch(consolevar[i],consolecommand,len);
		return;
	}
	if(!IsCommand("loadmap")) return;
	const char *arg=space+1;
	while(*arg==' ') arg++;
	matchstart=arg-consolecommand;
	static char names[64][MAXMAPNAME+1];
	int n=ListSavedMaps(names,64);
	for(int i=0;i<n;i++) AddMatch(names[i],arg,strlen(arg));
}

static void CompleteCommand()
{
	FindMatches();
	if(!nummatches) return;
	//longest common prefix of the matches; a single match also gets a space
	int len=strlen(matchnames[0]);
	for(int i=1;i<nummatches;i++)
	{
		int l=0;
		while(l<len&&matchnames[i][l]==matchnames[0][l]) l++;
		len=l;
	}
	if(matchstart+len+2>=CONSOLEMAXINPUT) return;
	memcpy(&consolecommand[matchstart],matchnames[0],len);
	consolecommand[matchstart+len]=0;
	if(nummatches==1&&matchstart==0) strcat(consolecommand," ");
	consolecursor=strlen(consolecommand);
}

static void InsertConsoleChar(char c)
{
	int len=strlen(consolecommand);
	if(len>=CONSOLEMAXINPUT) return;
	if(consolecursor>len) consolecursor=len;
	memmove(&consolecommand[consolecursor+1],&consolecommand[consolecursor],len-consolecursor+1);
	consolecommand[consolecursor++]=c;
}

//text typed in the console (SDL text input, so it follows the keyboard layout);
//accented letters are dropped since the game font is ASCII
void ConsoleHandleText(const char *utf8)
{
	if(!consolevisible||GetLocalPlayer()->playstate==0) return;
	for(const unsigned char *p=(const unsigned char *)utf8;*p;p++)
	{
		if(*p<32||*p>126||*p=='`'||*p=='~') continue;   //` and ~ toggle the console
		InsertConsoleChar(*p);
	}
	consolescroll=0;
}

//runs a command as if typed (debug hooks)
void ConsoleExecute(const char *cmd)
{
	snprintf(consolecommand,CONSOLEMAXINPUT+1,"%s",cmd);
	ConsoleCommand();
	consolecursor=0;
}

void ConsoleSetInput(const char *text)
{
	snprintf(consolecommand,CONSOLEMAXINPUT+1,"%s",text);
	consolecursor=strlen(consolecommand);
}

void ConsoleScroll(int rows)
{
	consolescroll+=rows;
	if(consolescroll<0) consolescroll=0;   //DrawConsolePanel keeps it under the top
}

int HandleConsoleKey(DWORD key,DWORD flags)
{
	int len=strlen(consolecommand);
	if(consolecursor>len) consolecursor=len;
	if(key==KEY_TAB)
	{
		CompleteCommand();
	}
	if(key==KEY_UP&&curprevconsolecommand<39&&prevconsolecommand[curprevconsolecommand+1][0])
	{
		curprevconsolecommand++;
		strcpy(consolecommand,prevconsolecommand[curprevconsolecommand]);
		consolecursor=strlen(consolecommand);
	}
	if(key==KEY_DOWN)
	if(curprevconsolecommand>=0)
	{
		curprevconsolecommand--;
		if(curprevconsolecommand>=0)
			strcpy(consolecommand,prevconsolecommand[curprevconsolecommand]);
		else
			consolecommand[0]=0;
		consolecursor=strlen(consolecommand);
	}

	if(key==KEY_BACK&&consolecursor>0)
	{
		memmove(&consolecommand[consolecursor-1],&consolecommand[consolecursor],len-consolecursor+1);
		consolecursor--;
	}
	if(key==KEY_DELETE&&consolecursor<len)
		memmove(&consolecommand[consolecursor],&consolecommand[consolecursor+1],len-consolecursor);
	if(key==KEY_LEFT&&consolecursor>0) consolecursor--;
	if(key==KEY_RIGHT&&consolecursor<len) consolecursor++;
	if(key==KEY_HOME) consolecursor=0;
	if(key==KEY_END) consolecursor=len;
	if(key==KEY_PRIOR) ConsoleScroll(10);
	if(key==KEY_NEXT) ConsoleScroll(-10);
	if(key==KEY_RETURN)
	{
		curprevconsolecommand=-1;
		ConsoleCommand();
		consolecursor=0;
	}
	return 1;

}

void AddConsoleVar(void *var,char *name,int type,char servervar)
{
	int i;
	for(i=0;i<numconsolevar;i++)
	{
		if(strcmp(consolevar[i],name)==0)
		{
			break;
		}
	}
	if(i!=numconsolevar) return;
	strcpy(consolevar[numconsolevar],name);
	consolevarptr[numconsolevar]=var;
	consolevartype[numconsolevar]=type;
	consolevarserver[numconsolevar]=servervar;
	numconsolevar++;
}

void AddConsoleVar(float *var,char *name,char servervar)
{
	AddConsoleVar(var,name,1,servervar);
}

void AddConsoleVar(int *var,char *name,char servervar)
{
	AddConsoleVar(var,name,2,servervar);
}

void AddConsoleVar(char *var,char *name,char servervar)
{
	AddConsoleVar(var,name,3,servervar);
}

void InitConsole()
{
	/*physics.walkaccelerate=.2;
	physics.strafeaccelerate=.15;
	physics.resistance=1.3;
	physics.stopresistance=1.6;*/
	graphicsprefs.drawdistance=57;
	graphicsprefs.mipdist[0]=0;
	graphicsprefs.mipdist[1]=4;
	graphicsprefs.mipdist[2]=8;
	graphicsprefs.mipdist[3]=8;
	graphicsprefs.mipdist[4]=4;
	
	AddConsoleVar(&sv.walkaccelerate,"walkaccelerate",1);
	AddConsoleVar(&sv.strafeaccelerate,"strafeaccelerate",1);
	AddConsoleVar(&sv.resistance,"resistance",1);
	AddConsoleVar(&sv.stopresistance,"stopresistance",1);
	AddConsoleVar(&sv.gatspeed,"gatspeed",1);
	AddConsoleVar(&sv.gattimeout,"gattimeout",1);
	AddConsoleVar(&sv.gatammohold,"gatammohold",1);
	AddConsoleVar(&sv.gatreloadtime,"gatreloadtime",1);
	AddConsoleVar(&sv.gravity,"gravity",1);
	AddConsoleVar(&sv.grenadedamage,"grenadedamage",1);
	AddConsoleVar(&sv.grenadeexplodetime,"grenadeexplodetime",1);
	AddConsoleVar(&sv.grenadespeed,"grenadespeed",1);
	AddConsoleVar(&sv.grenadetimeout,"grenadetimeout",1);
	AddConsoleVar(&sv.grenadeammohold,"grenadeammohold",1);
	AddConsoleVar(&sv.grenadereloadtime,"grenadereloadtime",1);
	AddConsoleVar(&sv.highbulletdamage,"highbulletdamage",1);
	AddConsoleVar(&sv.lowbulletdamage,"lowbulletdamage",1);
	AddConsoleVar(&sv.m16speed,"m16speed",1);
	AddConsoleVar(&sv.m16timeout,"m16timeout",1);
	AddConsoleVar(&sv.m16ammohold,"m16ammohold",1);
	AddConsoleVar(&sv.m16reloadtime,"m16reloadtime",1);
	AddConsoleVar(&sv.steyrspeed,"steyrspeed",1);
	AddConsoleVar(&sv.steyrtimeout,"steyrtimeout",1);
	AddConsoleVar(&sv.steyrammohold,"steyrammohold",1);
	AddConsoleVar(&sv.steyrreloadtime,"steyrreloadtime",1);
	AddConsoleVar(&sv.umpspeed,"umpspeed",1);
	AddConsoleVar(&sv.umptimeout,"umptimeout",1);
	AddConsoleVar(&sv.umpammohold,"umpammohold",1);
	AddConsoleVar(&sv.umpreloadtime,"umpreloadtime",1);
	AddConsoleVar(&sv.berettaspeed,"berettaspeed",1);
	AddConsoleVar(&sv.berettatimeout,"berettatimeout",1);
	AddConsoleVar(&sv.berettaammohold,"berettaammohold",1);
	AddConsoleVar(&sv.berettareloadtime,"berettareloadtime",1);
	AddConsoleVar(&sv.sr1000speed,"sr1000speed",1);
	AddConsoleVar(&sv.sr1000timeout,"sr1000timeout",1);
	AddConsoleVar(&sv.sr1000ammohold,"sr1000ammohold",1);
	AddConsoleVar(&sv.sr1000reloadtime,"sr1000reloadtime",1);
	
	AddConsoleVar(&sv.scale,"scale",1);
	AddConsoleVar(&sv.positionresendtime,"positionresendtime",1);

	AddConsoleVar(&sv.radarenable,"radarenable",1);
	AddConsoleVar(&sv.chopperthrust,"chopperthrust",1);
	AddConsoleVar(&sv.thrustrechargerate,"thrustrechargerate",1);
	AddConsoleVar(&sv.thrustuse,"thrustuse",1);

	AddConsoleVar(&sv.gamemode,"gamemode",1);

	AddConsoleVar(&sv.timelimit,"timelimit",1);
	AddConsoleVar(&sv.capturelimit,"capturelimit",1);
	CfgFile svini;
	svini.Load(UserFile("sv.ini"));
	for(int i=0;i<numconsolevar;i++)
	{
		switch(consolevartype[i])
		{
		case 1:
			*(float *)consolevarptr[i]=svini.GetValuef(consolevar[i]);
			break;
		case 2:
			*(int *)consolevarptr[i]=svini.GetValuef(consolevar[i]);
			break;
		}
	}
	AddConsoleVar(&graphicsprefs.drawdistance,"drawdistance",0);

	AddConsoleVar(&graphicsprefs.stereoseparation,"stereoseperation",0);

}

//console panel: a translucent band over the top of the game (or the whole black screen
//while loading), small text with colored lines, a scrollbar, the matches for Tab above
//the prompt. The original drew 15 full size lines on a black screen.
#define CONQUADW .0248f         //glyph cell; the HUD font is .04 x .07
#define CONGLYPHH .0434f
#define CONCHARW .0165f         //advance: tighter than the cell, whose glyph is narrow
#define CONLINEH .047f
#define CONBOTTOM -.2f          //panel bottom over the game
#define CONHEADH .06f
#define CONINPUTH .065f
#define CONX1 -.985f            //text left edge
#define CONSCROLLW .012f

//draws len characters (all if -1) with the font of DrawText (TEX_FONT, 16x16
//cells from ' '), overlapping the cells so the text is dense
static void ConsoleText(float x,float y,const char *text,int len=-1)
{
	if(len<0||len>(int)strlen(text)) len=strlen(text);
	const float cell=1.f/16;
	TexBind(TEX_FONT);
	glBegin(GL_QUADS);
	for(int i=0;i<len;i++,x+=CONCHARW)
	{
		int c=(unsigned char)text[i]-32;
		if(c<=0||c>=96) continue;
		float u=(c%16)*cell,v=(c/16)*cell;
		float x1=x-(CONQUADW-CONCHARW)/2;
		glTexCoord2f(u,v);
		glVertex2f(x1,y+CONGLYPHH);
		glTexCoord2f(u,v+cell);
		glVertex2f(x1,y);
		glTexCoord2f(u+cell,v+cell);
		glVertex2f(x1+CONQUADW,y);
		glTexCoord2f(u+cell,v);
		glVertex2f(x1+CONQUADW,y+CONGLYPHH);
	}
	glEnd();
}

static void ConsoleRect(float x1,float y1,float x2,float y2,float r,float g,float b,float a)
{
	glDisable(GL_TEXTURE_2D);
	glColor4f(r,g,b,a);
	glBegin(GL_QUADS);
	glVertex2f(x1,y1);
	glVertex2f(x2,y1);
	glVertex2f(x2,y2);
	glVertex2f(x1,y2);
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

static void KindColor(int kind,float alpha)
{
	if(kind==KIND_ECHO) glColor4f(1,1,.5f,alpha);
	else if(kind==KIND_ERROR) glColor4f(1,.45f,.4f,alpha);
	else if(kind==KIND_VALUE) glColor4f(.55f,.8f,1,alpha);
	else if(kind==KIND_DIM) glColor4f(.55f,.55f,.55f,alpha);
	else glColor4f(.9f,.9f,.9f,alpha);
}

//one screen row of a console line (long lines wrap)
struct ConsoleRow
{
	short line,start,len;
};

//splits consoletext[line] into rows of at most width characters, at a space when there
//is one in the second half; adds them last row first (rows are counted from the bottom)
static int WrapConsoleLine(int line,int width,ConsoleRow *rows,int count,int max)
{
	const char *text=consoletext[line];
	int len=strlen(text);
	int starts[64],lens[64];
	int n=0;
	int pos=0;
	do
	{
		int take=len-pos;
		if(take>width)
		{
			take=width;
			for(int i=width;i>width/2;i--)
				if(text[pos+i]==' ')
				{
					take=i;
					break;
				}
		}
		starts[n]=pos;
		lens[n]=take;
		n++;
		pos+=take;
		while(text[pos]==' ') pos++;
	}
	while(pos<len&&n<64);
	for(int i=n-1;i>=0&&count<max;i--)
	{
		rows[count].line=line;
		rows[count].start=starts[i];
		rows[count].len=lens[i];
		count++;
	}
	return count;
}

void DrawConsolePanel(int fullscreen)
{
	float top=1;
	float bottom=fullscreen?-1:CONBOTTOM;
	float x2=fullscreen?1:.985f;
	int cols=(int)((x2-CONSCROLLW-.01f-CONX1)/CONCHARW);
	int width=cols-2;           //2 columns of gutter for the > of commands
	consoleoverlay=!fullscreen;

	Mat4 projection=RProjection(),modelview=RModelView();   //over the game: back to them after
	RSet2D();
	glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT|GL_TEXTURE_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_FOG);
	glDisable(GL_LIGHTING);
	glDisable(GL_CULL_FACE);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
	glEnable(GL_TEXTURE_2D);

	//panel, title bar, input bar
	ConsoleRect(-1,bottom,1,top,0,0,0,fullscreen?1:.84f);
	ConsoleRect(-1,top-CONHEADH,1,top,0,0,0,.35f);
	ConsoleRect(-1,top-CONHEADH-.004f,1,top-CONHEADH,1,1,1,.18f);
	ConsoleRect(-1,bottom,1,bottom+CONINPUTH,0,0,0,.4f);
	ConsoleRect(-1,bottom+CONINPUTH,1,bottom+CONINPUTH+.004f,1,1,1,.18f);
	if(!fullscreen) ConsoleRect(-1,bottom-.004f,1,bottom,1,1,.5f,.45f);

	char text[200];
	float texty=top-CONHEADH+(CONHEADH-CONGLYPHH)/2;
	glColor4f(1,1,.5f,1);
	ConsoleText(CONX1,texty,"Console");
	glColor4f(.5f,.5f,.5f,1);
	snprintf(text,sizeof(text),"Edifice %s",EDIFICE_VERSION_STRING);
	ConsoleText(CONX1+9*CONCHARW,texty,text);
	if(!fullscreen)
	{
		const char *keys="Tab complete   PgUp/PgDn scroll   Up/Down history   ` close";
		ConsoleText(x2-strlen(keys)*CONCHARW,texty,keys);
	}

	//matches for Tab, found again only when the typed text changes
	static char matchedtext[CONSOLEMAXINPUT+1]="\1";
	if(strcmp(matchedtext,consolecommand))
	{
		FindMatches();
		snprintf(matchedtext,sizeof(matchedtext),"%s",consolecommand);
	}
	int typedlen=strlen(&consolecommand[matchstart]);
	int showmatches=nummatches>1||(nummatches==1&&strcmp(matchnames[0],&consolecommand[matchstart]));

	//log rows, newest at the bottom
	float logbottom=bottom+CONINPUTH+.012f;
	if(showmatches) logbottom+=CONLINEH;
	float logtop=top-CONHEADH-.012f;
	int visible=(int)((logtop-logbottom)/CONLINEH);
	static ConsoleRow rows[CONSOLELINES*8];
	int numrows=0;
	for(int i=0;i<consolecount&&numrows<CONSOLELINES*8;i++)
		numrows=WrapConsoleLine(i,width,rows,numrows,CONSOLELINES*8);
	//scrolled back, the bottom row tells how much is below
	int maxscroll=numrows>visible?numrows-visible+1:0;
	if(consolescroll>maxscroll) consolescroll=maxscroll;
	int r0=consolescroll>0;
	for(int r=r0;r<visible&&consolescroll+r-r0<numrows;r++)
	{
		const ConsoleRow &row=rows[consolescroll+r-r0];
		int kind=consolekind[row.line];
		float y=logbottom+r*CONLINEH;
		if(kind==KIND_ECHO&&row.start==0)
		{
			glColor4f(1,1,.5f,1);
			ConsoleText(CONX1,y,">");
		}
		KindColor(kind,1);
		ConsoleText(CONX1+2*CONCHARW,y,&consoletext[row.line][row.start],row.len);
	}
	if(consolescroll>0)
	{
		ConsoleRect(-1,logbottom-.006f,x2-CONSCROLLW-.01f,logbottom+CONLINEH-.006f,.25f,.25f,.1f,.9f);
		snprintf(text,sizeof(text),"v  %d more row%s below  (PgDn)",consolescroll,consolescroll==1?"":"s");
		glColor4f(1,1,.5f,1);
		ConsoleText(CONX1+2*CONCHARW,logbottom,text);
	}
	if(numrows>visible)
	{
		//scrollbar: the thumb is the part on screen
		float x=x2-CONSCROLLW;
		float h=logtop-logbottom;
		float thumb=h*visible/numrows;
		if(thumb<.03f) thumb=.03f;
		float y=logbottom+(h-thumb)*consolescroll/maxscroll;
		ConsoleRect(x,logbottom,x2,logtop,1,1,1,.08f);
		ConsoleRect(x,y,x2,y+thumb,1,1,.5f,.6f);
	}

	if(showmatches)
	{
		//Tab completes the part shared by all the matches
		float y=bottom+CONINPUTH+.012f;
		float x=CONX1+2*CONCHARW;
		float right=x2-CONSCROLLW-.02f;
		glColor4f(.5f,.5f,.5f,1);
		ConsoleText(CONX1,y,"Tab");
		x+=2*CONCHARW;
		for(int i=0;i<nummatches;i++)
		{
			int len=strlen(matchnames[i]);
			if(x+(len+8)*CONCHARW>right&&i<nummatches-1)
			{
				snprintf(text,sizeof(text),"+%d more",nummatches-i);
				glColor4f(.5f,.5f,.5f,1);
				ConsoleText(x,y,text);
				break;
			}
			glColor4f(1,1,.5f,1);
			ConsoleText(x,y,matchnames[i],typedlen);
			glColor4f(.75f,.75f,.75f,1);
			ConsoleText(x+typedlen*CONCHARW,y,&matchnames[i][typedlen]);
			x+=(len+2)*CONCHARW;
		}
	}

	//prompt; a long command scrolls to keep the cursor in view
	float y=bottom+(CONINPUTH-CONGLYPHH)/2;
	glColor4f(1,1,.5f,1);
	ConsoleText(CONX1,y,">");
	int len=strlen(consolecommand);
	if(consolecursor>len) consolecursor=len;
	int room=cols-3;
	int first=consolecursor>room?consolecursor-room:0;
	float x=CONX1+2*CONCHARW;
	if(!fullscreen&&!((curtime/500)&1))
		ConsoleRect(x+(consolecursor-first)*CONCHARW,y-.004f,x+(consolecursor-first+1)*CONCHARW,y+CONGLYPHH,1,1,.5f,.55f);
	glColor4f(1,1,1,1);
	ConsoleText(x,y,&consolecommand[first],room);

	glPopAttrib();
	RSetProjection(projection);
	RSetModelView(modelview);
	glColor3f(1,1,1);
}

//the last few lines over the game for a few seconds (console closed)
void DrawConsoleNotify()
{
	consoleoverlay=0;
	if(consolepartvisible<=0) return;
	glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	for(int i=0;i<consolepartvisible&&i<consolecount;i++)
	{
		float y=.985f-CONGLYPHH-(consolepartvisible-1-i)*CONLINEH;
		glColor4f(0,0,0,.8f);   //shadow: readable on snow too
		ConsoleText(-.84f+.003f,y-.004f,consoletext[i],120);
		KindColor(consolekind[i],1);
		ConsoleText(-.84f,y,consoletext[i],120);
	}
	glPopAttrib();
	glColor3f(1,1,1);
}
