//game side of the edifice-lobby client (roadmap M7c, M7d, see lobbygame.h): the host
//gets a session (its network identity) and listens for P2P connections under it
//(netlink.h), announces its game, renews it at the interval the lobby asks for
//(sooner when the player count changes) and removes it when the server stops; the
//join screen lists the public games and looks up game codes, and gets a session of
//its own to join a game P2P.

#include "StdAfx.h"
#include "cfgfile.h"
#include "playing.h"
#include "multiplay.h"
#include "version.h"
#include "ui.h"
#include "lobbygame.h"
#include "netlink.h"
#include <stdio.h>
#include <string.h>

extern NetSettings netsettings;
extern ServerVars sv;

//---- settings and session ----

static char lobbyurl[201];
static LobbySession session;    //identity and token: host and client alike
static int havesession;
static DWORD sessiontime;
static int sessionrequest;

//the public edifice-lobby instance (server/), used when net.ini has no lobby= line
#define DEFAULT_LOBBY "https://edifice.rousseaufournier.com"

//reads lobby= from net.ini (every time: the player may have just edited it);
//an empty lobby= turns Internet games off
static void LoadLobbyURL()
{
	CfgFile ini;
	snprintf(lobbyurl,sizeof(lobbyurl),"%s",DEFAULT_LOBBY);
	if(ini.Load(UserFile("net.ini"))&&ini.GetValues("lobby"))
		CopyIniString(lobbyurl,ini.GetValues("lobby"),sizeof(lobbyurl));
	char before[sizeof(lobbyurl)];
	snprintf(before,sizeof(before),"%s",LobbyGetURL());
	LobbySetURL(lobbyurl,"Edifice/" EDIFICE_VERSION_STRING);
	if(strcmp(before,LobbyGetURL()))
	{
		havesession=0;   //another lobby: another identity
		if(sessionrequest) LobbyDrop(sessionrequest);
		sessionrequest=0;
	}
}

int LobbyReady(char *why,int whysize)
{
	LoadLobbyURL();
	const char *reason=0;
	if(!LobbyHttpAvailable()) reason="This build has no Internet game list (made without libcurl).";
	else if(!lobbyurl[0]) reason="No lobby configured: set lobby=https://... in net.ini.";
	else if(!LobbyGetURL()[0]) reason="The lobby address in net.ini (lobby=) isn't an http(s):// URL.";
	if(why&&whysize>0) snprintf(why,whysize,"%s",reason?reason:"");
	return !reason;
}

//what went wrong with a request, for the screen or the console
static void Describe(int status,const std::string &reply,char *out,int size)
{
	char msg[100];
	if(!status) snprintf(out,size,"Can't reach the lobby: %.80s",reply.c_str());
	else if(LobbyParseError(reply.data(),(int)reply.size(),msg,sizeof(msg)))
		snprintf(out,size,"Lobby: %s",msg);
	else snprintf(out,size,"Lobby: HTTP error %d",status);
}

//a session lasts 12 hours: a new one after 11
static int SessionValid()
{
	return havesession&&GetTickCount()-sessiontime<11*3600*1000u;
}

int LobbySessionStart()
{
	if(!LobbyReady()) return -1;
	if(SessionValid()) return 1;
	if(sessionrequest) return 0;
	sessionrequest=LobbySend("POST","/v1/sessions",0,0);
	return sessionrequest?0:-1;
}

int LobbySessionResult(char *err,int errsize)
{
	if(SessionValid()) return 1;
	if(!sessionrequest)
	{
		snprintf(err,errsize,"No session with the lobby");
		return -1;
	}
	int status;
	std::string reply;
	if(!LobbyPoll(sessionrequest,&status,&reply)) return 0;
	sessionrequest=0;
	if(status==201&&LobbyParseSession(reply.data(),(int)reply.size(),&session))
	{
		havesession=1;
		sessiontime=GetTickCount();
		return 1;
	}
	if(status==201) snprintf(err,errsize,"Lobby: unreadable session");
	else Describe(status,reply,err,errsize);
	return -1;
}

const LobbySession *LobbySessionGet()
{
	return SessionValid()?&session:0;
}

