//join screen (main menu "Play"): list of servers found on the LAN, at an address
//typed in, or joined recently, with version, players, mode and ping. Sortable,
//filterable, mouse and keyboard driven. The networking side is in multiplay.cpp.
//The Internet tab lists the public games of the lobby (edifice-lobby, through
//lobbygame.cpp) and joins private ones by their game code; the connection is then
//the same direct UDP one, to the address the lobby saw the host at.

#include "StdAfx.h"
#include "redrawing.h"
#include "playing.h"
#include "multiplay.h"
#include "ui.h"
#include "lobbygame.h"
#include <SDL.h>

extern Vector mousepos;
extern DWORD curtime;

#define MAXRECENT 5
#define MAXLOCALLIST_UI 128     //MAXLOCALLIST in multiplay.cpp
#define S .7f                   //text scale of the screen
#define CW (.04f*S)             //character width
#define ROWH .062f
#define ROWTOP .40f
#define MAXROWS 13

enum { FOCUS_NONE, FOCUS_ADDRESS, FOCUS_NAME };   //FOCUS_ADDRESS: the game code on the Internet tab
enum { SORT_FOUND, SORT_NAME, SORT_PLAYERS, SORT_PING };
enum { TAB_LAN, TAB_INTERNET };

static int joinscreen;
static char address[64];
static char playername[32];
static int focus=FOCUS_ADDRESS;
static int selected=-1;         //index in the server list (not the row)
static int sortmode=SORT_PING;
static int showincompatible=1;
static char status[200];
static char recent[MAXRECENT][64];
static int numrecent;
static int tab;

//Internet tab: the lobby's public games, shown like the LAN ones
static LobbyGame lobbygames[LOBBY_MAXGAMES];
static JoinGame inetgames[LOBBY_MAXGAMES];
static int numinet;
static int inetstate;           //0 not asked, 1 waiting for the lobby, 2 listed, -1 unavailable
static char inetmsg[200];       //shown when the list is empty
static char code[LOBBY_CODELEN+5];
static int lookingup;           //waiting for the lobby to find a game code
//the Internet game being joined: waiting for its answer to an info request
static LobbyGame joining;
static int joiningactive;
static DWORD joiningsince,joininglastquery;

//columns: x of each, header label, sort mode it sets
static const float colx[6]={-.9f,-.35f,.17f,.36f,.61f,.79f};
static const char *colname[6]={"Name","Address","Ver.","Players","Mode","Ping"};
static const int colsort[6]={SORT_NAME,SORT_FOUND,SORT_FOUND,SORT_PLAYERS,SORT_FOUND,SORT_PING};

//the Internet tab has the game code instead of the ping
static const char *ColName(int c)
{
	return tab==TAB_INTERNET&&c==5?"Code":colname[c];
}

static int ColSort(int c)
{
	return tab==TAB_INTERNET&&c==5?SORT_FOUND:colsort[c];
}

static int GameCount()
{
	return tab==TAB_INTERNET?numinet:JoinGameCount();
}

static JoinGame *GameAt(int i)
{
	if(tab!=TAB_INTERNET) return JoinGameAt(i);
	return i>=0&&i<numinet?&inetgames[i]:0;
}

static int Inside(float x1,float y1,float x2,float y2)
{
	return mousepos.x>x1&&mousepos.x<x2&&mousepos.y>y1&&mousepos.y<y2;
}

static void Text(float x,float y,const char *text,float scale=S)
{
	glPushMatrix();
	glTranslatef(x,y,0);
	glScalef(scale,scale,1);
	DrawText(0,0,text);
	glPopMatrix();
}

static void Rect(float x1,float y1,float x2,float y2,float r,float g,float b,float a)
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

//framed button; returns 1 when the mouse is over it
static int Button(float x1,float y1,float x2,float y2,const char *label,int enabled,int strong=0)
{
	int over=enabled&&Inside(x1,y1,x2,y2);
	Rect(x1,y1,x2,y2,1,1,1,over?.22f:.1f);
	if(!enabled) glColor3f(.4f,.4f,.4f);
	else if(strong) glColor3f(.6f,1,.6f);
	else glColor3f(.9f,.9f,.9f);
	Text((x1+x2)/2-strlen(label)*CW/2,(y1+y2)/2-.028f,label);
	return over;
}

