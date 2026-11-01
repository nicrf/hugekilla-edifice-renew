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
#define ROWTOP .5f
#define ROWHEIGHT .09f
#define LABELX -.85f
#define VALUEX -.1f
#define CHARW .04f

static float RowY(int row)
{
	return ROWTOP-row*ROWHEIGHT;
}

static int Inside(float x1,float y1,float x2,float y2)
{
	return mousepos.x>x1&&mousepos.x<x2&&mousepos.y>y1&&mousepos.y<y2;
}

static int RowAt(float y)
{
	for(int i=0;i<SETUPROWS;i++)
		if(y>=RowY(i)-.015f&&y<RowY(i)+ROWHEIGHT-.015f)
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

static void RowText(int row,char *text)
{
	switch(row)
	{
	case 0: sprintf(text,"%s%s",servername,editingname?"_":""); break;
	case 1: strcpy(text,sv.gamemode?"Sandbox":"Capture the flag"); break;
	case 2: sprintf(text,"< %d >",sv.capturelimit); break;
	case 3: sprintf(text,"< %d min >",sv.timelimit); break;
	case 4: sprintf(text,"< %s >",skychoice>=0?skynames[skychoice]:"Random"); break;
	case 5: sprintf(text,"< %s >",terrainchoice>=0?terrainnames[terrainchoice]:"Random"); break;
	case 6: sprintf(text,"< %s >",scenerychoice>=0?scenerynames[scenerychoice]:"Random"); break;
	case MAPROW: sprintf(text,"< %s >",mapchoice>=0?mapnames[mapchoice]:"New random"); break;
	case INTERNETROW:
		if(lobbyok) sprintf(text,"< %s >",visibilitynames[visibility]);
		else strcpy(text,"No lobby set (net.ini)");
		break;
	}
	//a saved map brings its own sky, terrain and scenery
	if(mapchoice>=0&&row>=4&&row<=6) strcpy(text,"From the map");
}

//buttons at the bottom of the panel

#define BUTTONY -.78f
static int OverButton(float x,const char *text)
{
	return Inside(x-.02f,BUTTONY-.02f,x+strlen(text)*CHARW+.02f,BUTTONY+.09f);
}
#define RANDOMX -.85f
#define BACKX .2f
#define STARTX .55f

void ServerSetupClick()
{
	editingname=0;
	if(OverButton(RANDOMX,"Randomize all")) { RandomizeAll(); return; }
	if(OverButton(BACKX,"Back")) { showsetup=0; SaveSetup(); return; }
	if(OverButton(STARTX,"Start")) { StartConfiguredServer(); return; }
	if(mousepos.x<LABELX||mousepos.x>PANELX2) return;
	int row=RowAt(mousepos.y);
	if(row<0) return;
	//"<" (left part of the value) goes back, anything else forward
	ChangeRow(row,mousepos.x<VALUEX+CHARW*1.5f?-1:1);
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
	return 1;
}

void ServerSetupHandleText(const char *text)
{
	if(showsetup&&editingname)
		AppendText(servername,sizeof(servername),text);
}

void DrawServerSetup()
{
	if(!showsetup) return;
	glDisable(GL_TEXTURE_2D);
	glColor4f(0,0,0,.88f);
	glBegin(GL_QUADS);
	glVertex2f(PANELX1,-.92f);
	glVertex2f(PANELX2,-.92f);
	glVertex2f(PANELX2,.85f);
	glVertex2f(PANELX1,.85f);
	glEnd();
	glEnable(GL_TEXTURE_2D);

	glColor3f(1,1,.5f);
	DrawText(LABELX,.7f,"Server Setup");
	char text[100];
	for(int i=0;i<SETUPROWS;i++)
	{
		int hover=Inside(LABELX,RowY(i)-.015f,PANELX2,RowY(i)+ROWHEIGHT-.015f);
		glColor3f(hover?1:.8f,hover?1:.8f,hover?1:.8f);
		DrawText(LABELX,RowY(i),rowlabels[i]);
		RowText(i,text);
		if(i==0&&editingname) glColor3f(1,1,.5f);
		else glColor3f(1,1,1);
		DrawText(VALUEX,RowY(i),text);
	}
	glColor3f(.6f,.6f,.6f);
	DrawText(LABELX,-.6f,"Click a value to change it");
	if(setuperror[0])
	{
		char line[46];
		snprintf(line,sizeof(line),"%s",setuperror);   //what fits in the panel
		glColor3f(1,.4f,.4f);
		DrawText(LABELX,-.69f,line);
	}

	const struct { float x; const char *text; } buttons[3]=
	{
		{RANDOMX,"Randomize all"},{BACKX,"Back"},{STARTX,"Start"}
	};
	for(int i=0;i<3;i++)
	{
		if(OverButton(buttons[i].x,buttons[i].text)) glColor3f(1,1,1);
		else if(i==2) glColor3f(.6f,1,.6f);
		else glColor3f(.7f,.7f,.7f);
		DrawText(buttons[i].x,BUTTONY,buttons[i].text);
	}
	glColor3f(1,1,1);
}

#endif // !HK_WIN32_NATIVE
