//The dedicated server (dedicated.h): flags and HK_* variables over net.ini and sv.ini,
//then the game's own server (StartServer, Playloop, SockLoop) at its 60 Hz tick with
//no window: sleeping between ticks, the console log on stdout, console commands from
//stdin, SIGINT/SIGTERM to stop (the players are told, the game leaves the lobby).
//The same code in edifice-server (no SDL, renderer_null.cpp, sound_null.cpp) and in
//the game run with --dedicated (SDL without video or audio, the renderer headless).

#include "dedicated.h"
#include "platform.h"
#include "hktime.h"
#include "hksocket.h"
#include "cfgfile.h"
#include "renderer.h"
#include "hugekilla.h"
#include "playing.h"
#include "multiplay.h"
#include "multiplaystruct.h"
#include "gameplay.h"
#include "lobbygame.h"
#include "netlink.h"
#include "sock.h"
#include "version.h"
#include "ui.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

static int dedicated;
static volatile sig_atomic_t quitsignal;   //SIGINT/SIGTERM, or "quit"
static std::string mapname;                //--map / HK_MAP

int HKDedicated()
{
	return dedicated;
}

void DedicatedQuit()
{
	quitsignal=1;
}

//---- flags ----

static void SetEnv(const char *name,const char *value)
{
#ifdef _WIN32
	_putenv_s(name,value);
#else
	setenv(name,value,1);
#endif
}

//a flag and the variable it sets: the game reads some of them itself (HK_P2P,
//HK_TRANSPORT, HK_SEED, HK_CONSOLE_RUN, HK_USERDIR, HUGEKILLA_DATA); the others
//override a setting of net.ini or sv.ini (ApplySettings)
struct DedicatedFlag
{
	const char *flag;
	const char *env;
	const char *arg;
	const char *help;
};

static const DedicatedFlag flags[]=
{
	{"--port","HK_PORT","N","UDP port of the server (net.ini infosendport, default 27020); the players' own\n"
		"                          ports follow it (N+2 and up, one each, with plain UDP)"},
	{"--name","HK_SERVER_NAME","TEXT","name in the game lists (net.ini servername)"},
	{"--max-players","HK_MAX_PLAYERS","N","players it takes, 1-31 (net.ini maxplayers; 31 without it)"},
	{"--visibility","HK_VISIBILITY","V","lan (not announced), public (listed on the lobby), friends or private\n"
		"                          (not listed, joined by the game code) (net.ini servervisibility)"},
	{"--lobby","HK_LOBBY","URL","edifice-lobby to announce the game on, \"\" for none (net.ini lobby)"},
	{"--p2p","HK_P2P","1|0|relay","Internet games: P2P through the lobby, never, or by its TURN relay only\n"
		"                          (net.ini p2p; needs a build with GameNetworkingSockets)"},
	{"--transport","HK_TRANSPORT","udp|gns","connections by address in plain UDP or with GameNetworkingSockets\n"
		"                          (net.ini transport)"},
	{"--mode","HK_MODE","ctf|sandbox","game mode (sv.ini gamemode: 0 capture the flag, 1 sandbox)"},
	{"--map","HK_MAP","NAME|FILE","a saved map (maps/NAME.map in the user folder, or a .map file) instead\n"
		"                          of a random land"},
	{"--seed","HK_SEED","N","seed of the random land, sky and spawns (the same world every run)"},
	{"--exec","HK_CONSOLE_RUN","\"A;B\"","console commands once the server runs, ';'-separated: server vars\n"
		"                          (\"timelimit 20;capturelimit 3\"), resetgame..."},
	{"--userdir","HK_USERDIR","DIR","settings folder: net.ini, sv.ini, maps/ (default: the game's)"},
	{"--data","HUGEKILLA_DATA","DIR","the game's data folder (default: data/ next to the program)"},
};
#define NUMFLAGS (int)(sizeof(flags)/sizeof(flags[0]))