#define BTN_ADD -.02f,.6f,.2f,.68f
#define BTN_REFRESH .26f,.6f,.56f,.68f
#define BTN_BACK -.9f,-.86f,-.62f,-.76f
#define BTN_JOIN .56f,-.86f,.9f,-.76f
#define FIELD_ADDRESS -.62f,.6f,-.06f,.68f
#define FIELD_NAME .42f,.73f,.9f,.81f
#define CHECKBOX -.9f,-.7f,-.2f,-.62f
#define TAB_LANBTN -.4f,.73f,-.14f,.81f
#define TAB_INETBTN -.12f,.73f,.16f,.81f

//---- recent servers (net.ini "recentservers", comma separated) ----

static void LoadRecent()
{
	numrecent=0;
	CfgFile ini;
	if(!ini.Load(UserFile("net.ini"))) return;
	const char *list=ini.GetValues("recentservers");
	if(!list) return;
	while(*list&&*list!='\r'&&*list!='\n'&&numrecent<MAXRECENT)
	{
		int n=0;
		while(*list&&*list!=','&&*list!='\r'&&*list!='\n'&&n<63)
			recent[numrecent][n++]=*list++;
		recent[numrecent][n]=0;
		if(n) numrecent++;
		if(*list==',') list++;
	}
}

static void RememberServer(const char *addr)
{
	char list[MAXRECENT*65]="";
	strcpy(list,addr);
	int kept=1;
	for(int i=0;i<numrecent&&kept<MAXRECENT;i++)
	{
		if(!strcmp(recent[i],addr)) continue;
		strcat(list,",");
		strcat(list,recent[i]);
		kept++;
	}
	const char *keys[1]={"recentservers"};
	const char *values[1]={list};
	SetIniValues(UserFile("net.ini"),keys,values,1);
	LoadRecent();
}

static void SaveName()
{
	if(!playername[0]) strcpy(playername,"Player");
	const char *keys[1]={"name"};
	const char *values[1]={playername};
	SetIniValues(UserFile("net.ini"),keys,values,1);
	JoinNetOpen();   //reloads the settings the join request uses
}

//---- Internet games ----

//a lobby entry as a join list row; compatibility as for a LAN server
static void FromLobby(JoinGame *game,const LobbyGame *g)
{
	memset(game,0,sizeof(*game));
	snprintf(game->name,sizeof(game->name),"%s",g->name);
	snprintf(game->address,sizeof(game->address),"%s",g->address);
	snprintf(game->version,sizeof(game->version),"%s",g->version);
	PeerVersion peer;
	memset(&peer,0,sizeof(peer));
	peer.present=g->protocol>1;
	peer.protocol=g->protocol;
	peer.layout=EDIFICE_NET_LAYOUT;
	snprintf(peer.version,sizeof(peer.version),"%s",g->version);
	game->compatible=VersionCompatible(&peer,game->reason,sizeof(game->reason));
	game->players=g->players;
	game->maxplayers=g->maxplayers;
	game->gamemode=g->mode;
}

static void RefreshInternet()
{
	numinet=0;
	if(!LobbyReady(inetmsg,sizeof(inetmsg))) inetstate=-1;
	else if(LobbyListRequest())
	{
		inetstate=1;
		strcpy(inetmsg,"Asking the lobby...");
	}
	else
	{
		inetstate=-1;
		strcpy(inetmsg,"The lobby isn't answering (F5 or Refresh to try again)");
	}
}

//asks the host for its info, like any server; Update() joins once it answers
static void JoinInternet(const LobbyGame *g)
{
	JoinGame game;
	FromLobby(&game,g);
	if(!game.compatible)
	{
		snprintf(status,sizeof(status),"Can't join: %s",game.reason);
		return;
	}
	SaveName();
	joining=*g;
	joiningactive=1;
	joiningsince=GetTickCount();
	joininglastquery=joiningsince-1000;
	snprintf(status,sizeof(status),"Contacting %s at %s:%d...",g->name,g->address,g->port);
}

