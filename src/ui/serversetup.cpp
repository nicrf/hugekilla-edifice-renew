//server setup screen, shown by the main menu "Server" button before the server starts:
//server name, game mode, capture and time limits, sky, terrain and scenery, each of the
//world settings having a "Random" choice, and the map: a new random land or one saved
//with the console's savemap (maps/<name>.map in the user folder, which then gives the
//sky, terrain, scenery and buildings), and whether the game is announced on the
//Internet list (edifice-lobby, lobbygame.cpp). Choices are kept in sv.ini and net.ini.

#ifndef HK_WIN32_NATIVE

#include "StdAfx.h"
#include "redrawing.h"
#include "playing.h"
#include "multiplay.h"
#include "ui.h"
#include "lobbygame.h"

extern Vector mousepos;
extern ServerVars sv;
extern int hkforcedsky;
extern float hkreliefscale;
extern int hksceneryoverride;

#define MAXSKIES 16
#define MAXMAPS 64

static int showsetup;
static char servername[40];
static int editingname;
static int skychoice=-1;      //-1 random, else index in skies.ini
static int terrainchoice=-1;  //-1 random, else index in terrains
static int scenerychoice=-1;  //-1 random, else index in sceneries
static int numskies;
static char skynames[MAXSKIES][40];
static int mapchoice=-1;      //-1 new random land, else index in mapnames
static int nummaps;
static char mapnames[MAXMAPS][MAXMAPNAME+1];
static char setuperror[200];  //why the chosen map couldn't be loaded
static int visibility;        //index in visibilities
static int lobbyok;           //a lobby is configured (net.ini lobby=)

static const char *terrainnames[]={"Flat","Hills","Mountains"};
static const float terrainscales[]={.35f,1,1.8f};
#define NUMTERRAINS 3
static const char *scenerynames[]={"None","Few","Normal","Lots"};
static const int scenerycounts[]={0,4,10,25};
#define NUMSCENERIES 4
//net.ini servervisibility: lan, or announced to the lobby (lobbygame.h)
static const char *visibilities[]={"lan","public","friends","private"};
static const char *visibilitynames[]={"LAN only","Public (listed)","Friends (game code)","Private (game code)"};
#define NUMVISIBILITIES 4

#define SETUPROWS 9
#define MAPROW 7
#define INTERNETROW 8
static const char *rowlabels[SETUPROWS]=
{
	"Server name","Game mode","Captures to win","Time limit","Sky","Terrain","Scenery","Map","Internet"
};

#define PANELX1 -.95f
#define PANELX2 .95f
#define ROWTOP .66f
#define ROWHEIGHT .095f
#define LABELX -.85f
#define VALUEX -.2f
#define VALUEX2 .85f
#define ARROWW .08f            //"<" and ">" at both ends of a value

static int keyrow;             //row picked with the keyboard

//bottom of a row
static float RowY(int row)
{
	return ROWTOP-(row+1)*ROWHEIGHT;
}

static int Inside(float x1,float y1,float x2,float y2)
{
	return mousepos.x>x1&&mousepos.x<x2&&mousepos.y>y1&&mousepos.y<y2;
}

static int RowAt(float y)
{
	for(int i=0;i<SETUPROWS;i++)
		if(y>=RowY(i)&&y<RowY(i)+ROWHEIGHT)
			return i;
	return -1;
}

//sky names: the "name:" line written above each skyN block of skies.ini