//---- host ----

enum { HOST_OFF, HOST_SESSION, HOST_ANNOUNCE, HOST_LISTED };

static int hoststate;
static int hostrequest;
static DWORD hostnext;          //GetTickCount() of the next request
static DWORD hostlastsent;
static int hostrefresh;         //seconds, from the lobby
static int hostplayers;         //as last announced
static int hostp2p;             //listening for P2P connections: identity announced
static char hostvisibility[8];
static char hostid[LOBBY_CODELEN+1];
static char hosttoken[65];

static void Say(const char *text)
{
	char line[200];
	snprintf(line,sizeof(line),"%s",text);
	ConsoleDisplayText(line);
}

//servervisibility of net.ini, "lan" unless it is an Internet one
static void ReadVisibility()
{
	CfgFile ini;
	strcpy(hostvisibility,"lan");
	if(ini.Load(UserFile("net.ini"))&&ini.GetValues("servervisibility"))
		CopyIniString(hostvisibility,ini.GetValues("servervisibility"),sizeof(hostvisibility));
	if(strcmp(hostvisibility,"public")&&strcmp(hostvisibility,"friends")&&strcmp(hostvisibility,"private"))
		strcpy(hostvisibility,"lan");
}

int LobbyHostInternet()
{
	ReadVisibility();
	return strcmp(hostvisibility,"lan")&&LobbyReady();
}

void LobbyHostStart()
{
	LobbyHostStop();
	ReadVisibility();
	if(!strcmp(hostvisibility,"lan")) return;
	char why[120];
	if(!LobbyReady(why,sizeof(why)))
	{
		Say(why);
		return;
	}
	//P2P first needs a session (our identity); without P2P, nothing to ask
	hostp2p=0;
	hoststate=NetP2PMode()&&LobbySessionStart()>=0?HOST_SESSION:HOST_ANNOUNCE;
	hostnext=GetTickCount();
	hostplayers=-1;
}

static void HostSend(int players)
{
	char body[600],path[64];
	const LobbySession *s=LobbySessionGet();
	LobbyAnnouncement a;
	a.name=netsettings.servername;
	a.port=ntohs(netsettings.infosendport);
	a.version=EDIFICE_VERSION_STRING;
	a.protocol=EDIFICE_PROTOCOL_VERSION;
	a.players=players;
	a.maxplayers=MAXPLAYERS;
	a.mode=sv.gamemode;
	a.visibility=hostvisibility;
	a.identity=hostp2p&&s?s->identity:"";   //the lobby lists it with its token only
	a.session=hostp2p&&s?s->token:"";
	if(LobbyAnnouncementJSON(&a,body,sizeof(body)))
	{
		if(hoststate==HOST_ANNOUNCE)
			hostrequest=LobbySend("POST","/v1/servers",body,0,1);   //IPv4: the address listed
		else
		{
			snprintf(path,sizeof(path),"/v1/servers/%s",hostid);
			hostrequest=LobbySend("PUT",path,body,hosttoken);
		}
		hostplayers=players;
	}
	hostlastsent=GetTickCount();
	if(!hostrequest) hostnext=GetTickCount()+5000;
}