static void LookupCode()
{
	char why[200],id[LOBBY_CODELEN+1];
	focus=FOCUS_NONE;
	if(!code[0]) return;
	if(!LobbyReady(why,sizeof(why))) snprintf(status,sizeof(status),"%s",why);
	else if(!LobbyNormalizeCode(code,id,sizeof(id))) snprintf(status,sizeof(status),"Not a game code: %s",code);
	else if(!LobbyLookupRequest(id)) strcpy(status,"The lobby isn't answering, try again");
	else
	{
		lookingup=1;
		snprintf(status,sizeof(status),"Looking for game %s...",id);
	}
}

//every frame: answers of the lobby, and the Internet game being joined
static void Update()
{
	char err[200];
	int count;
	if(inetstate==1)
	{
		int r=LobbyListResult(lobbygames,LOBBY_MAXGAMES,&count,err,sizeof(err));
		if(r>0)
		{
			numinet=count;
			for(int i=0;i<count;i++) FromLobby(&inetgames[i],&lobbygames[i]);
			inetstate=2;
			strcpy(inetmsg,"No public game right now (F5 or Refresh to look again)");
		}
		else if(r<0)
		{
			inetstate=-1;
			snprintf(inetmsg,sizeof(inetmsg),"%s",err);
		}
	}
	if(lookingup)
	{
		LobbyGame g;
		int r=LobbyLookupResult(&g,err,sizeof(err));
		if(r) lookingup=0;
		if(r>0) JoinInternet(&g);
		else if(r<0) snprintf(status,sizeof(status),"%s",err);
	}
	if(joiningactive)
	{
		DWORD now=GetTickCount();
		int i=JoinNetFind(joining.address,joining.port);
		if(i>=0)
		{
			joiningactive=0;
			status[0]=0;
			char reason[200];
			if(JoinNetConnect(i,reason,sizeof(reason))) joinscreen=0;
			else snprintf(status,sizeof(status),"Can't join: %s",reason);
		}
		else if(now-joiningsince>6000)
		{
			joiningactive=0;
			snprintf(status,sizeof(status),"No answer from %s:%d (the host may have to open its UDP ports)",
				joining.address,joining.port);
		}
		else if(now-joininglastquery>=1000)   //UDP: asked again every second
		{
			JoinNetQuery(joining.address,joining.port);
			joininglastquery=now;
		}
	}
}

//---- list ----

static void Refresh()
{
	status[0]=0;
	selected=-1;
	joiningactive=0;
	if(tab==TAB_INTERNET)
	{
		RefreshInternet();
		return;
	}
	JoinNetClearList();
	JoinNetSearchLAN();
	for(int i=0;i<numrecent;i++)
		JoinNetQuery(recent[i]);
}

static void SetTab(int t)
{
	tab=t;
	focus=FOCUS_ADDRESS;
	Refresh();
}

static void AddAddress()
{
	if(tab==TAB_INTERNET)
	{
		LookupCode();
		return;
	}
	if(!address[0]) return;
	if(JoinNetQuery(address))
		snprintf(status,sizeof(status),"Asking %s...",address);
	else
		snprintf(status,sizeof(status),"Unknown host: %s",address);
	focus=FOCUS_NONE;
}

static int Order(int *order)
{
	int n=0;
	for(int i=0;i<GameCount();i++)
		if(showincompatible||GameAt(i)->compatible)
			order[n++]=i;
	//insertion sort: lists are short
	for(int a=1;a<n;a++)
		for(int b=a;b>0;b--)
		{
			JoinGame *x=GameAt(order[b-1]),*y=GameAt(order[b]);
			int swap=0;
			if(sortmode==SORT_NAME) swap=stricmp(x->name,y->name)>0;
			if(sortmode==SORT_PLAYERS) swap=x->players<y->players;
			if(sortmode==SORT_PING) swap=x->ping>y->ping;
			if(!swap) break;
			int t=order[b];
			order[b]=order[b-1];
			order[b-1]=t;
		}
	return n;
}