static void LoadSkyNames()
{
	CfgFile ini;
	numskies=0;
	if(ini.Load("skies.ini")) numskies=ini.GetValuei("numskies");
	if(numskies>MAXSKIES) numskies=MAXSKIES;
	for(int i=0;i<numskies;i++)
		sprintf(skynames[i],"Sky %d",i+1);

	HANDLE file=CreateFile("skies.ini",GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	if(file==INVALID_HANDLE_VALUE) return;
	DWORD len=GetFileSize(file,0);
	char *data=new char[len+1];
	DWORD read;
	ReadFile(file,data,len,&read,0);
	CloseHandle(file);
	data[read]=0;
	char label[40]="";
	for(char *line=data;*line;)
	{
		char *end=line;
		while(*end&&*end!='\n') end++;
		int linelen=end-line;
		while(linelen&&(line[linelen-1]=='\r'||line[linelen-1]==' ')) linelen--;
		if(linelen>1&&linelen<40&&line[linelen-1]==':')
		{
			memcpy(label,line,linelen-1);
			label[linelen-1]=0;
		}
		int n;
		if(label[0]&&sscanf(line,"sky%dsunheight",&n)==1&&n>=0&&n<numskies)
		{
			strcpy(skynames[n],label);
			label[0]=0;
		}
		line=*end?end+1:end;
	}
	delete []data;
}

void OpenServerSetup()
{
	LoadSkyNames();
	nummaps=ListSavedMaps(mapnames,MAXMAPS);
	mapchoice=-1;
	setuperror[0]=0;
	lobbyok=LobbyReady();
	visibility=0;
	CfgFile net;
	servername[0]=0;
	if(net.Load(UserFile("net.ini")))
	{
		if(net.GetValues("servername"))
			CopyIniString(servername,net.GetValues("servername"),sizeof(servername));
		if(net.GetValues("serversky")) skychoice=net.GetValuei("serversky");
		if(net.GetValues("serverterrain")) terrainchoice=net.GetValuei("serverterrain");
		if(net.GetValues("serverscenery")) scenerychoice=net.GetValuei("serverscenery");
		if(const char *map=net.GetValues("servermap"))   //a name, "*" for a random land
			for(int i=0;i<nummaps;i++)
				if(!strcmp(mapnames[i],map)) mapchoice=i;
		char vis[16];
		if(net.GetValues("servervisibility"))
		{
			CopyIniString(vis,net.GetValues("servervisibility"),sizeof(vis));
			for(int i=0;i<NUMVISIBILITIES;i++)
				if(!strcmp(vis,visibilities[i])) visibility=i;
		}
	}
	if(!servername[0]) strcpy(servername,"Edifice Server");
	if(skychoice<-1||skychoice>=numskies) skychoice=-1;
	if(terrainchoice<-1||terrainchoice>=NUMTERRAINS) terrainchoice=-1;
	if(scenerychoice<-1||scenerychoice>=NUMSCENERIES) scenerychoice=-1;
	if(sv.capturelimit<1) sv.capturelimit=5;
	if(sv.timelimit<5) sv.timelimit=10;
	editingname=0;
	keyrow=0;
	showsetup=1;
}

int IsServerSetupOpen()
{
	return showsetup;
}

static void SaveSetup()
{
	char sky[16],terrain[16],scenery[16],mode[16],captures[16],timelimit[16];
	sprintf(sky,"%d",skychoice);
	sprintf(terrain,"%d",terrainchoice);
	sprintf(scenery,"%d",scenerychoice);
	sprintf(mode,"%d",sv.gamemode);
	sprintf(captures,"%d",sv.capturelimit);
	sprintf(timelimit,"%d",sv.timelimit);
	const char *netkeys[6]={"servername","serversky","serverterrain","serverscenery","servermap","servervisibility"};
	const char *netvalues[6]={servername,sky,terrain,scenery,mapchoice>=0?mapnames[mapchoice]:"*",visibilities[visibility]};
	SetIniValues(UserFile("net.ini"),netkeys,netvalues,6);
	const char *svkeys[3]={"gamemode","capturelimit","timelimit"};
	const char *svvalues[3]={mode,captures,timelimit};
	SetIniValues(UserFile("sv.ini"),svkeys,svvalues,3);
}

//picks a value for every "Random" choice, then starts the server

static void StartConfiguredServer()
{
	if(!servername[0]) strcpy(servername,"Edifice Server");
	SaveSetup();   //before StartServer, which reads the server name from net.ini
	hkforcedsky=skychoice;
	int terrain=terrainchoice>=0?terrainchoice:(int)(RND*NUMTERRAINS)%NUMTERRAINS;
	hkreliefscale=terrainscales[terrain];
	int scenery=scenerychoice>=0?scenerychoice:(int)(RND*NUMSCENERIES)%NUMSCENERIES;
	hksceneryoverride=scenerycounts[scenery];
	showsetup=0;
	editingname=0;
	if(mapchoice>=0)
	{
		//the server starts with the map (hugekilla.cpp); clients get it, buildings
		//included, like any land
		char err[300];
		if(LoadMapPath(MapPath(mapnames[mapchoice]),err,sizeof(err))) return;
		snprintf(setuperror,sizeof(setuperror),"%s",err);
		showsetup=1;   //stay here and say why
		return;
	}
	StartServer();
}

static void RandomizeAll()
{
	skychoice=-1;
	terrainchoice=-1;
	scenerychoice=-1;
	sv.capturelimit=3+(int)(RND*8)%8;
	sv.timelimit=5*(2+(int)(RND*5)%5);
}

//cycles a choice in [-1,count-1] (-1 = Random)

static void Cycle(int &choice,int count,int dir)
{
	choice+=dir;
	if(choice<-1) choice=count-1;
	if(choice>=count) choice=-1;
}

static void ChangeRow(int row,int dir)
{
	switch(row)
	{
	case 0: editingname=1; break;
	case 1: sv.gamemode=!sv.gamemode; break;
	case 2:
		sv.capturelimit+=dir;
		if(sv.capturelimit<1) sv.capturelimit=1;
		if(sv.capturelimit>20) sv.capturelimit=20;
		break;
	case 3:
		sv.timelimit+=dir*5;
		if(sv.timelimit<5) sv.timelimit=5;   //0 would end the game at once (GamePlay.cpp)
		if(sv.timelimit>60) sv.timelimit=60;
		break;
	case 4: Cycle(skychoice,numskies,dir); break;
	case 5: Cycle(terrainchoice,NUMTERRAINS,dir); break;
	case 6: Cycle(scenerychoice,NUMSCENERIES,dir); break;
	case MAPROW: Cycle(mapchoice,nummaps,dir); setuperror[0]=0; break;
	case INTERNETROW:
		if(lobbyok) visibility=(visibility+dir+NUMVISIBILITIES)%NUMVISIBILITIES;
		break;
	}
}

//the value shown for a row; 0 when it can't be changed (given by the map, no lobby)
static int RowText(int row,char *text)
{
	switch(row)
	{
	case 0: strcpy(text,servername); break;
	case 1: strcpy(text,sv.gamemode?"Sandbox":"Capture the flag"); break;
	case 2: sprintf(text,"%d",sv.capturelimit); break;
	case 3: sprintf(text,"%d min",sv.timelimit); break;
	case 4: strcpy(text,skychoice>=0?skynames[skychoice]:"Random"); break;
	case 5: strcpy(text,terrainchoice>=0?terrainnames[terrainchoice]:"Random"); break;
	case 6: strcpy(text,scenerychoice>=0?scenerynames[scenerychoice]:"Random"); break;
	case MAPROW: strcpy(text,mapchoice>=0?mapnames[mapchoice]:"New random land"); break;
	case INTERNETROW:
		if(lobbyok) strcpy(text,visibilitynames[visibility]);
		else
		{
			strcpy(text,"No lobby set (net.ini)");
			return 0;
		}
		break;
	}
	//a saved map brings its own sky, terrain and scenery
	if(mapchoice>=0&&row>=4&&row<=6)
	{
		strcpy(text,"From the map");
		return 0;
	}
	return 1;
}

//what a row does, under the list
static const char *RowHelp(int row)
{
	switch(row)
	{
	case 0: return "Shown in the server list of the other players";
	case 1: return sv.gamemode?"Build freely, no flags":"Bring the enemy flag back to your base";
	case 2: return "Flag captures a team needs to win the round";
	case 3: return "Length of a round";
	case 4: case 5: case 6:
		return mapchoice>=0?"The saved map gives the sky, terrain and scenery":"Random picks one when the server starts";
	case MAPROW:
		return mapchoice>=0?"A map saved with savemap, buildings included":"A new land from the settings above";
	case INTERNETROW:
		if(!lobbyok) return "Set lobby= in net.ini to announce games on the Internet";
		if(visibility==0) return "Only players on your network see the game";
		if(visibility==1) return "Listed on the Internet tab of the join screen";
		return "Not listed: players join with the game code (console, scores)";
	}
	return "";
}

//buttons at the bottom of the panel

#define BTN_RANDOM -.9f,-.87f,-.42f,-.76f
#define BTN_BACK .22f,-.87f,.52f,-.76f
#define BTN_START .6f,-.87f,.9f,-.76f

void ServerSetupClick()
{
	editingname=0;
	if(Inside(BTN_RANDOM)) { RandomizeAll(); return; }
	if(Inside(BTN_BACK)) { showsetup=0; SaveSetup(); return; }
	if(Inside(BTN_START)) { StartConfiguredServer(); return; }
	if(mousepos.x<LABELX||mousepos.x>PANELX2) return;
	int row=RowAt(mousepos.y);
	if(row<0) return;
	keyrow=row;
	//"<" (left part of the value) goes back, anything else forward
	ChangeRow(row,mousepos.x<VALUEX+ARROWW?-1:1);
}

//returns 1 when the key was used by the screen

int ServerSetupHandleKey(int vk)
{
	if(!showsetup) return 0;
	if(editingname)
	{
		int len=strlen(servername);
		if(vk==VK_BACK&&len) servername[len-1]=0;
		if(vk==VK_RETURN||vk==VK_ESCAPE||vk==VK_TAB) editingname=0;
		return 1;
	}
	if(vk==VK_ESCAPE)
	{
		showsetup=0;
		SaveSetup();
	}
	if(vk==VK_RETURN) StartConfiguredServer();
	//keyboard: Up/Down (or Tab) pick a row, Left/Right change it
	if(vk==VK_UP) keyrow=(keyrow+SETUPROWS-1)%SETUPROWS;
	if(vk==VK_DOWN||vk==VK_TAB) keyrow=(keyrow+1)%SETUPROWS;
	if(vk==VK_LEFT) ChangeRow(keyrow,-1);
	if(vk==VK_RIGHT) ChangeRow(keyrow,1);
	return 1;
}

void ServerSetupHandleText(const char *text)
{
	if(showsetup&&editingname)
		AppendText(servername,sizeof(servername),text);
}

//"<" or ">" of a value
static void Arrow(float x1,float y1,float x2,float y2,const char *label)
{
	int over=Inside(x1,y1,x2,y2);
	UIRect(x1,y1,x2,y2,1,1,1,over?.25f:.1f);
	glColor3f(over?1:.8f,over?1:.8f,over?.5f:.8f);
	UIText((x1+x2-UITextWidth(label))/2,UITextY(y1,y2),label);
}

void DrawServerSetup()
{
	if(!showsetup) return;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	UIPanel(PANELX1,-.92f,PANELX2,.9f,.13f,"Server Setup");

	char text[100];
	int hoverrow=Inside(LABELX,RowY(SETUPROWS-1),PANELX2,ROWTOP)?RowAt(mousepos.y):-1;
	for(int i=0;i<SETUPROWS;i++)
	{
		float y1=RowY(i)+.008f,y2=RowY(i)+ROWHEIGHT-.008f;
		int enabled=RowText(i,text);
		if(i==hoverrow) UIRect(LABELX-.04f,RowY(i),PANELX2-.04f,RowY(i)+ROWHEIGHT,1,1,1,.06f);
		if(i==keyrow) UIRect(LABELX-.04f,y1,LABELX-.032f,y2,1,1,.5f,.9f);
		glColor3f(i==hoverrow||i==keyrow?1:.75f,i==hoverrow||i==keyrow?1:.75f,i==hoverrow||i==keyrow?1:.75f);
		UIText(LABELX,UITextY(y1,y2),rowlabels[i]);
		if(i==0)
		{
			UIField(VALUEX,y1,VALUEX2,y2,servername,editingname,"Edifice Server");
			continue;
		}
		UIRect(VALUEX,y1,VALUEX2,y2,0,0,0,.55f);
		if(enabled)
		{
			Arrow(VALUEX,y1,VALUEX+ARROWW,y2,"<");
			Arrow(VALUEX2-ARROWW,y1,VALUEX2,y2,">");
			glColor3f(1,1,1);
		}
		else glColor3f(.45f,.45f,.45f);
		float room=VALUEX2-VALUEX-2*ARROWW-.04f;
		float w=UITextWidth(text);
		UITextFit(w<room?(VALUEX+VALUEX2-w)/2:VALUEX+ARROWW+.02f,UITextY(y1,y2),text,room);
	}

	//what the row under the mouse (or picked with the keys) does
	int helprow=hoverrow>=0?hoverrow:keyrow;
	glColor3f(.6f,.6f,.6f);
	UITextFit(LABELX,UITextY(-.3f,-.22f),RowHelp(helprow),PANELX2-.05f-LABELX);
	if(setuperror[0])
	{
		UIRect(PANELX1+.02f,-.5f,PANELX2-.02f,-.4f,.6f,.15f,.1f,.35f);
		glColor3f(1,.7f,.55f);
		UITextFit(LABELX,UITextY(-.5f,-.4f),setuperror,PANELX2-.05f-LABELX);
	}
	glColor3f(.45f,.45f,.45f);
	const char *keys="Click or Left/Right: change    Up/Down: pick a row    Enter: start    Esc: back";
	UITextFit(LABELX,UITextY(-.72f,-.64f,.04f),keys,PANELX2-.05f-LABELX,.04f);

	UIButton(BTN_RANDOM,"Randomize all",1);
	UIButton(BTN_BACK,"Back",1);
	UIButton(BTN_START,"Start",1,1);
	glColor3f(1,1,1);
}

#endif // !HK_WIN32_NATIVE
