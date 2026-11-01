//join screen (main menu "Play"): list of servers found on the LAN, at an address
//typed in, or joined recently, with version, players, mode and ping. Sortable,
//filterable, mouse and keyboard driven. The networking side is in multiplay.cpp.
//The Internet tab lists the public games of the lobby (edifice-lobby, through
//lobbygame.cpp) and joins private ones by their game code. A game with an identity
//is joined P2P (M7d: a lobby session first, then GameNetworkingSockets through the
//NATs, netlink.h); otherwise, or when that fails, through the direct UDP one, to
//the address the lobby saw the host at. A host on our own network (the lobby then
//gives its LAN addresses, see lobby.h) is also asked there, from the start: whichever
//answers first is joined (routers seldom loop back to their LAN from their public IP).

#include "gl.h"
#include "platform.h"
#include "cfgfile.h"
#include "redrawing.h"
#include "playing.h"
#include "multiplay.h"
#include "ui.h"
#include "lobbygame.h"
#include "netlink.h"
#include "hktime.h"
#include <SDL.h>

#define MAXRECENT 5
#define MAXLOCALLIST_UI 128     //MAXLOCALLIST in multiplay.cpp
#define ROWH .06f
#define ROWTOP .5f              //top of the first row
#define MAXROWS 13
#define LISTX1 -.93f
#define LISTX2 .93f
#define HEADY1 .51f             //column headers (click to sort)
#define HEADY2 .58f
#define ROWRECT(r) LISTX1,ROWTOP-((r)+1)*ROWH,LISTX2,ROWTOP-(r)*ROWH

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
//the Internet game being joined: waiting for a lobby session (P2P), then for its
//answer to an info request
enum { JOINING_OFF, JOINING_SESSION, JOINING_QUERY };
static LobbyGame joining;
static int joiningactive;
static int joiningp2p;          //asked through a P2P connection
static DWORD joiningsince,joininglastquery,joininglanquery;

//columns: x of each, header label, sort mode it sets
static const float colx[6]={-.9f,-.38f,.1f,.3f,.5f,.7f};
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

//text in a column from x to x2, cut with "..." when too long
static void Cell(float x,float y,float x2,const char *text)
{
	UITextFit(x,y,text,x2-x-.03f);
}

#define PANELY2 .9f
#define TITLEH .13f
#define BTN_ADD .2f,.63f,.44f,.72f
#define BTN_REFRESH .66f,.63f,.9f,.72f
#define BTN_BACK -.9f,-.87f,-.6f,-.76f
#define BTN_JOIN .6f,-.87f,.9f,-.76f
#define FIELD_ADDRESS -.68f,.63f,.16f,.72f
#define FIELD_NAME .5f,.795f,.9f,.875f
#define CHECKBOX -.9f,-.62f,-.3f,-.54f
#define TAB_LANBTN -.36f,.79f,-.1f,.88f
#define TAB_INETBTN -.08f,.79f,.18f,.88f
#define STATUSBAR -.93f,-.5f,.93f,-.41f

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
	//on our network (the lobby gives the LAN addresses only then): joined there
	if(g->numlan) snprintf(game->address,sizeof(game->address),"LAN %s",g->lan[0]);
	else snprintf(game->address,sizeof(game->address),"%s",g->address);
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
	joiningactive=JOINING_QUERY;
	joiningp2p=0;
	joiningsince=HKTicks();
	joininglastquery=joiningsince-1000;
	joininglanquery=joiningsince-1000;
	snprintf(status,sizeof(status),"Contacting %s at %s:%d...",g->name,g->address,g->port);
	//P2P if the host listens for it (it then has an identity) and we can
	if(g->identity[0]&&NetP2PMode()&&LobbySessionStart()>=0)
	{
		joiningactive=JOINING_SESSION;
		snprintf(status,sizeof(status),"Connecting to %s (P2P)...",g->name);
	}
	if(g->numlan)
		snprintf(status,sizeof(status),"Contacting %s on your network (%s)...",g->name,g->lan[0]);
}