static void Join()
{
	if(!GameAt(selected))
	{
		strcpy(status,"Pick a server in the list first");
		return;
	}
	if(tab==TAB_INTERNET)
	{
		JoinInternet(&lobbygames[selected]);
		return;
	}
	SaveName();
	char reason[200];
	if(!JoinNetConnect(selected,reason,sizeof(reason)))
	{
		snprintf(status,sizeof(status),"Can't join: %s",reason);
		return;
	}
	RememberServer(JoinGameAt(selected)->address);
	joinscreen=0;
}

static void Close()
{
	if(focus==FOCUS_NAME) SaveName();
	joinscreen=0;
	JoinNetClose();
}

//---- entry points ----

void OpenJoinScreen()
{
	JoinNetOpen();
	CfgFile net;
	playername[0]=0;
	if(net.Load(UserFile("net.ini"))&&net.GetValues("name"))
		CopyIniString(playername,net.GetValues("name"),sizeof(playername));
	LoadRecent();
	focus=FOCUS_ADDRESS;
	joinscreen=1;
	lookingup=0;
	Refresh();
}

//debug aid (HK_LOBBYJOIN): the Internet tab, a game code typed in and looked up
void JoinScreenJoinCode(const char *text)
{
	OpenJoinScreen();
	SetTab(TAB_INTERNET);
	snprintf(code,sizeof(code),"%s",text);
	LookupCode();
}

int IsJoinScreenOpen()
{
	return joinscreen;
}

//back to the join screen after a refused connection, with the reason
void JoinScreenShowError(const char *text)
{
	OpenJoinScreen();
	int n=0;
	for(;text[n]&&n<(int)sizeof(status)-1;n++)
		status[n]=text[n]=='\r'?' ':text[n];
	status[n]=0;
}

void JoinScreenClick(int clicks)
{
	if(!joinscreen) return;
	if(Inside(TAB_LANBTN)) { if(tab!=TAB_LAN) SetTab(TAB_LAN); return; }
	if(Inside(TAB_INETBTN)) { if(tab!=TAB_INTERNET) SetTab(TAB_INTERNET); return; }
	if(Inside(FIELD_ADDRESS)) { focus=FOCUS_ADDRESS; return; }
	if(Inside(FIELD_NAME)) { focus=FOCUS_NAME; return; }
	if(focus==FOCUS_NAME) SaveName();
	focus=FOCUS_NONE;
	if(Inside(BTN_ADD)) { AddAddress(); return; }
	if(Inside(BTN_REFRESH)) { Refresh(); return; }
	if(Inside(BTN_BACK)) { Close(); return; }
	if(Inside(BTN_JOIN)) { Join(); return; }
	if(Inside(CHECKBOX)) { showincompatible=!showincompatible; return; }
	for(int c=0;c<6;c++)
	{
		float x2=c<5?colx[c+1]:.95f;
		if(Inside(colx[c]-.01f,.46f,x2,.53f)) { sortmode=ColSort(c); return; }
	}
	int order[MAXLOCALLIST_UI];
	int n=Order(order);
	for(int r=0;r<n&&r<MAXROWS;r++)
	{
		float y=ROWTOP-r*ROWH;
		if(Inside(-.93f,y-.012f,.93f,y+ROWH-.012f))
		{
			selected=order[r];
			if(clicks>=2) Join();
			return;
		}
	}
}