void LobbyHostLoop(int players)
{
	if(hoststate==HOST_OFF) return;
	DWORD now=GetTickCount();
	char text[200];
	if(hoststate==HOST_SESSION)
	{
		//without a session (or P2P) the game is still listed: clients then join it
		//directly, through the host's open UDP ports
		int r=LobbySessionResult(text,sizeof(text));
		if(!r) return;
		if(r>0) hostp2p=NetP2PStart(LobbySessionGet(),LobbyGetURL());
		else fprintf(stderr,"%s\n",text);
		if(!hostp2p) Say("No P2P: players will have to reach your UDP ports");
		hoststate=HOST_ANNOUNCE;
		hostnext=now;
	}
	if(hostrequest)
	{
		int status;
		std::string reply;
		if(!LobbyPoll(hostrequest,&status,&reply)) return;
		hostrequest=0;
		if(hoststate==HOST_ANNOUNCE)
		{
			if(status==201&&LobbyParseAnnounced(reply.data(),(int)reply.size(),hostid,sizeof(hostid),
				hosttoken,sizeof(hosttoken),&hostrefresh))
			{
				hoststate=HOST_LISTED;
				hostnext=now+hostrefresh*1000;
				if(!strcmp(hostvisibility,"public"))
					snprintf(text,sizeof(text),"Listed on the lobby, game code %s",hostid);
				else
					snprintf(text,sizeof(text),"Game code %s (give it to the players)",hostid);
				Say(text);
				fprintf(stderr,"lobby: listed, game code %s\n",hostid);
			}
			else
			{
				Describe(status,reply,text,sizeof(text));
				Say(text);
				fprintf(stderr,"%s\n",text);
				hostnext=now+30000;   //try again later
			}
		}
		else if(status==204)
			hostnext=now+hostrefresh*1000;
		else if(status==403||status==404)
		{
			//expired (the host was unreachable for a while) or the lobby restarted:
			//announced again, with a new code
			hoststate=HOST_ANNOUNCE;
			hostnext=now;
		}
		else
			hostnext=now+5000;
		return;
	}
	//a change in the player count shows soon, at most every 5 s
	if(hoststate==HOST_LISTED&&players!=hostplayers&&now-hostlastsent>=5000) hostnext=now;
	if((int)(now-hostnext)>=0) HostSend(players);
}

void LobbyHostStop()
{
	if(hostrequest) LobbyDrop(hostrequest);
	if(hoststate==HOST_LISTED)
	{
		char path[64];
		snprintf(path,sizeof(path),"/v1/servers/%s",hostid);
		LobbyDrop(LobbySend("DELETE",path,0,hosttoken));
	}
	hostrequest=0;
	hoststate=HOST_OFF;
	hostid[0]=hosttoken[0]=0;
}

const char *LobbyHostCode()
{
	return hoststate==HOST_LISTED?hostid:"";
}

const char *LobbyHostVisibility()
{
	return hostvisibility;
}

void LobbyQuit()
{
	LobbyHostStop();
	LobbyShutdown(2000);
}

//---- join screen ----

static int listrequest;
static int lookuprequest;

int LobbyListRequest()
{
	if(!LobbyReady()) return 0;
	if(listrequest) LobbyDrop(listrequest);
	//every protocol: the others are shown as incompatible, like on the LAN
	listrequest=LobbySend("GET","/v1/servers?protocol=0",0,0);
	return listrequest!=0;
}

int LobbyListResult(LobbyGame *games,int max,int *count,char *err,int errsize)
{
	int status;
	std::string reply;
	if(!listrequest||!LobbyPoll(listrequest,&status,&reply)) return 0;
	listrequest=0;
	*count=0;
	if(status!=200)
	{
		Describe(status,reply,err,errsize);
		return -1;
	}
	int n=LobbyParseList(reply.data(),(int)reply.size(),games,max);
	if(n<0)
	{
		snprintf(err,errsize,"Lobby: unreadable answer");
		return -1;
	}
	*count=n;
	return 1;
}

int LobbyLookupRequest(const char *code)
{
	char id[LOBBY_CODELEN+1],path[64];
	if(!LobbyNormalizeCode(code,id,sizeof(id))||!LobbyReady()) return 0;
	if(lookuprequest) LobbyDrop(lookuprequest);
	snprintf(path,sizeof(path),"/v1/servers/%s",id);
	lookuprequest=LobbySend("GET",path,0,0);
	return lookuprequest!=0;
}

int LobbyLookupResult(LobbyGame *game,char *err,int errsize)
{
	int status;
	std::string reply;
	if(!lookuprequest||!LobbyPoll(lookuprequest,&status,&reply)) return 0;
	lookuprequest=0;
	if(status==404)
	{
		snprintf(err,errsize,"No game with this code (it may have ended)");
		return -1;
	}
	if(status!=200)
	{
		Describe(status,reply,err,errsize);
		return -1;
	}
	if(!LobbyParseGame(reply.data(),(int)reply.size(),game))
	{
		snprintf(err,errsize,"Lobby: unreadable answer");
		return -1;
	}
	return 1;
}