//the host's LAN addresses (it is on our network): asked directly from the start,
//beside P2P or its public address, every second; -1, or the index in the server
//list of the first one that answered
static int JoinInternetLAN(DWORD now)
{
	for(int k=0;k<joining.numlan;k++)
	{
		int i=JoinNetFind(joining.lan[k],joining.port);
		if(i>=0) return i;
	}
	if(joining.numlan&&now-joininglanquery>=1000u)
	{
		for(int k=0;k<joining.numlan;k++) JoinNetQuery(joining.lan[k],joining.port);
		joininglanquery=now;
	}
	return -1;
}

static void JoinInternetConnect(int i)
{
	joiningactive=0;
	status[0]=0;
	char reason[200];
	if(JoinNetConnect(i,reason,sizeof(reason))) joinscreen=0;
	else snprintf(status,sizeof(status),"Can't join: %s",reason);
}

//the session is there (or not): P2P if it can be, else directly
static void JoinInternetQuery(int sessionok)
{
	const LobbySession *s=LobbySessionGet();
	joiningactive=JOINING_QUERY;
	joiningsince=HKTicks();
	joininglastquery=joiningsince;
	joiningp2p=sessionok&&s&&NetP2PStart(s,LobbyGetURL())&&
		JoinNetQueryP2P(joining.address,joining.port,joining.identity);
	if(!joiningp2p)
	{
		joininglastquery=joiningsince-1000;
		if(!joining.numlan)
			snprintf(status,sizeof(status),"Contacting %s at %s:%d...",joining.name,joining.address,joining.port);
	}
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
	int lan=joiningactive?JoinInternetLAN(HKTicks()):-1;
	if(lan>=0)
	{
		fprintf(stderr,"join: %s answered on the LAN at %s\n",joining.name,JoinGameAt(lan)->address);
		JoinInternetConnect(lan);
	}
	else if(joiningactive==JOINING_SESSION)
	{
		int r=LobbySessionResult(err,sizeof(err));
		if(r<0) fprintf(stderr,"%s\n",err);
		if(r) JoinInternetQuery(r>0);
		else if(HKTicks()-joiningsince>10000) JoinInternetQuery(0);
	}
	else if(joiningactive)
	{
		DWORD now=HKTicks();
		int i=JoinNetFind(joining.address,joining.port);
		if(i>=0) JoinInternetConnect(i);
		else if(now-joiningsince>(joiningp2p?20000u:6000u))
		{
			joiningactive=0;
			char where[60]="";
			if(joining.numlan) snprintf(where,sizeof(where),"on your network (%s) ",joining.lan[0]);
			if(joiningp2p)
				snprintf(status,sizeof(status),"No answer from %s, neither %sP2P nor directly",joining.name,
					joining.numlan?"on your network, ":"");
			else
				snprintf(status,sizeof(status),"No answer from %s %sor at %s:%d (the host may have to open its UDP ports)",
					joining.name,where,joining.address,joining.port);
		}
		else if(now-joininglastquery>=(joiningp2p?2000u:1000u))   //UDP: asked again every second
		{
			//P2P: through the connection once it is up; if it failed (GameNetworkingSockets
			//gives up after 10 s), directly
			if(joiningp2p) JoinNetRequestInfo(joining.address,joining.port);
			else JoinNetQuery(joining.address,joining.port);
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
			if(sortmode==SORT_NAME) swap=strcasecmp(x->name,y->name)>0;
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
		if(Inside(colx[c]-.01f,HEADY1,x2,HEADY2)) { sortmode=ColSort(c); return; }
	}
	int order[MAXLOCALLIST_UI];
	int n=Order(order);
	for(int r=0;r<n&&r<MAXROWS;r++)
	{
		if(Inside(ROWRECT(r)))
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

//a tab of the title bar
static void Tab(float x1,float y1,float x2,float y2,const char *label,int active)
{
	int over=!active&&Inside(x1,y1,x2,y2);
	UIRect(x1,y1,x2,y2,1,1,active?.5f:1,active?.2f:over?.14f:.06f);
	if(active) UIRect(x1,y1,x2,y1+.006f,1,1,.5f,.9f);
	if(active) glColor3f(1,1,.5f);
	else glColor3f(over?1:.7f,over?1:.7f,over?1:.7f);
	UIText((x1+x2-UITextWidth(label))/2,UITextY(y1,y2),label);
}

//status messages that are a step under way, not an error, end with "..."
static int InProgress(const char *text)
{
	int len=strlen(text);
	return len>=3&&!strcmp(&text[len-3],"...");
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
	UIPanel(-.95f,-.92f,.95f,PANELY2,TITLEH,"Join a game");
	Tab(TAB_LANBTN,"LAN / IP",tab==TAB_LAN);
	Tab(TAB_INETBTN,"Internet",tab==TAB_INTERNET);
	glColor3f(.6f,.6f,.6f);
	UIText(.47f-UITextWidth("Name"),UITextY(.795f,.875f),"Name");
	UIField(FIELD_NAME,playername,focus==FOCUS_NAME,"Player");

	glColor3f(.7f,.7f,.7f);
	float labely=UITextY(.63f,.72f);
	if(tab==TAB_INTERNET)
	{
		//private and friends games aren't listed: joined by the code their host gives
		UIText(-.9f,labely,"Code");
		UIField(FIELD_ADDRESS,code,focus==FOCUS_ADDRESS,"Game code from the host (6 letters)");
		UIButton(BTN_ADD,"Join",code[0]!=0&&!lookingup,1);
		UIButton(BTN_REFRESH,"Refresh",inetstate!=1);
	}
	else
	{
		UIText(-.9f,labely,"Address");
		UIField(FIELD_ADDRESS,address,focus==FOCUS_ADDRESS,"IP address or host name");
		UIButton(BTN_ADD,"Add",address[0]!=0);
		UIButton(BTN_REFRESH,"Refresh",1);
	}

	//list: column headers (click to sort), then the rows
	int order[MAXLOCALLIST_UI];
	int n=Order(order);
	UIRect(LISTX1,ROWTOP-MAXROWS*ROWH-.01f,LISTX2,HEADY2,0,0,0,.45f);
	UIRect(LISTX1,HEADY1,LISTX2,HEADY2,1,1,1,.06f);
	for(int c=0;c<6;c++)
	{
		int active=ColSort(c)!=SORT_FOUND&&ColSort(c)==sortmode;
		float x2=c<5?colx[c+1]:LISTX2;
		if(active) glColor3f(1,1,.5f);
		else if(ColSort(c)!=SORT_FOUND&&Inside(colx[c]-.01f,HEADY1,x2,HEADY2)) glColor3f(1,1,1);
		else glColor3f(.6f,.6f,.6f);
		float end=UIText(colx[c],UITextY(HEADY1,HEADY2,.045f),ColName(c),.045f);
		if(active) UIRect(colx[c],HEADY1+.008f,end,HEADY1+.013f,1,1,.5f,.8f);
	}
	UIRect(LISTX1,HEADY1-.004f,LISTX2,HEADY1,1,1,1,.2f);

	char text[120];
	if(!n)
	{
		const char *empty=GameCount()?"Only incompatible servers found (tick the box below to see them)":
			tab==TAB_INTERNET?inetmsg:"Searching the LAN... (F5 or Refresh to search again)";
		glColor3f(.55f,.55f,.55f);
		UITextFit(colx[0],UITextY(ROWTOP-ROWH,ROWTOP),empty,LISTX2-colx[0]-.03f);
	}
	for(int r=0;r<n&&r<MAXROWS;r++)
	{
		JoinGame *g=GameAt(order[r]);
		float y1=ROWTOP-(r+1)*ROWH,y=UITextY(y1,y1+ROWH);
		int hover=Inside(ROWRECT(r));
		if(order[r]==selected)
		{
			UIRect(ROWRECT(r),1,1,.5f,.2f);
			UIRect(LISTX1,y1,LISTX1+.008f,y1+ROWH,1,1,.5f,.9f);
		}
		else if(hover) UIRect(ROWRECT(r),1,1,1,.08f);
		else if(r&1) UIRect(ROWRECT(r),1,1,1,.025f);
		float dim=g->compatible?1:.45f;
		glColor3f(dim,dim,dim);
		Cell(colx[0],y,colx[1],g->name);
		glColor3f(dim*.8f,dim*.8f,dim*.8f);
		Cell(colx[1],y,colx[2],g->address);
		Cell(colx[2],y,colx[3],g->version);
		glColor3f(dim,dim,dim);
		if(g->players>=0) snprintf(text,sizeof(text),"%d/%d",g->players,g->maxplayers);
		else strcpy(text,"?");
		Cell(colx[3],y,colx[4],text);
		Cell(colx[4],y,colx[5],g->players<0?"?":g->gamemode?"Build":"CTF");
		if(tab==TAB_INTERNET)
		{
			glColor3f(dim,dim,dim*.6f);
			Cell(colx[5],y,LISTX2,lobbygames[order[r]].id);
		}
		else
		{
			if(!g->compatible) glColor3f(dim,dim,dim);
			else if(g->ping<80) glColor3f(.6f,1,.6f);
			else if(g->ping<200) glColor3f(1,1,.5f);
			else glColor3f(1,.55f,.45f);
			snprintf(text,sizeof(text),"%d ms",g->ping);
			Cell(colx[5],y,LISTX2,text);
		}
	}
	if(n>MAXROWS)
	{
		snprintf(text,sizeof(text),"+%d more: sort the list or hide the incompatible ones",n-MAXROWS);
		glColor3f(.5f,.5f,.5f);
		UIText(colx[0],ROWTOP-MAXROWS*ROWH-.055f,text,.04f);
	}

	//status bar: the last message, why the selected server can't be joined, or a hint
	JoinGame *sel=GameAt(selected);
	const char *line=status;
	char why[200];
	int kind=line[0]?(InProgress(line)?1:2):0;   //0 hint, 1 under way, 2 error
	if(!line[0]&&sel&&!sel->compatible)
	{
		snprintf(why,sizeof(why),"Can't join: %s",sel->reason);
		line=why;
		kind=2;
	}
	else if(!line[0]&&sel)
	{
		snprintf(why,sizeof(why),"%s  -  Enter or Join to play",sel->name);
		line=why;
	}
	else if(!line[0])
		line=tab==TAB_INTERNET?"Pick a public game, or type the code of a friend's game":
			"Pick a server found on the LAN, or add one by its address";
	if(kind==2) UIRect(STATUSBAR,.6f,.15f,.1f,.35f);
	else if(kind==1) UIRect(STATUSBAR,.5f,.45f,.1f,.3f);
	else UIRect(STATUSBAR,1,1,1,.04f);
	if(kind==2) glColor3f(1,.7f,.55f);
	else if(kind==1) glColor3f(1,1,.6f);
	else glColor3f(.75f,.75f,.75f);
	UITextFit(-.9f,UITextY(-.5f,-.41f),line,1.8f);

	//show incompatible servers: a check box
	int overbox=Inside(CHECKBOX);
	UIRect(-.9f,-.605f,-.86f,-.555f,1,1,1,overbox?.5f:.3f);
	UIRect(-.895f,-.6f,-.865f,-.56f,0,0,0,1);
	if(showincompatible) UIRect(-.888f,-.593f,-.872f,-.567f,1,1,.5f,1);
	glColor3f(overbox?1:.8f,overbox?1:.8f,overbox?1:.8f);
	UIText(-.83f,UITextY(-.62f,-.54f),"Show incompatible servers");
	glColor3f(.45f,.45f,.45f);
	snprintf(text,sizeof(text),"You: v%s (protocol %d)",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	UIText(.9f-UITextWidth(text),UITextY(-.62f,-.54f),text);

	UIButton(BTN_BACK,"Back",1);
	UIButton(BTN_JOIN,"Join",sel&&sel->compatible,1);
	const char *keys="Double-click or Enter: join    Tab: next field    F5: refresh    Esc: back";
	glColor3f(.45f,.45f,.45f);
	float w=UITextWidth(keys,.04f);
	UITextFit(-w/2>-.55f?-w/2:-.55f,UITextY(-.87f,-.76f,.04f),keys,1.1f,.04f);

	glPopAttrib();
	glColor3f(1,1,1);
}