//returns 1 when the key was used by the join screen (all are, while it is open)
int HandleJoinScreenKey(int sdlkey)
{
	if(!joinscreen) return 0;
	char *typed=tab==TAB_INTERNET?code:address;
	char *field=focus==FOCUS_ADDRESS?typed:focus==FOCUS_NAME?playername:0;
	switch(sdlkey)
	{
	case SDLK_ESCAPE:
		Close();
		break;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		if(focus==FOCUS_ADDRESS&&typed[0]) AddAddress();
		else if(focus==FOCUS_NAME) { SaveName(); focus=FOCUS_NONE; }
		else Join();
		break;
	case SDLK_BACKSPACE:
		if(field&&field[0]) field[strlen(field)-1]=0;
		break;
	case SDLK_TAB:
		focus=focus==FOCUS_ADDRESS?FOCUS_NAME:FOCUS_ADDRESS;
		break;
	case SDLK_F5:
		Refresh();
		break;
	case SDLK_UP:
	case SDLK_DOWN:
		{
			int order[MAXLOCALLIST_UI];
			int n=Order(order);
			if(!n) break;
			int row=0;
			for(int r=0;r<n;r++) if(order[r]==selected) row=r;
			if(GameAt(selected)) row+=sdlkey==SDLK_UP?-1:1;
			if(row<0) row=0;
			if(row>=n) row=n-1;
			selected=order[row];
		}
		break;
	case SDLK_v:
		if(field&&(SDL_GetModState()&(KMOD_GUI|KMOD_CTRL)))
		{
			char *clip=SDL_GetClipboardText();
			if(clip)
			{
				field[0]=0;
				HandleJoinScreenText(clip);
				SDL_free(clip);
			}
		}
		break;
	}
	return 1;
}