static void Usage(FILE *out)
{
	fprintf(out,"Edifice %s dedicated server (protocol %d)\n\n"
		"usage: edifice-server [flags]   (or: Edifice --dedicated [flags], HK_DEDICATED=1)\n\n",
		EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	for(int i=0;i<NUMFLAGS;i++)
	{
		char left[64];
		snprintf(left,sizeof(left),"%s %s",flags[i].flag,flags[i].arg);
		fprintf(out,"  %-24s%s\n  %-24s(%s)\n",left,flags[i].help,"",flags[i].env);
	}
	fprintf(out,"  %-24s%s\n","--healthcheck","exit 0 if the server on this port answers (Docker's healthcheck)");
	fprintf(out,"  %-24s%s\n","--version","print the version");
	fprintf(out,"\nA flag wins over its variable, which wins over net.ini and sv.ini (the user folder's).\n"
		"Console commands on stdin: help, status, kick <n>, say <text>, <server var> [value], quit.\n"
		"SIGINT or SIGTERM stops the server: its players are told, the game leaves the lobby.\n");
}

static int Fail(const char *what,const char *value)
{
	fprintf(stderr,"edifice-server: %s: \"%s\" (--help)\n",what,value);
	return 2;
}

static int ParsePositive(const char *s,int lo,int hi,int *out)
{
	char *end;
	long v=strtol(s,&end,10);
	if(!*s||*end||v<lo||v>hi) return 0;
	*out=(int)v;
	return 1;
}

//the HK_* variables of net.ini and sv.ini as overrides (cfgfile.h); 0, or an exit code
static int ApplySettings()
{
	const char *v;
	if((v=getenv("HK_PORT"))&&*v)
	{
		int port;
		if(!ParsePositive(v,1,65535-2*MAXPLAYERS-1,&port)) return Fail("bad port",v);
		char text[16];
		snprintf(text,sizeof(text),"%d",port);
		CfgFile::Override("net.ini","infosendport",text);
		snprintf(text,sizeof(text),"%d",port+1);
		CfgFile::Override("net.ini","datasendport",text);
	}
	if((v=getenv("HK_SERVER_NAME"))&&*v)
		CfgFile::Override("net.ini","servername",v);
	if((v=getenv("HK_MAX_PLAYERS"))&&*v)
	{
		int n;
		if(!ParsePositive(v,1,MAXPLAYERS-1,&n)) return Fail("max players: 1 to 31",v);
		CfgFile::Override("net.ini","maxplayers",v);
	}
	if((v=getenv("HK_VISIBILITY"))&&*v)
	{
		if(strcmp(v,"lan")&&strcmp(v,"public")&&strcmp(v,"friends")&&strcmp(v,"private"))
			return Fail("visibility: lan, public, friends or private",v);
		CfgFile::Override("net.ini","servervisibility",v);
	}
	if((v=getenv("HK_LOBBY")))   //empty: no lobby
		CfgFile::Override("net.ini","lobby",v);
	if((v=getenv("HK_MODE"))&&*v)
	{
		const char *mode=!strcmp(v,"ctf")||!strcmp(v,"0")?"0":!strcmp(v,"sandbox")||!strcmp(v,"1")?"1":0;
		if(!mode) return Fail("mode: ctf or sandbox",v);
		CfgFile::Override("sv.ini","gamemode",mode);
	}
	if((v=getenv("HK_SEED"))&&*v)
	{
		int seed;
		if(!ParsePositive(v,0,0x7fffffff,&seed)) return Fail("bad seed",v);
	}
	if((v=getenv("HK_MAP"))&&*v) mapname=v;
	return 0;
}

//--healthcheck: an INFO answer from the server on 127.0.0.1 (HK_PORT, else 27020), as
//the join screen's query: 0 healthy, 1 not
static int Healthcheck()
{
	int port=27020;
	const char *v=getenv("HK_PORT");
	if(v&&*v&&!ParsePositive(v,1,65535,&port)) return 1;
	HKSocketsInit();
	SOCKET s=socket(AF_INET,SOCK_DGRAM,0);
	if(s==INVALID_SOCKET) return 1;
	SOCKADDR_IN to;
	memset(&to,0,sizeof(to));
	to.sin_family=AF_INET;
	to.sin_port=htons((unsigned short)port);
	to.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
	//a Sock datagram: a 0, then each message with its 16-bit length
	unsigned char packet[3+SIMPLESIZE];
	packet[0]=0;
	packet[1]=SIMPLESIZE;
	packet[2]=0;
	WriteSimple(packet+3,REQUESTINFO,0,0);
	int ok=0;
	for(int attempt=0;attempt<3&&!ok;attempt++)
	{
		sendto(s,(const char *)packet,sizeof(packet),0,(struct sockaddr *)&to,sizeof(to));
		fd_set set;
		FD_ZERO(&set);
		FD_SET(s,&set);
		timeval wait={1,0};
		if(select((int)s+1,&set,0,0,&wait)<=0) continue;
		unsigned char reply[2048];
		int n=(int)recv(s,(char *)reply,sizeof(reply),0);
		//the first message is INFO
		ok=n>=4&&reply[0]==0&&reply[3]==INFO;
	}
	closesocket(s);
	HKSocketsQuit();
	if(!ok) fprintf(stderr,"edifice-server: no answer on udp port %d\n",port);
	return ok?0:1;
}

int DedicatedArgs(int argc,char **argv,int always,int *exitcode)
{
	*exitcode=-1;
	const char *env=getenv("HK_DEDICATED");
	int on=always||(env&&*env&&strcmp(env,"0"));
	for(int i=1;i<argc;i++)
		if(!strcmp(argv[i],"--dedicated")) on=1;
	if(!on) return 0;
	int healthcheck=0,positional=0;
	for(int i=1;i<argc;i++)
	{
		const char *a=argv[i];
		if(!strcmp(a,"--dedicated")) continue;
		if(!strcmp(a,"--help")||!strcmp(a,"-h"))
		{
			Usage(stdout);
			*exitcode=0;
			return 1;
		}
		if(!strcmp(a,"--version"))
		{
			printf("Edifice %s (protocol %d)\n",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
			*exitcode=0;
			return 1;
		}
		if(!strcmp(a,"--healthcheck"))
		{
			healthcheck=1;
			continue;
		}
		if(a[0]!='-'&&!positional)   //as the game: the data folder
		{
			positional=1;
			SetEnv("HUGEKILLA_DATA",a);
			continue;
		}
		int f;
		const char *value=0;
		for(f=0;f<NUMFLAGS;f++)
		{
			size_t n=strlen(flags[f].flag);
			if(strncmp(a,flags[f].flag,n)) continue;
			if(a[n]=='=') value=a+n+1;
			else if(!a[n]&&i+1<argc) value=argv[++i];
			else if(a[n]) continue;   //another flag starting the same
			break;
		}
		if(f==NUMFLAGS)
		{
			*exitcode=Fail("unknown flag",a);
			return 1;
		}
		if(!value)
		{
			*exitcode=Fail("missing value",a);
			return 1;
		}
		SetEnv(flags[f].env,value);
	}
	int err=ApplySettings();
	if(err)
	{
		*exitcode=err;
		return 1;
	}
	if(healthcheck) *exitcode=Healthcheck();
	dedicated=1;
	return 1;
}

//---- console on stdin ----

static std::mutex stdinlock;
static std::deque<std::string> stdinlines;

//its own thread: fgets blocks. Ends at the end of stdin (none in a container started
//without -i: /dev/null); never joined, the process ends with it
static void ReadStdin()
{
	char line[512];
	while(fgets(line,sizeof(line),stdin))
	{
		size_t n=strcspn(line,"\r\n");
		line[n]=0;
		std::lock_guard<std::mutex> lock(stdinlock);
		stdinlines.push_back(line);
	}
}

static void Status()
{
	char text[300];
	int players=0;
	for(int i=1;i<GetNumPlayers();i++)
	{
		if(!PlayerSlotUsed(i)) continue;
		Player *p=GetPlayer(i);
		const char *state=p->playstate==10?"playing":p->playstate==11?"dead":"joining";
		snprintf(text,sizeof(text),"  %2d %-20.31s %-6s %-8s kills %d deaths %d",i,p->name,teamname(p->team),state,p->kills,p->deaths);
		ConsoleDisplayText(text);
		players++;
	}
	const char *code=LobbyHostCode();
	snprintf(text,sizeof(text),"%d/%d players, %s, %s%s%s",players,ServerListedMaxPlayers(),
		sv.gamemode?"sandbox":"capture the flag",LobbyHostVisibility(),code[0]?", game code ":"",code);
	ConsoleDisplayText(text);
}

static void Execute(const char *line)
{
	while(*line==' ') line++;
	if(!*line) return;
	if(!strcmp(line,"quit")||!strcmp(line,"exit")||!strcmp(line,"stop"))
	{
		quitsignal=1;
		return;
	}
	if(!strcmp(line,"status"))
	{
		Status();
		return;
	}
	ConsoleExecute(line);
	if(!strcmp(line,"help"))
	{
		char more[]="  status                   the players (numbers for kick)\r"
			"  quit                     stop the server (as SIGINT, SIGTERM)";
		ConsoleDisplayText(more);
	}
}

//HK_CONSOLE_RUN (--exec): ';'-separated commands
static void ExecuteList(const char *run)
{
	char cmd[256];
	while(run&&*run)
	{
		size_t n=strcspn(run,";");
		snprintf(cmd,sizeof(cmd),"%.*s",(int)n,run);
		Execute(cmd);
		run+=n+(run[n]==';');
	}
}

//---- the server ----

static void OnSignal(int)
{
	if(quitsignal) _Exit(1);   //a second one: at once, the clean way is stuck
	quitsignal=1;
}

static void Banner()
{
	char text[400];
	snprintf(text,sizeof(text),"Edifice %s dedicated server (protocol %d)",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	ConsoleDisplayText(text);
	const char *transports="udp";
#if defined(EDIFICE_WEBRTC)
	transports="udp, gns, p2p, webrtc (browsers)";
#elif defined(EDIFICE_GNS)
	transports="udp, gns, p2p";
#endif
	std::error_code err;
	snprintf(text,sizeof(text),"settings: %s, data: %s",HKUserDir()[0]?HKUserDir():"(the data folder)",
		std::filesystem::current_path(err).string().c_str());
	ConsoleDisplayText(text);
	snprintf(text,sizeof(text),"transports built in: %s",transports);
	ConsoleDisplayText(text);
}

static void Summary()
{
	char text[400];
	snprintf(text,sizeof(text),"\"%s\" on udp port %d, %d players at most, %s, %s",netsettings.servername,
		ntohs(netsettings.infosendport),ServerListedMaxPlayers(),sv.gamemode?"sandbox":"capture the flag",
		LobbyHostVisibility());
	ConsoleDisplayText(text);
}

int DedicatedRun()
{
	setvbuf(stdout,0,_IOLBF,BUFSIZ);   //each log line flushed too (Windows has no line buffering)
	signal(SIGINT,OnSignal);
	signal(SIGTERM,OnSignal);
#ifndef _WIN32
	signal(SIGPIPE,SIG_IGN);   //a socket closed under a send: an error, not the end
#endif
	RSetHeadless();
	const char *seed=getenv("HK_SEED");
	srand(seed&&*seed?(unsigned)atoi(seed):(unsigned)time(NULL));

	InitConsole();   //the server vars, from sv.ini
	consolevisible=0;
	Banner();
	InitSocks();
	for(int i=0;i<MAXPLAYERS;i++)
	{
		GetPlayer(i)->playstate=0;
		GetPlayer(i)->dir=Vector(1,0,0);
		GetPlayer(i)->tilt=Vector(1,0,0);
		GetPlayer(i)->fwd=Vector(1,0,0);
		GetPlayer(i)->gunup=Vector(0,1,0);
		GetPlayer(i)->fzoom=0;
	}

	//the world: a saved map (--map, HK_MAP; HK_MAPFILE as the game's), else a random land
	std::string map=mapname;
	if(map.empty()&&getenv("HK_MAPFILE")) map=getenv("HK_MAPFILE");
	if(!map.empty())
	{
		int file=map.find('/')!=std::string::npos||map.find('\\')!=std::string::npos||
			(map.size()>4&&!strcasecmp(map.c_str()+map.size()-4,".map"));
		char err[300];
		if(!LoadMapPath(file?map.c_str():MapPath(map.c_str()),err,sizeof(err)))
		{
			fprintf(stderr,"edifice-server: map %s: %s\n",map.c_str(),err);
			CloseSocks();
			return 1;
		}
	}
	else
		StartServer();
	if(!IsServer())
	{
		fprintf(stderr,"edifice-server: the server didn't start\n");
		CloseSocks();
		return 1;
	}
	Summary();
	ExecuteList(getenv("HK_CONSOLE_RUN"));
	std::thread(ReadStdin).detach();

	//the game's frame without the drawing: the ticks due (Playloop), the network
	//(SockLoop), then asleep until the next tick: 60 passes a second
	while(!quitsignal)
	{
		std::deque<std::string> lines;
		{
			std::lock_guard<std::mutex> lock(stdinlock);
			lines.swap(stdinlines);
		}
		for(const std::string &l:lines) Execute(l.c_str());
		int64_t start=HKPerfCounter();
		curtime=HKPerfMillis();
		Playloop();
		SockLoop();
		double spent=(double)(HKPerfCounter()-start)*1000/(double)HKPerfFrequency();
		//a little past the tick: woken just before it, Playloop would run none
		double wait=PlayNextTickMs()-spent+.25;
		if(wait>0) std::this_thread::sleep_for(std::chrono::microseconds((long long)(wait*1000)));
	}

	char stopping[]="Shutting down";
	ConsoleDisplayText(stopping);
	char bye[]="The server is shutting down";
	SendChatMessage(bye,CMSG_INFO);
	SockLoop();      //sends it
	CloseSocks();    //EXIT to every player, the game off the lobby (LobbyQuit waits for it)
	char stopped[]="Stopped";
	ConsoleDisplayText(stopped);
	return 0;
}