void HandleJoinScreenText(const char *text)
{
	if(!joinscreen||focus==FOCUS_NONE) return;
	if(SDL_GetModState()&(KMOD_GUI|KMOD_CTRL)) return;
	if(focus==FOCUS_ADDRESS)
	{
		char *field=tab==TAB_INTERNET?code:address;
		int size=tab==TAB_INTERNET?sizeof(code):sizeof(address);
		int len=strlen(field);
		for(;*text&&len<size-1;text++)
		{
			char c=*text;
			if((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||(c=='.'&&tab!=TAB_INTERNET))
				field[len++]=c;
		}
		field[len]=0;
	}
	else
		AppendText(playername,sizeof(playername),text);
}

static void Field(float x1,float y1,float x2,float y2,const char *text,int focused)
{
	Rect(x1,y1,x2,y2,0,0,0,.6f);
	Rect(x1,y1,x2,y1+.004f,1,1,focused?.5f:1,focused?.9f:.3f);
	char shown[80];
	int room=(int)((x2-x1-.03f)/CW)-1;
	int len=strlen(text);
	snprintf(shown,sizeof(shown),"%s%s",len>room?&text[len-room]:text,focused&&((curtime/500)&1)?"_":"");
	glColor3f(1,1,focused?.6f:1);
	Text(x1+.015f,(y1+y2)/2-.028f,shown);
}

//a tab next to the title
static void Tab(float x1,float y1,float x2,float y2,const char *label,int active)
{
	int over=!active&&Inside(x1,y1,x2,y2);
	Rect(x1,y1,x2,y2,1,1,active?.5f:1,active?.2f:over?.16f:.07f);
	if(active) Rect(x1,y1,x2,y1+.004f,1,1,.5f,.9f);
	if(active) glColor3f(1,1,.5f);
	else glColor3f(over?1:.7f,over?1:.7f,over?1:.7f);
	Text((x1+x2)/2-strlen(label)*CW/2,(y1+y2)/2-.028f,label);
}

void DrawJoinScreen()
{
	if(!joinscreen) return;
	Update();
	if(!joinscreen) return;   //joined
	glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	Rect(-.95f,-.92f,.95f,.88f,0,0,0,.85f);

	glColor3f(1,1,.5f);
	Text(-.9f,.74f,"Join a game",1);
	Tab(TAB_LANBTN,"LAN / IP",tab==TAB_LAN);
	Tab(TAB_INETBTN,"Internet",tab==TAB_INTERNET);
	glColor3f(.7f,.7f,.7f);
	Text(.2f,.75f,"Name:");
	Field(FIELD_NAME,playername,focus==FOCUS_NAME);

	glColor3f(.7f,.7f,.7f);
	if(tab==TAB_INTERNET)
	{
		//private and friends games aren't listed: joined by the code their host gives
		Text(-.9f,.62f,"Code:");
		Field(FIELD_ADDRESS,code,focus==FOCUS_ADDRESS);
		Button(BTN_ADD,"Join",code[0]!=0&&!lookingup);
		Button(BTN_REFRESH,"Refresh",inetstate!=1);
	}
	else
	{
		Text(-.9f,.62f,"Address:");
		Field(FIELD_ADDRESS,address,focus==FOCUS_ADDRESS);
		Button(BTN_ADD,"Add",address[0]!=0);
		Button(BTN_REFRESH,"Refresh",1);
	}

	//column headers (click to sort)
	for(int c=0;c<6;c++)
	{
		int active=ColSort(c)!=SORT_FOUND&&ColSort(c)==sortmode;
		float x2=c<5?colx[c+1]:.95f;
		if(active) glColor3f(1,1,.5f);
		else if(ColSort(c)!=SORT_FOUND&&Inside(colx[c]-.01f,.46f,x2,.53f)) glColor3f(1,1,1);
		else glColor3f(.6f,.6f,.6f);
		Text(colx[c],.48f,ColName(c));
	}
	Rect(-.92f,.455f,.92f,.46f,1,1,1,.25f);

	int order[MAXLOCALLIST_UI];
	int n=Order(order);
	char text[120];
	if(!n)
	{
		glColor3f(.55f,.55f,.55f);
		if(GameCount()) Text(colx[0],ROWTOP,"(only incompatible servers found)");
		else if(tab==TAB_INTERNET) Text(colx[0],ROWTOP,inetmsg);
		else Text(colx[0],ROWTOP,"Searching... (F5 or Refresh to search again)");
	}
	for(int r=0;r<n&&r<MAXROWS;r++)
	{
		JoinGame *g=GameAt(order[r]);
		float y=ROWTOP-r*ROWH;
		int hover=Inside(-.93f,y-.012f,.93f,y+ROWH-.012f);
		if(order[r]==selected) Rect(-.93f,y-.012f,.93f,y+ROWH-.012f,1,1,.5f,.18f);
		else if(hover) Rect(-.93f,y-.012f,.93f,y+ROWH-.012f,1,1,1,.07f);
		float dim=g->compatible?1:.45f;
		glColor3f(dim,dim,dim);
		strncpy(text,g->name,16);
		text[16]=0;
		Text(colx[0],y,text);
		Text(colx[1],y,g->address);
		Text(colx[2],y,g->version);
		if(g->players>=0) snprintf(text,sizeof(text),"%d/%d",g->players,g->maxplayers);
		else strcpy(text,"?");
		Text(colx[3],y,text);
		Text(colx[4],y,g->players<0?"?":g->gamemode?"Build":"CTF");
		if(g->compatible)
		{
			if(g->ping<80) glColor3f(.6f,1,.6f);
			else if(g->ping<200) glColor3f(1,1,.5f);
			else glColor3f(1,.55f,.45f);
		}
		if(tab==TAB_INTERNET)
		{
			glColor3f(dim,dim,dim);
			Text(colx[5],y,lobbygames[order[r]].id);
		}
		else
		{
			snprintf(text,sizeof(text),"%d",g->ping);
			Text(colx[5],y,text);
		}
	}

	//status: last error, or why the selected server can't be joined
	JoinGame *sel=GameAt(selected);
	const char *line=status;
	char why[200];
	if(!line[0]&&sel&&!sel->compatible)
	{
		snprintf(why,sizeof(why),"Can't join: %s",sel->reason);
		line=why;
	}
	if(line[0])
	{
		glColor3f(1,.6f,.45f);
		Text(-.9f,-.52f,line);
	}

	glColor3f(.85f,.85f,.85f);
	snprintf(text,sizeof(text),"[%c] Show incompatible servers",showincompatible?'x':' ');
	if(Inside(CHECKBOX)) glColor3f(1,1,1);
	Text(-.9f,-.68f,text);
	glColor3f(.5f,.5f,.5f);
	snprintf(text,sizeof(text),"You: v%s (protocol %d)",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	Text(.1f,-.68f,text);

	Button(BTN_BACK,"Back",1);
	Button(BTN_JOIN,"Join",sel&&sel->compatible,1);
	glColor3f(.5f,.5f,.5f);
	Text(-.5f,-.82f,"Double-click a server to join");

	glPopAttrib();
	glColor3f(1,1,1);
}
