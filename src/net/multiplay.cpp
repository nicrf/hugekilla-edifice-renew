//all the necessary functions to connect to a server, and perform all necessary communications
//like sending a building, sending the land information, movement, chat messages
//uses the class Sock intensively

#include "StdAfx.h"
#include "winsock2.h"
#include "cfgfile.h"
#include "resource.h"
#include "multiplay.h"
#include "land.h"
#include "hugekilla.h"
#include "redrawing.h"
#include "playing.h"
#include "multiplaystruct.h"
#include "math.h"
#include "mmsystem.h"
#include "sound.h"
#include "stdio.h"
#include "gameplay.h"
#include "sock.h"
#include "udplink.h"
#include "netlink.h"
#include "version.h"
#include "wire.h"
#include "netmsg.h"
#include "ui.h"
#include "voice.h"
#include "lobbygame.h"
#include "anticheat.h"
#include "hmac.h"
#include <stdarg.h>
#define MAXLOCALLIST 128
#define PEERTIMEOUT 70000     //ms without a message before a peer is dropped
#define SERVERSILENCE 10000   //ms: the server is gone, once in the game (ClientLoop)
#ifndef HK_WIN32_NATIVE
#ifndef _WIN32
#include <netdb.h>
#endif
#include <SDL.h>
#endif

extern int consolevisible;
extern DWORD curtime;
extern Object object[MAXOBJS];
extern AseObj projectile[20];
extern ServerVars sv;
extern ServerObject sobject[MAXSERVEROBJS];
extern LandDefs ldfs;


RemoteGameData rgd;
WORD curport;
NetSettings netsettings;
int server;
int connected;

Sock infosock;
Sock datasock;
Sock clientsock[MAXPLAYERS];

int numplayers;

HWND dlghwnd;

int numlocallistaddr;
SOCKADDR_IN locallistaddr[MAXLOCALLIST];

char blockbuffer[50000];
DWORD blockbufferpos=0;
DWORD numblocks;

DWORD nextresend;

char curobjective[1000];
char curscore[1000];
int SendMultMessage(const void *msg,int len,int player);




//used to make a SIMPLE message, serialized: send simple.b,sizeof(simple.b)

SimpleBuf MakeSimple(char type,DWORD msg,DWORD msg2)
{
	SimpleBuf simple;
	WriteSimple(simple.b,type,msg,msg2);
	return simple;
}

//used to convert an ip address into a sockaddr

SOCKADDR_IN MakeSockAddr(char a1,char a2,char a3,char a4,WORD port)
{
	SOCKADDR_IN addr;
	//ZeroMemory(&addr,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=port;
	unsigned char *b=(unsigned char *)&addr.sin_addr;
	b[0]=a1;
	b[1]=a2;
	b[2]=a3;
	b[3]=a4;
	return addr;
}

//---- game/protocol version: src/net/version.h, version.cpp ----

//join cookie the server at entercookieip asked for (ENTERCOOKIE, M11 phase 2)
static unsigned char entercookie[HELLOCOOKIE_SIZE];
static unsigned int entercookieip;
static int haveentercookie;

//client asks infosock.dest to join, with its version, its capabilities and the
//cookie that server gave it, if any
static void SendRequestEnter()
{
	char buf[2+32+VERSIONTRAILER_MAX+HELLOCAPS_MAX];
	int nl=strlen(netsettings.playername);
	if(nl>31) nl=31;
	buf[0]=REQUESTENTER;
	buf[1]=nl;
	memcpy(&buf[2],netsettings.playername,nl);
	buf[2+nl]=0;
	int len=2+nl+1;
	len+=WriteVersionTrailer(&buf[len]);
	int cookie=haveentercookie&&entercookieip==infosock.dest.sin_addr.s_addr;
	len+=WriteHelloCaps(&buf[len],HELLOCAP_COOKIE,cookie?entercookie:0);
	infosock.Send(buf,len);
	infosock.Flush();
}

extern int ac_enable,ac_requirecookie;

//server answers an info request, with its version
static void SendInfo(SOCKADDR_IN dest)
{
	char buf[2+100+VERSIONTRAILER_MAX+5];
	int nl=strlen(netsettings.servername);
	if(nl>99) nl=99;
	buf[0]=INFO;
	buf[1]=nl;
	memcpy(&buf[2],netsettings.servername,nl);
	buf[2+nl]=0;
	int len=2+nl+1;
	len+=WriteVersionTrailer(&buf[len]);
	//server details for the join list (ignored by older clients)
	buf[len++]=(char)numplayers;
	buf[len++]=(char)MAXPLAYERS;
	buf[len++]=(char)sv.gamemode;
	buf[len++]=(char)sv.capturelimit;
	//capabilities (M11 phase 2, version.h; ignored by older clients)
	buf[len++]=(char)(ac_enable?HELLOCAP_COOKIE|(ac_requirecookie?HELLOCAP_REQUIRECOOKIE:0):0);
	infosock.dest=dest;
	infosock.Send(buf,len);
	infosock.Flush();
}

//server turns a client down: a SIMPLE message new clients understand, plus a
//console text (CMSG_INFO chat) that original clients display
static void RefuseClient(SOCKADDR_IN dest,char why,const char *text)
{
	infosock.dest=dest;
	SimpleBuf refuse=MakeSimple(why,EDIFICE_PROTOCOL_VERSION,0);
	infosock.Send(refuse.b,sizeof(refuse.b));
	char chat[300];
	int len=strlen(text);
	if(len>250) len=250;
	ByteWriter w(chat,sizeof(chat));
	w.U8(CHATMESSAGE);
	w.U8(CMSG_INFO);
	w.U8(0);
	w.U16(len);
	w.Bytes(text,len);
	infosock.Send(chat,w.Size());
	infosock.Flush();
}

//---- validation of what comes from the network (roadmap M2) ----

static int ValidPlayer(int n)
{
	return n>=0&&n<MAXPLAYERS;
}

static int ValidSObj(int n)
{
	return n>=0&&n<MAXSERVEROBJS;
}

static int ValidWeapon(int n)
{
	return n>=0&&n<10;
}

//wire format of each message: src/net/netmsg.h

//PLAYERNAMES: uint8 count, then per player: uint8 number, int32 kills,
//int32 deaths, int8 team, uint8 name length (<=31), name
static void WritePlayerName(ByteWriter &w,int i)
{
	Player *pl=GetPlayer(i);
	int namelen=strlen(pl->name);
	if(namelen>31) namelen=31;
	w.U8(i);
	w.I32(pl->kills);
	w.I32(pl->deaths);
	w.U8((unsigned char)pl->team);
	w.U8(namelen);
	w.Bytes(pl->name,namelen);
}

//---- anti-cheat (roadmap M11 phase 1, docs/M11.md; the logic is in anticheat.h) ----

//server vars, from the console or sv.ini; the defaults never get in the way of
//normal play. 0 turns a check off.
int ac_enable=1;            //0: no check at all
int ac_killdamage=200;      //damage the server counts on a player before killing it itself
int ac_hitcap=100;          //at most this much counted per bullet or grenade
int ac_deadtime=5000;       //ms a player killed by the server stays dead
int ac_killinterval=2000;   //ms between two deaths a client may report
float ac_firetolerance=.75f;//shortest shot interval accepted, times the weapon's
int ac_fireburst=300;       //ms of shots that may arrive bunched up (jitter)
int ac_ammoslack=2;         //rounds the server's magazine may be off by, -1: off
float ac_maxspeed=150;      //units/s between two positions above which it is logged
int ac_firedist=20;         //units between a shot and its shooter above which it is logged
int ac_builddist=50;        //blocks from the player a block may be placed or removed
int ac_ipinfo=5;            //info requests answered per second and IP
int ac_ipenter=30;          //join requests per minute and IP
int ac_ippackets=50;        //messages per second and IP on the info port
int ac_requirecookie=0;     //1: refuse clients that can't answer a join cookie (0.5.x and older)
int ac_cookietime=30000;    //ms of a join cookie time bucket (valid one to two buckets)

#define AC_KILL 0
#define AC_DEATH 1
#define AC_FIRE 2
#define AC_AMMO 3
#define AC_MOVE 4
#define AC_BUILD 5
#define AC_IP 6
#define AC_STATE 7
#define AC_ORIGIN 8
#define ACKINDS 9
static const char *ackind[ACKINDS]={"kill","death","fire","ammo","move","build","ip","state","origin"};

struct AcPlayer
{
	float damage;        //counted since its last death
	DWORD deaduntil;     //killed by the server: dead until then
	DWORD lastdeath;     //last death counted, 0: none
	int deathcounted;    //a death since the last respawn (its next 11 -> 10 is one)
	DWORD lastfire;
	AcBucket fire;
	AcAmmo ammo;
	AcMove move;
};
static AcPlayer acplayer[MAXPLAYERS];
#define ACIPTABLE 64
static AcIpEntry aciptable[ACIPTABLE];

//each kind of event: 3 console lines, then one every 2 s; HK_ANTICHEAT_LOG=1 also
//writes them on stderr (20, then 10 per second)
static void AcLog(int kind,const char *fmt,...)
{
	static AcBucket console[ACKINDS],err[ACKINDS];
	static int suppressed[ACKINDS];
	static int toerr=getenv("HK_ANTICHEAT_LOG")!=0;
	char text[300];
	va_list args;
	va_start(args,fmt);
	vsnprintf(text,sizeof(text),fmt,args);
	va_end(args);
	if(toerr&&AcTake(&err[kind],curtime,.01f,20)) fprintf(stderr,"anticheat: %s: %s\n",ackind[kind],text);
	if(AcTake(&console[kind],curtime,.0005f,3))
	{
		char line[400];
		if(suppressed[kind]) snprintf(line,sizeof(line),"anticheat %s: %s (%d more)",ackind[kind],text,suppressed[kind]);
		else snprintf(line,sizeof(line),"anticheat %s: %s",ackind[kind],text);
		suppressed[kind]=0;
		ConsoleDisplayText(line);
	}
	else suppressed[kind]++;
}

//the weapon tables of sv (GetAmmoHold has no default case)
static int AcTimeout(int w)
{
	int t[7]={sv.gattimeout,sv.m16timeout,sv.steyrtimeout,sv.umptimeout,sv.grenadetimeout,sv.berettatimeout,sv.sr1000timeout};
	return w>=0&&w<7?t[w]:0;
}
static int AcReloadTime(int w)
{
	int t[7]={sv.gatreloadtime,sv.m16reloadtime,sv.steyrreloadtime,sv.umpreloadtime,sv.grenadereloadtime,sv.berettareloadtime,sv.sr1000reloadtime};
	return w>=0&&w<7?t[w]:0;
}
static void AcHolds(int *hold)
{
	int t[7]={sv.gatammohold,sv.m16ammohold,sv.steyrammohold,sv.umpammohold,sv.grenadeammohold,sv.berettaammohold,sv.sr1000ammohold};
	for(int w=0;w<ACWEAPONS;w++) hold[w]=w<7?t[w]:0;
}

//console vars, with their value in sv.ini if it has one (after InitConsole)
static void AcInitVars()
{
	CfgFile svini;
	int loaded=svini.Load(UserFile("sv.ini"));
	struct { int *i; float *f; const char *name; } vars[]={
		{&ac_enable,0,"ac_enable"},{&ac_killdamage,0,"ac_killdamage"},{&ac_hitcap,0,"ac_hitcap"},
		{&ac_deadtime,0,"ac_deadtime"},{&ac_killinterval,0,"ac_killinterval"},
		{0,&ac_firetolerance,"ac_firetolerance"},{&ac_fireburst,0,"ac_fireburst"},{&ac_ammoslack,0,"ac_ammoslack"},
		{0,&ac_maxspeed,"ac_maxspeed"},{&ac_firedist,0,"ac_firedist"},{&ac_builddist,0,"ac_builddist"},
		{&ac_ipinfo,0,"ac_ipinfo"},{&ac_ipenter,0,"ac_ipenter"},{&ac_ippackets,0,"ac_ippackets"},
		{&ac_requirecookie,0,"ac_requirecookie"},{&ac_cookietime,0,"ac_cookietime"},
	};
	//debug (tests): HK_AC_VARS="ac_requirecookie=1 ac_cookietime=1000" wins over sv.ini
	const char *env=getenv("HK_AC_VARS");
	for(unsigned int v=0;v<sizeof(vars)/sizeof(vars[0]);v++)
	{
		char *val=loaded?svini.GetValues(vars[v].name):0;
		static char envval[32];
		int nl=strlen(vars[v].name);
		for(const char *e=env;e&&*e;)
		{
			while(*e==' '||*e==',') e++;
			if(!strncmp(e,vars[v].name,nl)&&e[nl]=='=')
			{
				snprintf(envval,sizeof(envval),"%.*s",(int)strcspn(e+nl+1," ,"),e+nl+1);
				val=envval;
			}
			e+=strcspn(e," ,");
		}
		if(vars[v].i)
		{
			if(val) *vars[v].i=atoi(val);
			AddConsoleVar(vars[v].i,(char *)vars[v].name,1);
		}
		else
		{
			if(val) *vars[v].f=(float)atof(val);
			AddConsoleVar(vars[v].f,(char *)vars[v].name,1);
		}
	}
}

static int AcDead(AcPlayer *a)
{
	return a->deaduntil&&(int)(curtime-a->deaduntil)<0;
}

//a new life: joined, or respawned after a death
static void AcNewLife(int i)
{
	AcPlayer *a=&acplayer[i];
	int hold[ACWEAPONS];
	AcHolds(hold);
	a->damage=0;
	a->deathcounted=0;
	a->fire.started=0;
	AcAmmoFill(&a->ammo,hold);
	a->ammo.weapon=GetPlayer(i)->weapon;
	a->move.valid=0;
}

//a client gets slot i: it arrives dead (its first 11 -> 10 is a respawn)
static void AcJoin(int i)
{
	ZeroMemory(&acplayer[i],sizeof(AcPlayer));
	AcNewLife(i);
	acplayer[i].deathcounted=1;
}

//client i reports its own death (KILLED, KILLEDPOSITION): 0 if it is ignored
static int AcDeath(int i)
{
	if(!ac_enable) return 1;
	AcPlayer *a=&acplayer[i];
	if(AcDead(a))
	{
		AcLog(AC_DEATH,"%.31s (%d) reported a death the server already counted",GetPlayer(i)->name,i);
		return 0;
	}
	if(ac_killinterval>0&&a->lastdeath&&(int)(curtime-a->lastdeath)<ac_killinterval)
	{
		AcLog(AC_DEATH,"%.31s (%d) reported deaths too often, ignored",GetPlayer(i)->name,i);
		return 0;
	}
	a->lastdeath=curtime?curtime:1;
	a->damage=0;
	a->deathcounted=1;
	return 1;
}

//the server kills player v itself: its client never reported the death
static void AcServerKill(int v,int killer)
{
	AcPlayer *a=&acplayer[v];
	if(!ValidPlayer(killer)||killer>=numplayers) killer=v;
	AcLog(AC_KILL,"%.31s (%d) took %.0f damage without dying: killed by the server",GetPlayer(v)->name,v,a->damage);
	ReceiveKillMessage(v,killer);
	SimpleBuf killed=MakeSimple(KILLED,v,killer);
	for(int i=1;i<numplayers;i++)
		clientsock[i].SendReliable(killed.b,sizeof(killed.b),0);   //the victim too: it dies
	GetPlayer(v)->playstate=11;
	a->deaduntil=curtime+ac_deadtime;
	if(!a->deaduntil) a->deaduntil=1;
	a->lastdeath=curtime?curtime:1;
	a->deathcounted=1;
	a->damage=0;
}

//can the server count damage on remote player v: alive, or shooting
static int AcCanBeHit(int v)
{
	AcPlayer *a=&acplayer[v];
	if(AcDead(a)) return 0;
	return GetPlayer(v)->playstate==10||(a->lastfire&&(int)(curtime-a->lastfire)<1500);
}

static void AcDamage(int v,int attacker,float damage)
{
	if(damage>ac_hitcap) damage=(float)ac_hitcap;
	AcPlayer *a=&acplayer[v];
	a->damage+=damage;
	if(a->damage>=ac_killdamage) AcServerKill(v,attacker);
}

static int AcCountsDamage()
{
	return server&&ac_enable&&ac_killdamage>0;
}

//called by TickObjects for every step of a bullet: the hits the server sees on
//remote players (each client still decides its own, playing.cpp)
void AcBulletTick(Object *o)
{
	if(!AcCountsDamage()||(o->type!=1&&o->type!=3)) return;
	int shooter=o->pnum;
	if(!ValidPlayer(shooter)||shooter>=numplayers) return;
	for(int v=1;v<numplayers;v++)
	{
		if(v==shooter||v==GetLocalNum()||(o->achit&(1u<<v))) continue;
		Player *pl=GetPlayer(v);
		if(GetPlayer(shooter)->team==pl->team&&pl->team!=0) continue;
		if(!AcCanBeHit(v)) continue;
		if(!AcBulletHits(o->pos.x,o->pos.y,o->pos.z,pl->pos.x,pl->pos.y,pl->pos.z,LANDGRIDSIZE)) continue;
		o->achit|=1u<<v;
		//the mean of the victim's roll, damage*(1+RND)
		float damage=(o->type==1?sv.highbulletdamage:sv.lowbulletdamage)*1.5f*Magnitude(o->vel);
		AcDamage(v,shooter,damage);
	}
}

//called by TickObjects when a grenade explodes
void AcGrenade(Object *o)
{
	if(!AcCountsDamage()) return;
	for(int v=1;v<numplayers;v++)
	{
		if(v==GetLocalNum()||!AcCanBeHit(v)) continue;
		Vector p=GetPlayer(v)->pos+Vector(0,-1,0);
		float mag=AcDistance(o->pos.x,o->pos.y,o->pos.z,p.x,p.y,p.z,LANDGRIDSIZE);
		if(mag<8) AcDamage(v,o->pnum,AcGrenadeDamage(mag,sv.grenadedamage));
	}
}

static int AcFinite(float f)
{
	return f==f&&f<1e30f&&f>-1e30f;
}

//a shot (or reload) of client i: 0 if it is refused. Normalizes its direction.
static int AcFire(int i,FireGunDirection *f)
{
	if(!ac_enable) return 1;
	AcPlayer *a=&acplayer[i];
	if(!AcFinite(f->dir.x)||!AcFinite(f->dir.y)||!AcFinite(f->dir.z)||
		!AcFinite(f->pos.x)||!AcFinite(f->pos.y)||!AcFinite(f->pos.z)) return 0;
	float len=Magnitude(f->dir);
	if(len<.0001f) return 0;
	f->dir=f->dir/len;   //the bullet's speed, so its damage, scales with it
	if(AcDead(a))
	{
		AcLog(AC_FIRE,"%.31s (%d) fired while dead, refused",GetPlayer(i)->name,i);
		return 0;
	}
	int w=GetPlayer(i)->weapon;
	int hold[ACWEAPONS];
	AcHolds(hold);
	float cost=AcTimeout(w)*ac_firetolerance;
	if(f->reload)
	{
		AcAmmoReload(&a->ammo,w,hold);
		if(ac_firetolerance>0) AcFireReload(&a->fire,curtime,cost,(float)ac_fireburst,AcReloadTime(w)*ac_firetolerance);
		return 1;
	}
	if(ac_firetolerance>0&&!AcFireAllowed(&a->fire,curtime,cost,(float)ac_fireburst))
	{
		AcLog(AC_FIRE,"%.31s (%d) fired weapon %d faster than it can, refused",GetPlayer(i)->name,i,w);
		return 0;
	}
	if(ac_ammoslack>=0&&!AcAmmoFire(&a->ammo,w,ac_ammoslack))
	{
		AcLog(AC_AMMO,"%.31s (%d) fired weapon %d with an empty magazine, refused",GetPlayer(i)->name,i,w);
		return 0;
	}
	a->lastfire=curtime?curtime:1;
	Vector p=GetPlayer(i)->pos;
	float dist=AcDistance(f->pos.x,f->pos.y,f->pos.z,p.x,p.y,p.z,LANDGRIDSIZE);
	if(ac_firedist>0&&dist>ac_firedist)
		AcLog(AC_ORIGIN,"%.31s (%d) fired %.0f units away from where it is",GetPlayer(i)->name,i,dist);
	return 1;
}

//a position of client i with state (10 alive, 11 dead) and weapon: returns the
//state the server keeps
static int AcPosition(int i,int state,Vector pos,int weapon)
{
	if(!ac_enable) return state;
	AcPlayer *a=&acplayer[i];
	if(AcDead(a)) state=11;   //killed by the server: dead whatever it says
	int prev=GetPlayer(i)->playstate;
	if(prev==11&&state==10)
	{
		if(a->deathcounted) AcNewLife(i);   //respawn
		else AcLog(AC_STATE,"%.31s (%d) came back to life without dying",GetPlayer(i)->name,i);
	}
	int hold[ACWEAPONS];
	AcHolds(hold);
	AcAmmoPosition(&a->ammo,weapon,hold);
	if(state==10&&ac_maxspeed>0)
	{
		float speed=AcMoveSpeed(&a->move,pos.x,pos.y,pos.z,curtime,250,LANDGRIDSIZE);
		if(speed>ac_maxspeed)
			AcLog(AC_MOVE,"%.31s (%d) moved at %.0f units/s",GetPlayer(i)->name,i,speed);
	}
	else a->move.valid=0;
	return state;
}

//the blocks client pnum sends: all near it (1), else none
static int AcBlocksNear(const char *buf,int numblocks,int pnum)
{
	if(!ac_enable||ac_builddist<=0) return 1;
	Vector p=GetPlayer(pnum)->pos;
	for(int i=0;i<numblocks;i++)
	{
		BlockEdit b;
		ReadBlockEdit(buf,i,&b);
		if(!AcBuildNear(b.x,b.y,b.z,p.x,p.y,p.z,ac_builddist,BLOCKSIZE,BLOCKGRIDSIZE))
		{
			AcLog(AC_BUILD,"%.31s (%d) %s a block far from itself (%d %d %d), refused",
				GetPlayer(pnum)->name,pnum,b.del?"removed":"placed",b.x,b.y,b.z);
			return 0;
		}
	}
	return 1;
}

//rate limits per source IP on the info port, kind 0: any message, 1: REQUESTINFO,
//2: REQUESTENTER. 0: ignore it.
static int AcIpAllowed(SOCKADDR_IN source,int kind)
{
	if(!ac_enable) return 1;
	AcIpEntry *e=AcIpFind(aciptable,ACIPTABLE,source.sin_addr.s_addr,curtime);
	int ok=1;
	if(kind==0&&ac_ippackets>0) ok=AcTake(&e->packets,curtime,ac_ippackets/1000.f,ac_ippackets*2.f);
	if(kind==1&&ac_ipinfo>0) ok=AcTake(&e->info,curtime,ac_ipinfo/1000.f,ac_ipinfo*4.f);
	if(kind==2&&ac_ipenter>0) ok=AcTake(&e->enter,curtime,ac_ipenter/60000.f,ac_ipenter/4.f+1);
	if(!ok) AcLog(AC_IP,"too many %s from %s, ignored",kind==0?"messages":kind==1?"info requests":"join requests",
		inet_ntoa(source.sin_addr));
	return ok;
}

//---- join cookie (M11 phase 2, docs/M11.md) ----
//A UDP source address can be spoofed: before a slot, a port and a stream of data
//go to the address of a REQUESTENTER, the client must send back the cookie of that
//address, HMAC-SHA256(secret, IP, port, time bucket) (hmac.h). Stateless: nothing is
//kept for a request without a valid cookie, and its answer (ENTERCOOKIE, 12 bytes)
//is never bigger than the request (version trailer alone: 11 bytes, plus caps).
static unsigned char joinsecret[JOINSECRET_SIZE];
static int joinsecretset;

static unsigned int JoinBucket()
{
	return curtime/(DWORD)(ac_cookietime>=500?ac_cookietime:500);
}

static void SendSimpleTo(SOCKADDR_IN dest,char type,DWORD msg,DWORD msg2)
{
	SimpleBuf s=MakeSimple(type,msg,msg2);
	infosock.dest=dest;
	infosock.Send(s.b,sizeof(s.b));
	infosock.Flush();
}

//REQUESTENTER (msglen bytes, caps) from source: 1 if its address is proven and it
//may get a slot; else it was answered (a cookie, or a refusal) and 0
static int JoinAddressProven(SOCKADDR_IN source,const HelloCaps *caps,int msglen)
{
	if(!ac_enable) return 1;
	//GameNetworkingSockets: an encrypted connection, its handshake proved the address
	if(NetSourceIsConnection(&source)) return 1;
	if(!(caps->caps&HELLOCAP_COOKIE))
	{
		if(!ac_requirecookie) return 1;   //a build before cookies: as before phase 2
		//the SIMPLE alone (12 bytes, the request has at least 14): the client says
		//"update the game"; no text, which would be bigger than the request
		ConsoleDisplayText("refused, client without join cookie (ac_requirecookie 1)");
		if(msglen>=SIMPLESIZE) SendSimpleTo(source,VERSIONMISMATCH,EDIFICE_PROTOCOL_VERSION,0);
		return 0;
	}
	if(!joinsecretset)
	{
		JoinSecretRandom(joinsecret);
		joinsecretset=1;
	}
	unsigned int bucket=JoinBucket();
	if(caps->caps&HELLOCAP_HASCOOKIE)
	{
		if(JoinCookieValid(joinsecret,source.sin_addr.s_addr,source.sin_port,bucket,caps->cookie)) return 1;
		ConsoleDisplayText("join cookie expired or wrong, sending a new one");
	}
	if(msglen<SIMPLESIZE) return 0;
	unsigned char c[JOINCOOKIE_SIZE];
	JoinCookie(joinsecret,source.sin_addr.s_addr,source.sin_port,bucket,c);
	SendSimpleTo(source,ENTERCOOKIE,WireGetU32(c),WireGetU32(c+4));
	return 0;
}

void HandleSimpleServer(char type,DWORD msg,DWORD msg2,SOCKADDR_IN source,int pnum);
void ReceivePosition(const char *msg,int msglen,int sender);
static void RelayVoice(VoiceMessage *voice);
#ifndef HK_WIN32_NATIVE
static void JoinNetAddGame(const char *msg,int msglen,SOCKADDR_IN source);
#endif
void DeletePlayer(int num,int reason);
void ReceiveChatMessage(char *msg,int msglen);


//reads a configuration file

void LoadNetSettings()
{
	CfgFile netsetfile;
	netsetfile.Load(UserFile("net.ini"));
	netsettings.infosendport=htons(netsetfile.GetValuei("infosendport"));
	netsettings.datasendport=htons(netsetfile.GetValuei("datasendport"));
	char *servername;
	servername=netsetfile.GetValues("servername");
	if(servername) strcpy(netsettings.servername,servername);
	char *playername;
	playername=netsetfile.GetValues("name");
	if(playername) strcpy(netsettings.playername,playername);
#ifdef HKDS
	strcpy(netsettings.playername,netsettings.servername);
#endif
}


//initializes windows sockets

void InitSocks()
{
	LoadNetSettings();
	WORD version=MAKEWORD(2,2);
	WSADATA vdata;
	WSAStartup(version,&vdata);
	static int acvars=0;
	if(!acvars) AcInitVars();   //anti-cheat server vars (M11), after InitConsole
	acvars=1;
}

//creates sockets and initializes some things

void StartServer()
{
	if(server) return;
	Disconnect();
	server=1;
	joinsecretset=0;   //a new join cookie secret for each game hosted (M11 phase 2)
	LoadNetSettings();
	numplayers=1;
	
	//an Internet game also listens for P2P connections, once the lobby gave it an
	//identity (lobbygame.cpp)
	infosock.Open(NetOpenInfoLink(netsettings.infosendport,1,LobbyHostInternet()));

	SetLocalNum(0);
	GetLocalPlayer()->playstate=10;
#ifdef HKDS
	GetLocalPlayer()->playstate=1;
#endif

	ConsoleDisplayText("Started server");
	RedrawSplash(800,"Loading Textures");
	LoadTextures();
	if(!StartServerMap())   //a saved map chosen by loadmap or Server Setup (hugekilla.cpp)
	{
		RedrawSplash(800,"Loading Sky");
		LoadSky();
		RedrawSplash(800,"Creating Land");
		RandomLand(ldfs);
		CreateLand(ldfs);
	}

	consolevisible=0;
	strcpy(GetLocalPlayer()->name,netsettings.playername);
	curport=netsettings.datasendport;
	LockCursor(1);
#ifndef HKDS
	GPlayerEnterGame(GetLocalNum());
#endif
	LobbyHostStart();   //announced on the Internet list if net.ini says so
}

//closes sockets

//the other side leaves at once instead of waiting for its timeout: the server's
//clients go back to the menu, or the server drops us. A few copies, unreliable: the
//sockets close right after (older versions ignore EXIT)
static void SendExit()
{
	SimpleBuf exit=MakeSimple(EXIT,0,0);
	for(int k=0;k<3;k++)
	{
		if(server)
			for(int i=1;i<numplayers;i++) clientsock[i].Send(exit.b,sizeof(exit.b));
		else if(connected)
			datasock.Send(exit.b,sizeof(exit.b));
	}
	if(server)
		for(int i=1;i<numplayers;i++) clientsock[i].Flush();
	else if(connected)
		datasock.Flush();
}

void Disconnect()
{
	SendExit();
	LockCursor(0);
	LobbyHostStop();
	infosock.Close();
	datasock.Close();
	for(int i=1;i<numplayers;i++)
		clientsock[i].Close();
	server=0;
	connected=0;
	numplayers=0;
	VoiceReset();
}

void CloseSocks()
{
	Disconnect();
	LobbyQuit();
	NetLinkShutdown();
	WSACleanup();
}

//sent to show a change in a player's state (like playing, dead, etc.)

void SendState(int srcnum,int destnum)
{
	SimpleBuf state=MakeSimple(STATECHANGE,GetPlayer(srcnum)->playstate,0);
	if(server)
	{
		clientsock[destnum].SendReliable(state.b,sizeof(state.b),0);
	}
	else
	{
		datasock.SendReliable(state.b,sizeof(state.b),0);
	}
}

//the server calls this when it receives a simplemessage

void HandleSimpleServer(char type,DWORD msg,DWORD msg2,SOCKADDR_IN source,int pnum)
{
	switch(type)
	{
		case EXIT:
			{
				//deleted by the timeout check of ServerLoop, after its receive loop
				if(pnum>0) GetPlayer(pnum)->timeout=curtime-1;
				break;
			}
		case REQUESTINFO:
		{
			SendInfo(source);
			ConsoleDisplayText("received info request");
			break;
		}
		case PORTACK:
			{

				char names[2+MAXPLAYERS*PLAYERNAMESIZE];
				ByteWriter w(names,sizeof(names));
				w.U8(PLAYERNAMES);
				w.U8(numplayers);
				for(int i=0;i<numplayers;i++)
					WritePlayerName(w,i);
				clientsock[pnum].SendReliable(names,w.Size(),RECEIVEDPLAYERNAMES);
				
				ConsoleDisplayText("received port acknowledgement, sending player names");
				break;
			}
		case RECEIVEDPLAYERNAMES:
			{
				SendLandInfo(pnum);
				ConsoleDisplayText("received player names acknowledgement, sending land info");
				break;
			}
		case RECEIVEDLANDINFO:
			{
				StartBlockBuffer();

				for(int x=0;x<BLOCKGRIDSIZE;x++)
				for(int y=0;y<BLOCKGRIDHEIGHT;y++)
				for(int z=0;z<BLOCKGRIDSIZE;z++)
				{
					Block *cur=GetBlock(x,y,z);
					if(cur->type)
					{
						AddBlockBuffer(x,y,z,cur->type,0,cur->top,cur->side,cur->bottom,0);
						if(blockbufferpos+20>BIGBUFSIZE)
						{
							WirePutU32(&blockbuffer[1],numblocks);
							clientsock[pnum].SendReliable(blockbuffer,blockbufferpos,0);
							StartBlockBuffer();
						}
					}
				}
				WirePutU32(&blockbuffer[1],numblocks);
				clientsock[pnum].SendReliable(blockbuffer,blockbufferpos,RECEIVEDBLOCKINFO);
				StartBlockBuffer();
				ConsoleDisplayText("received landinfo acknowledgement, sending blockinfo");
				static ServerObjectSnapshot snapshot;
				static char sobjectbuf[SNAPSHOTSIZE];
				snapshot.type=SERVEROBJECTSNAPSHOT;
				CopyMemory(snapshot.obj,sobject,sizeof(snapshot.obj));
				int snapsize=WriteSnapshot(sobjectbuf,sizeof(sobjectbuf),&snapshot);
				clientsock[pnum].SendReliable(sobjectbuf,snapsize,0);
				SendServerVars(pnum); 
				SendRGD(pnum);
				
				
				
				break;
			}
		case RECEIVEDBLOCKINFO:
			{
				SimpleBuf joingame=MakeSimple(JOINGAME,pnum,0);
				int i;
				for(i=1;i<numplayers;i++)
				{
					clientsock[i].SendReliable(joingame.b,sizeof(joingame.b),0);
				}
				for(i=0;i<numplayers;i++)
				if(GetPlayer(i)->playstate==10)
				{
					joingame=MakeSimple(JOINGAME,i,0);
					clientsock[pnum].SendReliable(joingame.b,sizeof(joingame.b),0);
				}
				GPlayerEnterGame(pnum);
				ResetPlayer(pnum);
				GetPlayer(pnum)->playstate=10;
				AcJoin(pnum);
				
				ConsoleDisplayText("received blockinfo acknowledgement, sending objects and join game");
				break;
			}
		
		case PINGREPLY:
			{
				if(GetPlayer(pnum)->pingid==msg)
				{
					GetPlayer(pnum)->ping=(GetPlayer(pnum)->ping*3+(curtime-GetPlayer(pnum)->lastpingtime))/4;
					GetPlayer(pnum)->pingwasrecv=1;
					GetPlayer(pnum)->packetloss=(1-(float)msg2/clientsock[pnum].packetssent)*30000;
					for(int i=1;i<numplayers;i++)
					{
						SendSimple(i,PINGINFO,pnum,MAKEWPARAM(GetPlayer(pnum)->ping,GetPlayer(pnum)->packetloss));
					}
					SendSimple(pnum,PINGINFO,0,MAKEWPARAM(GetPlayer(0)->ping,GetPlayer(0)->packetloss));
				}
				break;
			}
		case PAIN:
			{
				for(int i=1;i<numplayers;i++)
				{
					if(i!=pnum) SendSimple(i,PAIN,pnum,msg2);
				}
				SetSoundPosition(msg2+1,GetPlayer(pnum)->pos);
				if(msg2<500) PlaySound(msg2+1,0);
				else PlaySound(msg2,0);
				
			}
			break;
		case KILLED:
			{
				if(!ValidPlayer(msg2)) break;
				if(pnum>0&&!AcDeath(pnum)) break;   //rate limited (M11)
				ReceiveKillMessage(pnum,msg2);
				SimpleBuf killed=MakeSimple(KILLED,pnum,msg2);
				if(server)
				{
					for(int i=1;i<numplayers;i++)
						if(i!=pnum)clientsock[i].SendReliable(killed.b,sizeof(killed.b),0);
				}
				break;
			}
				
		case CHANGETEAM:
			{
				ChangeTeam(pnum,msg2);
				break;
			}
		case CLAIMOBJECT:
			{
				ClaimObject(pnum);
				break;
			}
	}
}

//called every frame on the server side

void ServerLoop()
{
	GTick();
	LobbyHostLoop(numplayers);
	static Message msg;
	SOCKADDR_IN source;
	infosock.CheckReliableResend();
		
	int msglen;
	while((msglen=infosock.Receive(&msg,&source)))
	{
		if(!AcIpAllowed(source,0)) continue;   //per-IP rate limits (M11)
		switch (msg.type)
		{
		case REQUESTENTER:
			{
				if(!AcIpAllowed(source,2)) break;
				char reqname[32];
				int trailer=ReadHelloName((char *)&msg,msglen,reqname,sizeof(reqname));
				PeerVersion peer;
				ReadVersionTrailer((char *)&msg+trailer,msglen-trailer,&peer);
				int addplayer=numplayers;

				char message[1000];
				strcpy(message,reqname);
				strcat(message," at ");
				strcat(message,inet_ntoa(source.sin_addr));
				strcat(message," requests to connect");
				ConsoleDisplayText(message);
				
				char reason[200];
				if(!VersionCompatible(&peer,reason,sizeof(reason)))
				{
					strcpy(message,"refused, ");
					strcat(message,reason);
					ConsoleDisplayText(message);
					sprintf(message,"This server runs Edifice %s (protocol %d): update your game to join.",
						EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
					RefuseClient(source,VERSIONMISMATCH,message);
					break;
				}
				//a retried request (its ENTER got lost) gets the same slot again
				int known;
				for(known=1;known<numplayers;known++)
					if(GetPlayer(known)->playstate==1&&
						clientsock[known].requestaddr.sin_addr.s_addr==source.sin_addr.s_addr&&
						clientsock[known].requestaddr.sin_port==source.sin_port)
						break;
				if(known<numplayers)
				{
					SimpleBuf enter=MakeSimple(ENTER,htons(clientsock[known].localport),known);
					infosock.dest=source;
					infosock.Send(enter.b,sizeof(enter.b));
					infosock.Flush();
					break;
				}
				//no slot, nothing sent but a small answer, before the address is proven
				HelloCaps caps;
				ReadHelloCaps((char *)&msg+trailer,msglen-trailer,&caps);
				if(!JoinAddressProven(source,&caps,msglen)) break;
				if(numplayers>=MAXPLAYERS)
				{
					ConsoleDisplayText("server was full");
					RefuseClient(source,SERVERFULL,"The server is full.");
					break;
				}
				
				//each client gets its own server port: the first free one after datasendport.
				//A GameNetworkingSockets client has its connection instead (netlink.h): its
				//port is only a number then, nothing is bound.
				int baseport=ntohs(netsettings.datasendport);
				DatagramLink *peerlink=NetOpenPeerLink(&source);
				UdpLink *newlink=peerlink?0:new UdpLink;
				int port=0;
				for(int p=baseport+1;p<=baseport+2*MAXPLAYERS&&!port;p++)
				{
					int used=0;
					for(int c=1;c<numplayers;c++)
						if(clientsock[c].link&&clientsock[c].localport==p) used=1;
					if(used) continue;
					if(peerlink||newlink->Bind(htons(p))) port=p;
				}
				if(!port)
				{
					ConsoleDisplayText("no free port for a new client");
					RefuseClient(source,SERVERFULL,"The server has no free port.");
					delete peerlink;   //after the refusal, which goes through its connection
					delete newlink;
					break;
				}

				strcpy(GetPlayer(addplayer)->name,reqname);
				clientsock[addplayer].Open(peerlink?peerlink:newlink);
				clientsock[addplayer].dest=source;
				clientsock[addplayer].dest.sin_port=htons(port);   //the client binds the same port
				clientsock[addplayer].localport=port;
				clientsock[addplayer].requestaddr=source;
				clientsock[addplayer].filterip=1;
				clientsock[addplayer].ResetReliable();
				SimpleBuf enter=MakeSimple(ENTER,htons(port),addplayer);
				infosock.dest=source;
				infosock.Send(enter.b,sizeof(enter.b));
				infosock.Flush();
				/*SimpleBuf tmpmsg=MakeSimple(PORTOPENER,0,0);
				for(int m=0;m<30;m++)
				{
					clientsock[addplayer].Send(tmpmsg.b,sizeof(tmpmsg.b));
					clientsock[addplayer].Flush();
				}*/
				strcpy(message,GetPlayer(addplayer)->name);
				strcat(message," has connected");
				ConsoleDisplayText(message);
				GetPlayer(addplayer)->playstate=1;
				AcJoin(addplayer);
				GetPlayer(addplayer)->kills=0;
				GetPlayer(addplayer)->deaths=0;
				char names[2+PLAYERNAMESIZE];
				ByteWriter w(names,sizeof(names));
				w.U8(PLAYERNAMES);
				w.U8(1);
				WritePlayerName(w,addplayer);

				for(int i=1;i<numplayers;i++)
				{
					clientsock[i].SendReliable(names,w.Size(),0);
				}
				GetPlayer(addplayer)->timeout=curtime+PEERTIMEOUT;
				GetPlayer(addplayer)->gunup=Vector(0,1,0);
				clientsock[addplayer].packetssent=0;
				clientsock[addplayer].packetsreceived=0;
				numplayers++;
				
				break;
			}	
			
		case SIMPLE:
			{
				SimpleMessage simple;
				if(!ReadSimple(&msg,msglen,&simple)) break;
				//anyone can reach the info port: only info requests are answered there
				if(simple.type2==REQUESTINFO&&AcIpAllowed(source,1))
					HandleSimpleServer(simple.type2,simple.msg,simple.msg2,source,0);
				break;
			}
		}
	}
	
	int i;
	for(i=1;i<numplayers;i++)
	{
		clientsock[i].CheckReliableResend();
		int clen;
		while((clen=clientsock[i].Receive(&msg,&source)))
		{
			GetPlayer(i)->timeout=curtime+PEERTIMEOUT;
			switch(msg.type)
			{
				case PLAYERPOSITION:
					{
						ReceivePosition((char *)&msg,clen,i);   //a client only moves itself
						break;
					}
				case VOICE:
					{
						VoiceMessage voice;
						if(!ReadVoice(&msg,clen,&voice)) break;
						voice.player=i;   //a client only speaks for itself
						if(GetPlayer(i)->playstate<10)
						{
							if(getenv("HK_VOICE_LOG")) fprintf(stderr,"voice from %d dropped: state %d\n",i,GetPlayer(i)->playstate);
							break;
						}
						RelayVoice(&voice);
						break;
					}
				case SIMPLE:
				{
					SimpleMessage simple;
					if(!ReadSimple(&msg,clen,&simple)) break;
					int ackcallback=simple.type2==0||simple.type2==RECEIVEDLANDINFO||
						simple.type2==RECEIVEDPLAYERNAMES||simple.type2==RECEIVEDJOINGAME||
						simple.type2==RECEIVEDBLOCKINFO;
					//"received" steps come from our own reliable acks, never from the client
					if(ackcallback&&!clientsock[i].fromack) break;
					HandleSimpleServer(simple.type2,simple.msg,simple.msg2,source,i);
					break;
				}
				case BLOCKS:
					{
						ReceiveBlockBuffer((char *)&msg,i,clen);
						
						break;
					}
				case CHATMESSAGE:
					{
						if(clen>=3) msg.buf[1]=i;   //sender: the client, whatever it wrote
						ReceiveChatMessage((char *)&msg,clen);
						break;
					}
				case FIREGUN:
				{
					FireGunDirection firegun;
					if(!ReadFireGun(&msg,clen,&firegun)) break;
					if(!AcFire(i,&firegun)) break;   //rate, ammo, dead (M11)
					TryFireGun(i,firegun.pos,firegun.dir,firegun.reload);
					break;
				}
				case KILLEDPOSITION:
				{
					KilledPosition kp;
					if(!ReadKilledPosition(&msg,clen,&kp)) break;
					if(!ValidPlayer(kp.killer)) break;
					if(!AcDeath(i)) break;   //rate limited (M11)
					kp.killed=i;
					GetPlayer(i)->pos=kp.pos;
					ReceiveKillMessage(i,kp.killer);
					if(server)
					{
						char kpbuf[KILLEDPOSITIONSIZE];
						int kpsize=WriteKilledPosition(kpbuf,sizeof(kpbuf),&kp);
						for(int i3=1;i3<numplayers;i3++)
							if(i3!=i)clientsock[i3].SendReliable(kpbuf,kpsize,0);
					}
					break;
				}
			}
		};
		if(curtime>GetPlayer(i)->timeout||clientsock[i].reliablefailed)
		{
			DeletePlayer(i,0);
		}
		if((GetPlayer(i)->pingwasrecv&&(curtime>GetPlayer(i)->lastpingtime+1000))||
			curtime>GetPlayer(i)->lastpingtime+500)
		{
			GetPlayer(i)->pingid++;
			GetPlayer(i)->pingwasrecv=0;
			GetPlayer(i)->lastpingtime=curtime;
			SendSimple(i,PING,GetPlayer(i)->pingid,curtime);
			clientsock[i].Flush();
		}
	}
	GetPlayer(0)->ping=curtime-GetPlayer(0)->lastpingtime;
	GetPlayer(0)->lastpingtime=curtime;
	infosock.Flush();
	
	if(curtime>nextresend)
	{
		char localnum=GetLocalNum();
		for(i=0;i<numplayers;i++)
		for(int i2=1;i2<numplayers;i2++)
		{
			if(localnum!=i2&&i!=i2&&GetPlayer(i2)->playstate>=10&&GetPlayer(i)->playstate>=10)
				SendPosition(i,i2);
		}
		nextresend=curtime+sv.positionresendtime;
	}
	for(i=0;i<numplayers;i++)
		{
			clientsock[i].Flush();
		}
	
}

//quick way to send a simplemessage

void SendSimple(char num,char type,DWORD msg,DWORD msg2)
{
	SimpleBuf simple=MakeSimple(type,msg,msg2);
	if(server)
		clientsock[num].Send(simple.b,sizeof(simple.b));
	else
		datasock.Send(simple.b,sizeof(simple.b));
}

//the server left or stopped answering: back to the join screen, saying why
static void ServerGone(const char *why)
{
	char text[100];
	snprintf(text,sizeof(text),"%s",why);
	ConsoleDisplayText(text);
	Disconnect();
	GetLocalPlayer()->playstate=0;
#ifndef HK_WIN32_NATIVE
	JoinScreenShowError(why);
#endif
}

//client calls this when it receives a simplemessage

void HandleSimpleClient(char type,DWORD msg,DWORD msg2,SOCKADDR_IN source)
{
	switch(type)
	{
		case VERSIONMISMATCH:
		case SERVERFULL:
			{
				//only while waiting for an answer to REQUESTENTER
				if(GetLocalPlayer()->playstate!=1||connected) break;
				char text[200];
				if(type==SERVERFULL)
					strcpy(text,"Can't join:\rthe server is full");
				else
					snprintf(text,sizeof(text),"Can't join: server is\rprotocol %d, you %d.\rUpdate the game.",(int)msg,EDIFICE_PROTOCOL_VERSION);
				ConsoleDisplayText(text);
				Disconnect();
				GetLocalPlayer()->playstate=0;
#ifndef HK_WIN32_NATIVE
				JoinScreenShowError(text);
#endif
				break;
			}
		case ENTERCOOKIE:
			{
				//the server wants its join cookie back (M11 phase 2): asks again with it,
				//at once if it is a new one (else the retry timer sends it)
				if(GetLocalPlayer()->playstate!=1||connected) break;
				unsigned char c[HELLOCOOKIE_SIZE];
				WirePutU32(c,msg);
				WirePutU32(c+4,msg2);
				int fresh=!haveentercookie||entercookieip!=source.sin_addr.s_addr||memcmp(c,entercookie,sizeof(c));
				memcpy(entercookie,c,sizeof(c));
				entercookieip=source.sin_addr.s_addr;
				haveentercookie=1;
				if(fresh)
				{
					ConsoleDisplayText("received join cookie");
					SendRequestEnter();
				}
				GetPlayer(0)->timeout=curtime+2000;
				break;
			}
		case ENTER:
			{
				if(GetLocalPlayer()->playstate==1&&connected==0&&msg2<MAXPLAYERS)
				{
					connected=1;
					SetLocalNum(msg2);
					GetLocalPlayer()->playstate=1;
					GetLocalPlayer()->timeout=curtime+PEERTIMEOUT;
					numplayers=msg2+1;
					datasock.Open(NetOpenDataLink(msg,&source));
					datasock.dest=source;
					datasock.dest.sin_port=msg;
					datasock.filterip=1;   //from now on, only the server's IP
					SimpleBuf portack=MakeSimple(PORTACK,msg,0);
					datasock.SendReliable(portack.b,sizeof(portack.b),0);
					infosock.Close();
					ConsoleDisplayText("connection accepted");
					datasock.packetssent=0;
					datasock.packetsreceived=0;
					LockCursor(1);
				}
				break;
			}
		case EXIT:
			{
				if(!connected) break;
				ServerGone("The server closed the game");
				break;
			}
		case JOINGAME:
			{
				if(!ValidPlayer(msg)) break;
				GetPlayer(msg)->playstate=10;
				consolevisible=0;
				if(msg==GetLocalNum()) ConsoleDisplayText("entering game");
				ResetPlayer(msg);
				break;
			}
		case DELETEPLAYER:
			{
				if(!ValidPlayer(msg)) break;
				DeletePlayer(msg,msg2);
				break;
			}
		case PING:
			{
				SendSimple(-1,PINGREPLY,msg,datasock.packetsreceived);
				break;
			}
		case PINGINFO:
			{
				if(!ValidPlayer(msg)) break;
				GetPlayer(msg)->ping=LOWORD(msg2);
				GetPlayer(msg)->packetloss=HIWORD(msg2);
				break;
			}
		case PAIN:
			{
				if(!ValidPlayer(msg)) break;
				SetSoundPosition(msg2+1,GetPlayer(msg)->pos);
				if(msg2<500) PlaySound(msg2+1,0);
				else PlaySound(msg2,0);
				break;
			}
		case KILLED:
			{
				if(!ValidPlayer(msg)||!ValidPlayer(msg2)) break;
				if((int)msg==GetLocalNum())
				{
					//the server killed us (M11): die here too, unless our own death (already
					//counted) crossed it
					if(GetLocalPlayer()->life<=0||GetLocalPlayer()->playstate!=10) break;
					GetLocalPlayer()->life=0;
				}
				ReceiveKillMessage(msg,msg2);
				break;
			}
		case CHANGETEAM:
			{
				if(!ValidPlayer(msg)||msg2>2) break;
				ReceiveChangeTeam(msg,msg2);
				break;
			}
		case SCORERESET:
			{
				ScoreReset();
				break;
			}
		case CREDITS:
			{
				int credit=msg;
				int number=msg2;
				if(credit>=0&&credit<(int)sizeof(GetLocalPlayer()->credits)) GetLocalPlayer()->credits[credit]=number;
				else if(credit<0)
					ZeroMemory(&GetLocalPlayer()->credits,sizeof(GetLocalPlayer()->credits));
				break;
			}
	}
}

//called every frame on the client side

void ClientLoop()
{
	static Message msg;
	SOCKADDR_IN source;
	datasock.CheckReliableResend();
	infosock.CheckReliableResend();
		
	while(1)
	{	
		int frominfo=0;
		int msglen=datasock.Receive(&msg,&source);
		if(!msglen)
		{
			msglen=infosock.Receive(&msg,&source);
			frominfo=1;
		}
		if(!msglen) break;
		//before joining, game messages only come from the server we asked; anyone may
		//answer a LAN search (INFO)
		if(frominfo&&msg.type!=INFO)
		{
			if(source.sin_addr.s_addr!=infosock.dest.sin_addr.s_addr) continue;
			if(msg.type!=SIMPLE&&msg.type!=CHATMESSAGE) continue;
		}
		GetPlayer(0)->timeout=curtime+PEERTIMEOUT;
		//if(!(source.sin_addr.s_addr==infosock.dest.sin_addr.s_addr)) continue;
		switch(msg.type)
		{
			case INFO:
				{
#ifdef HK_WIN32_NATIVE
					if(numlocallistaddr==MAXLOCALLIST) break;
					int i;
					for(i=0;i<numlocallistaddr;i++)
					{
						if(locallistaddr[i].sin_addr.s_addr==source.sin_addr.s_addr&&
							locallistaddr[i].sin_port==source.sin_port) break;
					}
					if(i!=numlocallistaddr) break;
					char infoname[100];
					int trailer=ReadHelloName((char *)&msg,msglen,infoname,sizeof(infoname));
					PeerVersion peer;
					ReadVersionTrailer((char *)&msg+trailer,msglen-trailer,&peer);
					locallistaddr[numlocallistaddr]=source;
					numlocallistaddr++;
#ifdef HK_WIN32_NATIVE
					SendDlgItemMessage(dlghwnd,IDC_LOCALGAMES,LB_ADDSTRING,0,(long)infoname);
#else
					JoinScreenAddGame(infoname,&peer);
#endif
					ConsoleDisplayText("received info");
					break;
#else
					JoinNetAddGame((char *)&msg,msglen,source);
					break;
#endif
				}
			case LANDINFO:
				{
					
					static LandInfo landinfo;
					if(!ReadLandInfo(&msg,msglen,&landinfo)) break;
					//bound everything the land builder uses as a count or a file name
					SanitizeLandInfo(&landinfo.ldfs,&landinfo.skysets);
					ApplyLand(&landinfo.ldfs,&landinfo.skysets);
					for(int i=0;i<numplayers;i++)
					{
						GetPlayer(i)->playstate=1;
					}
					ConsoleDisplayText("received landinfo");
					break;
				}
			case SIMPLE:
				{
					SimpleMessage simple;
					if(!ReadSimple(&msg,msglen,&simple)) break;
					HandleSimpleClient(simple.type2,simple.msg,simple.msg2,source);
					break;
				}
			case PLAYERPOSITION:
				{
					if(connected)
					{
						ReceivePosition((char *)&msg,msglen,-1);
					}
					break;
				}
			case VOICE:
				{
					//the server only relays the voices of players near us
					VoiceMessage voice;
					if(!connected||!ReadVoice(&msg,msglen,&voice)) break;
					if(!ValidPlayer(voice.player)||voice.player==GetLocalNum()) break;
					if(GetPlayer(voice.player)->playstate<10) break;
					VoiceReceive(voice.player,voice.seq,voice.data,voice.len);
					break;
				}
			case SERVERVARS:
				{
					ServerVarsMessage vars;
					if(ReadServerVars(&msg,msglen,&vars)) sv=vars.v;
					break;
				}
			case PLAYERNAMES:
				{
					PlayerNameEntry entries[MAXPLAYERS];
					int numnames=ReadPlayerNames(&msg,msglen,entries,MAXPLAYERS);
					for(int i=0;i<numnames;i++)
					{
						int playernum=entries[i].num;
						if(!ValidPlayer(playernum)) break;
						GetPlayer(playernum)->kills=entries[i].kills;
						GetPlayer(playernum)->deaths=entries[i].deaths;
						GetPlayer(playernum)->team=entries[i].team;
						strcpy(GetPlayer(playernum)->name,entries[i].name);
						if(playernum>=numplayers) 
						{
							char message[1000];
							strcpy(message,GetPlayer(playernum)->name);
							strcat(message," has joined the game");
							ConsoleDisplayText(message);
							numplayers=playernum+1;
						}
					}
					ConsoleDisplayText("received player names");
					break;
				}
			case REMOTEGAMEDATA:
				{
					RemoteGameDataMessage data;
					if(ReadRGD(&msg,msglen,&data)) rgd=data.d;
					break;
				}
			case BLOCKS:
				{
					ReceiveBlockBuffer((char *)&msg,0,msglen);
					break;
				}
			case CHATMESSAGE:
				{
					//ConsoleDisplayText("chat message");
					ReceiveChatMessage((char *)&msg,msglen);
					break;		
				}
			case FIREGUN:
				{
					FireGunDirection firegun;
					if(ReadFireGun(&msg,msglen,&firegun)) HandleFireGun(&firegun);
					break;
				}
			case SERVEROBJECTSNAPSHOT:
				{
					static ServerObjectSnapshot snapshot;
					CopyMemory(snapshot.obj,sobject,sizeof(snapshot.obj));   //keeps the (unsent) timeouts
					if(ReadSnapshot(&msg,msglen,&snapshot))
						CopyMemory(sobject,snapshot.obj,sizeof(snapshot.obj));
					break;
				}
			case UPDATESERVEROBJECT:
				{
					UpdateServerObject update;
					if(ReadUpdateServerObject(&msg,msglen,&update)&&ValidSObj(update.num))
						ReceiveUpdateServerObject(&update);
					break;
				}
			case KILLEDPOSITION:
				{
					KilledPosition kp;
					if(!ReadKilledPosition(&msg,msglen,&kp)) break;
					if(!ValidPlayer(kp.killed)||!ValidPlayer(kp.killer)) break;
					GetPlayer(kp.killed)->pos=kp.pos;
					ReceiveKillMessage(kp.killed,kp.killer);
					break;
				}
			case 0:
				break;
			default:
				{
					ConsoleDisplayText("Unknown message received");
					break;
				}
		}
	}
	if(curtime>GetPlayer(0)->timeout&&GetLocalPlayer()->playstate==1)
	{
		if(connected==0)
		{
			SendRequestEnter();
			GetPlayer(0)->timeout=curtime+2000;
			ConsoleDisplayText("retrying");
		}
		else
		{
			ConsoleDisplayText("server timed out");
			//GetPlayer(0)->timeout=curtime+60000;
			Disconnect();
			GetLocalPlayer()->playstate=0;
		}
	}
	//in the game the server pings every second: SERVERSILENCE without a word means
	//it is gone (while joining, a map transfer may take longer: the full timeout)
	if(connected&&GetLocalPlayer()->playstate>=10&&(curtime+(PEERTIMEOUT-SERVERSILENCE)>GetPlayer(0)->timeout||
		datasock.reliablefailed))
		ServerGone("Lost the connection to the server");
	if(curtime>nextresend)
	{
		if(GetLocalPlayer()->playstate>=10)SendPosition(GetLocalNum(),-1);
		nextresend=curtime+sv.positionresendtime;
	}
	datasock.Flush();
	infosock.Flush();
	
}

//called every frame

void SockLoop()
{
#ifndef HK_WIN32_NATIVE
	VoiceUpdate();   //before the loops, which flush what it sends
#endif
	if(server) 
		ServerLoop();
	else
		ClientLoop();
}


//window message handler for the connect dialog

#ifdef HK_WIN32_NATIVE
LRESULT CALLBACK Multiplay(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	
	switch (message)
	{
		case WM_INITDIALOG:
			dlghwnd=hDlg;
			LoadNetSettings();
				return TRUE;
		case WM_DESTROY:
			dlghwnd=0;
			
				return TRUE;
		case WM_SHOWWINDOW:
			if(wParam)
			{
				infosock.Open(UdpLink::Open(netsettings.infosendport,1));
			}

				return 1;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				GetLocalPlayer()->playstate=0;
				EndDialog(hDlg, 0);
				return TRUE;
			}
			if(LOWORD(wParam)==IDC_PASTE)
			{
				OpenClipboard(hDlg);
				HANDLE clipboard=GetClipboardData(CF_OEMTEXT);
				CloseClipboard();
				SetDlgItemText(hDlg,IDC_IP,(char *)clipboard);

				return 1;
			}
			if(LOWORD(wParam)==IDC_REFRESH)
			{
				SOCKADDR_IN dest;
				infosock.dest=MakeSockAddr(255,255,255,255,netsettings.infosendport);
				SimpleBuf simple=MakeSimple(REQUESTINFO,0,0);
				infosock.Send(simple.b,sizeof(simple.b));
				infosock.Flush();
				SendDlgItemMessage(dlghwnd,IDC_LOCALGAMES,LB_RESETCONTENT,0,0);
				numlocallistaddr=0;
				ConsoleDisplayText("Sent info request");
				return 1;			
			}
			if(LOWORD(wParam)==IDC_JOININTERNET)
			{
				datasock.ResetReliable();
				SOCKADDR_IN dest;
				infosock.dest=MakeSockAddr(255,255,255,255,netsettings.infosendport);
				char iptext[40];
				GetWindowText(GetDlgItem(hDlg,IDC_IP),iptext,40);
				
				infosock.dest.sin_addr.s_addr=inet_addr(iptext);
				SimpleBuf simple=MakeSimple(REQUESTINFO,0,0);
				infosock.Send(simple.b,sizeof(simple.b));
				infosock.Flush();
				SendDlgItemMessage(dlghwnd,IDC_LOCALGAMES,LB_RESETCONTENT,0,0);
				numlocallistaddr=0;
				ConsoleDisplayText("Sent info request");
				return 1;			
			}
			if(LOWORD(wParam)==IDC_JOINLOCAL)
			{
				datasock.ResetReliable();
				HWND listhwnd=GetDlgItem(dlghwnd,IDC_LOCALGAMES);
				int lbsel=SendMessage(listhwnd,LB_GETCURSEL,0,0);
				if(lbsel==LB_ERR) return 1;
				infosock.dest=locallistaddr[lbsel];
				SendRequestEnter();
				connected=0;
				GetLocalPlayer()->playstate=1;
				GetLocalPlayer()->timeout=curtime+2000;
				EndDialog(hDlg,1);
				ConsoleDisplayText("Sent connection request");
			}
			break;
	}
    return FALSE;
}
#else

//---- server list for the join screen (src/ui/joinscreen.cpp draws it) ----

JoinGame joingames[MAXLOCALLIST];
static DWORD lastinfoquery;   //GetTickCount() of the last info request, for the ping

void JoinNetOpen()
{
	LoadNetSettings();
	if(!infosock.link)
		infosock.Open(NetOpenInfoLink(netsettings.infosendport,0));
}

void JoinNetClose()
{
	GetLocalPlayer()->playstate=0;
	infosock.Close();
}

void JoinNetClearList()
{
	numlocallistaddr=0;
}

int JoinGameCount()
{
	return numlocallistaddr;
}

JoinGame *JoinGameAt(int i)
{
	if(i<0||i>=numlocallistaddr) return 0;
	return &joingames[i];
}

static void SendInfoRequest(SOCKADDR_IN dest)
{
	infosock.dest=dest;
	SimpleBuf simple=MakeSimple(REQUESTINFO,0,0);
	infosock.Send(simple.b,sizeof(simple.b));
	infosock.Flush();
	lastinfoquery=GetTickCount();
}

void JoinNetSearchLAN()
{
	SendInfoRequest(MakeSockAddr(255,255,255,255,netsettings.infosendport));
}

//asks one server (IP or host name) for its info; it shows up in the list when it
//answers. port: its info port (host order), 0 for ours (net.ini infosendport).
//Returns 0 if the host name can't be resolved.
int JoinNetQuery(const char *host,int port)
{
	in_addr_t addr=inet_addr(host);
	if(addr==INADDR_NONE)
	{
		addrinfo hints,*res=0;
		memset(&hints,0,sizeof(hints));
		hints.ai_family=AF_INET;
		hints.ai_socktype=SOCK_DGRAM;
		if(getaddrinfo(host,0,&hints,&res)!=0||!res)
			return 0;
		addr=((sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
		freeaddrinfo(res);
	}
	SOCKADDR_IN dest=MakeSockAddr(0,0,0,0,port?htons((WORD)port):netsettings.infosendport);
	dest.sin_addr.s_addr=addr;
	NetQueryHost(&dest);
	SendInfoRequest(dest);
	return 1;
}

//an Internet game joined P2P (M7d): the host identity the lobby lists, named by the
//address and info port it lists (they then find its INFO answer, JoinNetFind). The
//info link becomes a GameNetworkingSockets one if it isn't. Returns 0 if P2P can't
//be used (the caller asks the host directly, JoinNetQuery).
int JoinNetQueryP2P(const char *address,int port,const char *identity)
{
	if(!NetLinkIsGns(infosock.link))
	{
		infosock.Close();
		infosock.Open(NetOpenInfoLink(netsettings.infosendport,0,1));
		if(!NetLinkIsGns(infosock.link)) return 0;
	}
	SOCKADDR_IN dest=MakeSockAddr(0,0,0,0,htons((WORD)port));
	dest.sin_addr.s_addr=inet_addr(address);
	if(dest.sin_addr.s_addr==INADDR_NONE||!NetQueryHostP2P(&dest,identity)) return 0;
	SendInfoRequest(dest);   //goes out once connected
	return 1;
}

//asks a server for its info again, without opening anything (an Internet game
//being joined: P2P, or UDP if that failed)
void JoinNetRequestInfo(const char *address,int port)
{
	SOCKADDR_IN dest=MakeSockAddr(0,0,0,0,htons((WORD)port));
	dest.sin_addr.s_addr=inet_addr(address);
	if(dest.sin_addr.s_addr!=INADDR_NONE) SendInfoRequest(dest);
}

//index in the list of the server that answered from address:port (an Internet
//game being joined), -1 if none yet
int JoinNetFind(const char *address,int port)
{
	in_addr_t addr=inet_addr(address);
	for(int i=0;i<numlocallistaddr;i++)
		if(locallistaddr[i].sin_addr.s_addr==addr&&ntohs(locallistaddr[i].sin_port)==port)
			return i;
	return -1;
}

//sends the join request to a listed server. Returns 0 (and the reason) if it
//can't be joined.
int JoinNetConnect(int index,char *reason,int reasonsize)
{
	JoinGame *game=JoinGameAt(index);
	if(!game) return 0;
	if(!game->compatible)
	{
		snprintf(reason,reasonsize,"%s",game->reason);
		return 0;
	}
	datasock.ResetReliable();
	infosock.dest=locallistaddr[index];
	SendRequestEnter();
	connected=0;
	GetLocalPlayer()->playstate=1;
	GetLocalPlayer()->timeout=curtime+2000;
	ConsoleDisplayText("Sent connection request");
	return 1;
}

//debug aid (HK_MENU=joindemo): a fake entry in the server list
void JoinNetDebugAdd(const char *name,const char *addr,const char *version,int protocol,int ping,int players,int mode)
{
	if(numlocallistaddr>=MAXLOCALLIST) return;
	JoinGame &game=joingames[numlocallistaddr];
	memset(&locallistaddr[numlocallistaddr],0,sizeof(SOCKADDR_IN));
	numlocallistaddr++;
	memset(&game,0,sizeof(game));
	strncpy(game.name,name,sizeof(game.name)-1);
	strncpy(game.address,addr,sizeof(game.address)-1);
	strncpy(game.version,version,sizeof(game.version)-1);
	PeerVersion peer;
	memset(&peer,0,sizeof(peer));
	peer.present=protocol>1;
	peer.protocol=protocol;
	peer.layout=EDIFICE_NET_LAYOUT;
	strncpy(peer.version,version,sizeof(peer.version)-1);
	game.compatible=VersionCompatible(&peer,game.reason,sizeof(game.reason));
	game.ping=ping;
	game.players=players;
	game.maxplayers=MAXPLAYERS;
	game.gamemode=mode;
	game.capturelimit=5;
}

//INFO answer: name, version, then (servers since 0.3.0) players, max players,
//game mode, capture limit
static void JoinNetAddGame(const char *msg,int msglen,SOCKADDR_IN source)
{
	char name[100];
	int trailerpos=ReadHelloName(msg,msglen,name,sizeof(name));
	const char *trailer=msg+trailerpos;
	int trailerlen=msglen-trailerpos;
	PeerVersion peer;
	ReadVersionTrailer(trailer,trailerlen,&peer);

	int i;
	for(i=0;i<numlocallistaddr;i++)
		if(locallistaddr[i].sin_addr.s_addr==source.sin_addr.s_addr&&locallistaddr[i].sin_port==source.sin_port)
			break;
	if(i==MAXLOCALLIST) return;
	if(i==numlocallistaddr) numlocallistaddr++;
	locallistaddr[i]=source;
	JoinGame &game=joingames[i];
	memset(&game,0,sizeof(game));
	strncpy(game.name,name,sizeof(game.name)-1);
	strcpy(game.version,peer.version);
	strncpy(game.address,inet_ntoa(source.sin_addr),sizeof(game.address)-1);
	game.compatible=VersionCompatible(&peer,game.reason,sizeof(game.reason));
	game.ping=GetTickCount()-lastinfoquery;
	game.players=game.maxplayers=-1;
	if(peer.present)
	{
		int extra=11+(unsigned char)trailer[10];
		if(trailerlen>=extra+4)
		{
			game.players=(unsigned char)trailer[extra];
			game.maxplayers=(unsigned char)trailer[extra+1];
			game.gamemode=(unsigned char)trailer[extra+2];
			game.capturelimit=(unsigned char)trailer[extra+3];
		}
	}
}

#endif // HK_WIN32_NATIVE

int GetNumPlayers()
{
	return numplayers;
}
int IsServer()
{
	return server;
}
int IsConnected()
{
	return connected;
}

//called by the client and server when a player leaves

void DeletePlayer(int num,int reason)
{
	
	if(server)
	{
		GPlayerLeave(num);
		SimpleBuf deleteplayer=MakeSimple(DELETEPLAYER,num,reason);
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(deleteplayer.b,sizeof(deleteplayer.b),0);
		}
		
		clientsock[num].Close();
		
		clientsock[num].ResetReliable();
		
	}
	int localnum=GetLocalNum();
	if(localnum==num)
	{
		Disconnect();
		SetLocalNum(0);
		GetLocalPlayer()->playstate=0;
	}
	if(localnum>num)
	{
		SetLocalNum(localnum-1);
	}
	char message[1000];
	strcpy(message,GetPlayer(num)->name);
	switch (reason)
	{
	case 0:
		strcat(message," has timed out");
		break;
	case 1:
		strcat(message," has left the game");
		break;
	case 2:
		strcat(message," has been kicked");
		break;
	}
	ConsoleDisplayText(message);
	VoiceReset();   //players are renumbered
	numplayers--;
	for(int i=num;i<numplayers;i++)
	{
		CopyMemory(GetPlayer(i),GetPlayer(i+1),sizeof(Player));
		if(server) CopyMemory(&clientsock[i],&clientsock[i+1],sizeof(Sock));
		acplayer[i]=acplayer[i+1];
	}
	ZeroMemory(GetPlayer(numplayers),sizeof(Player));
	ZeroMemory(&clientsock[numplayers],sizeof(Sock));
	
}

//sends the land information necessary to construct the same landscape

//builds the land described by LandDefs and SkySettings (already sanitized:
//SanitizeLandInfo): what a client does on LANDINFO, and the server when it
//loads a map (src/game/mapfile.h). The blocks are cleared.

void ApplyLand(const LandDefs *defs,const SkySettings *sky)
{
	CopyMemory(GetDefs(),defs,sizeof(LandDefs));
	RedrawSplash(800,"Loading Textures");
	LoadTextures();
	RedrawSplash(800,"Loading Sky");
	CopyMemory(GetSkySettings(),sky,sizeof(SkySettings));
	//the sky's ambient sound, as LoadSky does on the server
	if(strlen(GetSkySettings()->sound))
	{
		char soundname[200];
		snprintf(soundname,sizeof(soundname),"sound\\%s",GetSkySettings()->sound);
		LoadSound(soundname,0,0);
		PlaySound(0,1);
	}
	char fullfilename[400];
	if(strlen(GetSkySettings()->cloudtex))
	{
		strcpy(fullfilename,"textures\\clouds\\");
		strcat(fullfilename,GetSkySettings()->cloudtex);
		LoadTexture(fullfilename,TEX_CLOUDS);
	}
	if(strlen(GetSkySettings()->misttex))
	{
		strcpy(fullfilename,"textures\\clouds\\");
		strcat(fullfilename,GetSkySettings()->misttex);
		LoadTexture(fullfilename,TEX_CLOUDMIST);
	}
	RedrawSplash(800,"Creating Land");
	CreateLand(*GetDefs());
}

void SendLandInfo(int num)
{
	static LandInfo landinfo;
	static char buf[LANDINFOSIZE];
	landinfo.type=LANDINFO;
	CopyMemory(&landinfo.ldfs,GetDefs(),sizeof(landinfo.ldfs));
	CopyMemory(&landinfo.skysets,GetSkySettings(),sizeof(SkySettings));
	int size=WriteLandInfo(buf,sizeof(buf),&landinfo);
	if(num>0)
	{
		GetPlayer(num)->playstate=1;
		clientsock[num].SendReliable(buf,size,RECEIVEDLANDINFO);
	}
	else
	{
		for(int i=1;i<numplayers;i++)
		{
			GetPlayer(i)->playstate=1;
			clientsock[i].SendReliable(buf,size,RECEIVEDLANDINFO);
		}
	}
}


#define pi 3.14159265


//converts the angle made by x and y to a byte

unsigned char GetAng(float x,float y)
{
	float a=atan(y/x);
	if(x<0)a-=pi;
	a/=pi;
	a+=.5;
	a*=127;
	a+=1;
	return -a;	
}

//called to receive a player position

//sender: the client that sent it (server side), or -1 for positions relayed by the server
void ReceivePosition(const char *msg,int msglen,int sender)
{
	PlayerPosition position;
	if(!ReadPosition(msg,msglen,&position)) return;
	PlayerPosition *pos=&position;
	int pnum=sender>=0?sender:pos->pnum;
	if(!ValidPlayer(pnum)) return;
	Player *src=GetPlayer(pnum);
	src->up=!!((1<<0)&pos->keys);
	src->down=!!((1<<1)&pos->keys);
	src->lstrafe=!!((1<<2)&pos->keys);
	src->rstrafe=!!((1<<3)&pos->keys);
	src->jetpack=!!((1<<4)&pos->keys);
	src->hanggliding=!!((1<<5)&pos->keys);
	src->pos.x=(float)pos->posx/65536*LANDGRIDSIZE;
	src->pos.y=(float)pos->posy/65536*LANDGRIDSIZE;
	src->pos.z=(float)pos->posz/65536*LANDGRIDSIZE;
	src->fwd.x=sin(((float)pos->fwd/255-.5)*pi*2);
	src->fwd.z=cos(((float)pos->fwd/255-.5)*pi*2);
	src->tilt.x=sin(((float)pos->tilt/255-.5)*pi*2);
	src->tilt.y=cos(((float)pos->tilt/255-.5)*pi*2);
	src->vel.x=(float)pos->velx/64;
	src->vel.y=(float)pos->vely/64;
	src->vel.z=(float)pos->velz/64;
	src->weapon=ValidWeapon(pos->weapon)?pos->weapon:0;
	if(sender>=0)
	{
		//a client only reports being alive or dead, and stays dead while the
		//server says so (M11)
		if(pos->state==10||pos->state==11)
			src->playstate=AcPosition(pnum,pos->state,src->pos,src->weapon);
	}
	else if(pos->state!=1) 
		src->playstate=pos->state;
	MakeClose(&src->pos);
}

//sends a certain player's position

void SendPosition(char srcnum,char destnum)
{
	Player *src=GetPlayer(srcnum);
	PlayerPosition pos;
	pos.type=PLAYERPOSITION;
	
	pos.keys=0;
	pos.keys|=(1<<0)*src->up;
	pos.keys|=(1<<1)*src->down;
	pos.keys|=(1<<2)*src->lstrafe;
	pos.keys|=(1<<3)*src->rstrafe;
	pos.keys|=(1<<4)*src->jetpack;
	pos.keys|=(1<<5)*src->hanggliding;
	
	pos.posx=src->pos.x/LANDGRIDSIZE*65536;
	pos.posy=src->pos.y/LANDGRIDSIZE*65536;
	pos.posz=src->pos.z/LANDGRIDSIZE*65536;
	pos.fwd=GetAng(src->fwd.x,src->fwd.z);
	pos.tilt=GetAng(src->tilt.x,src->tilt.y);
	pos.velx=src->vel.x*64;
	pos.vely=src->vel.y*64;
	pos.velz=src->vel.z*64;
	pos.weapon=src->weapon;
	pos.pnum=srcnum;
	pos.state=src->playstate;
	char buf[POSITIONSIZE];
	int size=WritePosition(buf,sizeof(buf),&pos);
	if(!server)
	{
		datasock.Send(buf,size);
	}
	else
	{
		clientsock[destnum].Send(buf,size);
	}
}

//---- voice chat (src/audio/voice.cpp): VOICE goes only to players in range ----

static_assert((int)VOICE_WORLDSIZE==LANDGRIDSIZE,"voice.h: VOICE_WORLDSIZE must be LANDGRIDSIZE");

static int VoiceNear(int a,int b)
{
	Vector pa=GetPlayer(a)->pos,pb=GetPlayer(b)->pos;
	return VoiceDistance(pa.x,pa.y,pa.z,pb.x,pb.y,pb.z)<VOICE_RANGE;
}

//server: sends a voice frame of voice->player to the players near it, and plays
//it here if the host is one of them
static void RelayVoice(VoiceMessage *voice)
{
	int speaker=voice->player;
	if(!ValidPlayer(speaker)||speaker>=numplayers) return;
	char buf[VOICEMSGSIZE];
	int size=WriteVoice(buf,sizeof(buf),voice);
	if(!size) return;
	static int voicelog=getenv("HK_VOICE_LOG")!=0;
	if(voicelog)   //debug: why a frame went (or not) to each player
		for(int i=0;i<numplayers;i++)
		{
			Vector pa=GetPlayer(speaker)->pos,pb=GetPlayer(i)->pos;
			fprintf(stderr,"relay %d seq %d -> %d: state %d/%d dist %.1f (%.1f %.1f %.1f)-(%.1f %.1f %.1f)\n",
				speaker,voice->seq,i,GetPlayer(speaker)->playstate,GetPlayer(i)->playstate,
				VoiceDistance(pa.x,pa.y,pa.z,pb.x,pb.y,pb.z),pa.x,pa.y,pa.z,pb.x,pb.y,pb.z);
		}
	for(int i=1;i<numplayers;i++)
		if(i!=speaker&&GetPlayer(i)->playstate>=10&&VoiceNear(speaker,i))
			clientsock[i].Send(buf,size);
	if(speaker!=0&&GetPlayer(0)->playstate>=10&&VoiceNear(speaker,0))
		VoiceReceive(speaker,voice->seq,voice->data,voice->len);
}

//sends one encoded frame of the local player (voice.cpp)
void SendVoiceFrame(int seq,const unsigned char *data,int len)
{
	if(len<1||len>VOICEMAXFRAME) return;
	if(GetLocalPlayer()->playstate<10) return;
	VoiceMessage voice;
	voice.player=GetLocalNum();
	voice.seq=(uint16_t)seq;
	voice.len=(uint16_t)len;
	memcpy(voice.data,data,len);
	if(server)
	{
		RelayVoice(&voice);
		return;
	}
	if(!connected) return;
	char buf[VOICEMSGSIZE];
	int size=WriteVoice(buf,sizeof(buf),&voice);
	if(size) datasock.Send(buf,size);
}

//called in structure.cpp when a building is going to be sent

void StartBlockBuffer()
{
	numblocks=0;
	blockbufferpos=0;
	blockbuffer[blockbufferpos]=BLOCKS;
	blockbufferpos++;
	blockbufferpos+=4; //for numblocks
}

//called by structure.cpp

void AddBlockBuffer(char x,char y,char z,char type,char team,char top,char side,char bottom,char del)
{
	if(blockbufferpos+30>BIGBUFSIZE)
	{
		return;
	}
	blockbuffer[blockbufferpos]=x;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=y;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=z;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=type;
	blockbufferpos++;	
	blockbuffer[blockbufferpos]=team;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=top;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=side;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=bottom;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=del;
	blockbufferpos++;
	numblocks++;
}

//finishes things off

void SendBlockBuffer()
{
	WirePutU32(&blockbuffer[1],numblocks);
	if(server)
	{
		ReceiveBlockBuffer(blockbuffer,0,blockbufferpos);
	}
	else
	{
		datasock.SendReliable(blockbuffer,blockbufferpos,0);
	}
}

//creates a structure

//len: size of the message, type included
void ReceiveBlockBuffer(char *buf,int pnum,int len)
{
	if(len<5) return;
	//only near the player; a refused message costs no credit (M11)
	if(server&&pnum>0&&!AcBlocksNear(buf,ReadBlocksCount(buf,len),pnum)) return;
	if(server&&!GetPlayer(pnum)->credits[0])
	{
		return;
	}
	GiveCredit(pnum,0,-1);
						
	int numblocks=ReadBlocksCount(buf,len);   //bounded by len (netmsg.h)
	int pos=BLOCKSHEADERSIZE+numblocks*BLOCKEDITSIZE;
	int i;
	for(i=0;i<numblocks;i++)
	{
		BlockEdit b;
		ReadBlockEdit(buf,i,&b);
		if(!ValidBlockEdit(&b)) continue;   //type or texture the renderer can't index
		if(b.del)
		{
			DeleteBlock(b.x,b.y,b.z);
		}
		else
		{
			AddBlock(b.x,b.y,b.z,b.type,b.top,b.side,b.bottom);
		}
	}
	if(server)
	{
		WirePutU32(&buf[1],numblocks);   //relay the blocks actually received
		for(i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(buf,pos,0);
		}
	}
}

int SendMultMessage(const void *msg,int len,int player=-1)
{
	int selfrecv=0;
	if(!server) return 0;
	for(int i=0;i<numplayers;i++)
	{
		if(player==TARGTEAM||GetPlayer(i)->team==player-TARGTEAM||player==-1||i==player)
		{
			if(i==0)
				selfrecv=1;
			else
				clientsock[i].SendReliable(msg,len,0);
		}
	}
	return selfrecv;
}


//CHATMESSAGE: type, int8 dest (CMSG_*), uint8 sender, uint16 length, text
//msglen: size of the message, type included
void ReceiveChatMessage(char *msg,int msglen)
{
	ChatMessage chat;
	if(!ReadChat(msg,msglen,&chat)) return;
	char dest=chat.dest;
	int sender=chat.sender;
	int len=chat.len;
	//score, objective and info lines come from the server only
	if(server&&sender&&dest!=CMSG_BROADCAST&&dest!=CMSG_TEAM) return;
	//if(server==1&&(dest!=CMSG_BROADCAST&&dest!=CMSG_TEAM)) return;
	


	char *chatmessage=chat.text;
#ifdef HK_WIN32_NATIVE
	char message[1000];
#endif

	if(dest==CMSG_BROADCAST||dest==CMSG_TEAM)
	{
#ifdef HK_WIN32_NATIVE
		snprintf(message,sizeof(message),"%.31s: %s",ValidPlayer(sender)?GetPlayer(sender)->name:"?",chatmessage);
		ConsoleDisplayText(message);
#else
		ChatAddLine(ValidPlayer(sender)?GetPlayer(sender)->name:"?",ValidPlayer(sender)?GetPlayer(sender)->team:0,
			dest==CMSG_TEAM,chatmessage);
#endif
	}
	if(dest==CMSG_SCORE)
	{
		strcpy(curscore,chatmessage);
	}
	if(dest==CMSG_OBJECTIVE)
	{
		strcpy(curobjective,chatmessage);
	}
	if(dest==CMSG_INFO)
	{
		ConsoleDisplayText(chatmessage);
	}
	/*if(server)
	{
		for(int i2=1;i2<numplayers;i2++)
		{
			clientsock[i2].SendReliable(msg,len+5,0);
		}
	}*/
	if(sender)  //this came from a player (called from serverloop)
	{
		if(dest==CMSG_BROADCAST)
			SendMultMessage(msg,len+5,-1);
		if(dest==CMSG_TEAM&&ValidPlayer(sender))
			SendMultMessage(msg,len+5,GetPlayer(sender)->team+TARGTEAM);
	}

}

void SendChatMessage(char *text,int dest,int player)
{
	static char chat[5+60000];
	int len=strlen(text);
	if(len>60000) len=60000;
	ByteWriter w(chat,sizeof(chat));
	w.U8(CHATMESSAGE);
	w.U8(dest);
	w.U8(0);   //sender: the server fills it in
	w.U16(len);
	w.Bytes(text,len);
	if(server)
	{
		if(SendMultMessage(chat,w.Size(),player))
			ReceiveChatMessage(chat,w.Size());
	}
	else
	{
		datasock.SendReliable(chat,w.Size(),0);
	}
}

//called when a player shoots

void CreateObject(int num,int obj,Vector pos,Vector dir,Vector vel)
{
	for(int i=0;i<MAXOBJS;i++)
	{
		if(object[i].type==0)
		{
			object[i].type=obj;
			object[i].pos=pos;
			object[i].vel=vel;
			object[i].dir=dir;
			object[i].pnum=num;
			object[i].timeout=curtime+1000;
			object[i].achit=0;

			object[i].wasfroze=0;
			switch(obj)
			{
			case 0:
			case 1:
			case 2:
			case 3:
				object[i].object=&projectile[0];
				break;
			case 4:
				object[i].object=&projectile[2];
				object[i].timeout=curtime+sv.grenadeexplodetime;
				break;
			}
			break;
		}
	}
}

//called on all computers when someone fires

void HandleFireGun(FireGunDirection *firegun)
{
	if(!ValidPlayer(firegun->pnum)) return;
	float speed=0;
	if(GetPlayer(firegun->pnum)->weapon==0)
		speed=sv.gatspeed;
	if(GetPlayer(firegun->pnum)->weapon==1)
		speed=sv.m16speed;
	if(GetPlayer(firegun->pnum)->weapon==2)
		speed=sv.steyrspeed;
	if(GetPlayer(firegun->pnum)->weapon==3)
		speed=sv.umpspeed;
	if(GetPlayer(firegun->pnum)->weapon==4)
		speed=sv.grenadespeed;
	if(GetPlayer(firegun->pnum)->weapon==5)
		speed=sv.berettaspeed;
	if(GetPlayer(firegun->pnum)->weapon==6)
		speed=sv.sr1000speed;
	int objecttype=1;
	if(GetPlayer(firegun->pnum)->weapon==3) objecttype=3;
	if(GetPlayer(firegun->pnum)->weapon==5) objecttype=3;
	if(GetPlayer(firegun->pnum)->weapon==4) objecttype=4;
	speed*=2;
	if(speed==0) return;
	if(!firegun->reload) CreateObject(firegun->pnum,objecttype,firegun->pos-Vector(0,.2,0),firegun->dir,firegun->dir*speed);
	int soundnum=0;
	if(!firegun->reload)
	{
		float factor=1;
		if(GetPlayer(firegun->pnum)->weapon==0) factor=2.5;
		GetPlayer(firegun->pnum)->inaccuracyvel=Vector(RND-.5,RND-.3,RND-.5)*.02*factor;
		GetPlayer(firegun->pnum)->inaccuracy=GetPlayer(firegun->pnum)->inaccuracyvel*3*factor;
	}
	soundnum=GetPlayer(firegun->pnum)->weapon*4+2;
	if(firegun->reload) soundnum+=2;
	if(firegun->pnum==GetLocalNum()) soundnum--;
	if(soundnum)
	{
		SetSoundPosition(soundnum,GetPlayer(firegun->pnum)->pos);
		PlaySound(soundnum,0);
	}
}

//tries to fire the gun, and sends out the message that the gun was fired

int TryFireGun(int num,Vector pos,Vector dir,char reload)
{
	if(!reload)
	{
		if(GetPlayer(num)->ammoingun[GetPlayer(num)->weapon]<=0&&GetPlayer(num)==GetLocalPlayer())
			return 0;
		GetPlayer(num)->ammoingun[GetPlayer(num)->weapon]--;
		
		
	}
	FireGunDirection firegun;
	firegun.type=FIREGUN;
	firegun.pnum=num;
	firegun.dir=dir;
	firegun.pos=pos;
	firegun.reload=reload;
	HandleFireGun(&firegun);
	char buf[FIREGUNSIZE];
	int size=WriteFireGun(buf,sizeof(buf),&firegun);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
		{
			if(i!=num) clientsock[i].Send(buf,size);
		}

	}
	else
	{
		datasock.Send(buf,size);
	}
	return 1;
}

//sends to all computers to play this sound

void HavePain(int sound,int targ)
{
	if(!server||(targ==-1||GetLocalPlayer()->team==targ-TARGTEAM||targ==0))
		PlaySound(sound,0);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
		{
			if(targ==-1||GetPlayer(i)->team==targ-TARGTEAM||targ==i) 
				SendSimple(i,PAIN,GetLocalNum(),sound);
		}
	}
	else
	{
		SendSimple(0,PAIN,GetLocalNum(),sound);
	}
}

//when someone dies somehow this is called

void ReceiveKillMessage(int killed,int killer)
{
	if(!ValidPlayer(killed)||!ValidPlayer(killer)) return;
	GetPlayer(killed)->deaths++;
	if(killed!=killer) GetPlayer(killer)->kills++;
	char cmsg[300];
	char *skilled=GetPlayer(killed)->name;
	char *skiller=GetPlayer(killer)->name;
	if(killed==killer) 
	{
		skilled="himself";
	}
		
	snprintf(cmsg,sizeof(cmsg),"%.31s killed %.31s",GetPlayer(killer)->name,skilled);
	ConsoleDisplayText(cmsg);
	if(server)
	for(int i=0;i<MAXSERVEROBJS;i++)
	if(sobject[i].type&&sobject[i].attached==killed)
	{
		ClaimObject(killed);
		break;
	}
}

//called by the person that was killed

void Killed(int killer)
{
	SimpleBuf killed=MakeSimple(KILLED,GetLocalNum(),killer);
	ReceiveKillMessage(GetLocalNum(),killer);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
			clientsock[i].SendReliable(killed.b,sizeof(killed.b),0);
	}
	else
	{
		datasock.SendReliable(killed.b,sizeof(killed.b),0);
	}
}

//called when someone joins

void ResetPlayer(int num)
{
	GetPlayer(num)->gunmainup=Vector(0,1,0);
	GetPlayer(num)->pos=Vector(0,0,0);
	GetPlayer(num)->gunup=Vector(0,1,0);
}


//sends the various server controlled variables, like gravity, gun properties, etc.

void SendServerVars(int num)
{
	ServerVarsMessage vars;
	vars.type=SERVERVARS;
	vars.v=sv;
	char buf[SERVERVARSSIZE];
	int size=WriteServerVars(buf,sizeof(buf),&vars);
	if(server)
	{
		if(num)
		{
			clientsock[num].SendReliable(buf,size,0);
		}
		else
		{
			for(int i=0;i<numplayers;i++)
			{
				clientsock[i].SendReliable(buf,size,0);
			}
		}
	}
	else
	{
		datasock.SendReliable(buf,size,0);
	}
}

void SendRGD(int player)
{
	RemoteGameDataMessage data;
	data.type=REMOTEGAMEDATA;
	data.d=rgd;
	char buf[RGDSIZE];
	int size=WriteRGD(buf,sizeof(buf),&data);
	SendMultMessage(buf,size);
}
//called by everyone when someone changes team

void ReceiveChangeTeam(int player,int team)
{
	if(!ValidPlayer(player)||team<0||team>2) return;
	char message[500];
	char *teamname="White";
	if(team==0) teamname="White";
	if(team==1) teamname="Red";
	if(team==2) teamname="Blue";
	snprintf(message,sizeof(message),"%.31s has joined team %s",GetPlayer(player)->name,teamname);
	ConsoleDisplayText(message);
	GetPlayer(player)->team=team;
}

//called by console.cpp when a player's team is changed

void ChangeTeam(int player,int team)
{
	if(team<0||team>2) return;
	if((team=GPlayerChangeTeam(player,team))==-1) return;
	SimpleBuf changeteam=MakeSimple(CHANGETEAM,player,team);
	if(server)
	{
		ReceiveChangeTeam(player,team);
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(changeteam.b,sizeof(changeteam.b),0);
		}
	}
	else
	{
		datasock.SendReliable(changeteam.b,sizeof(changeteam.b),0);
	}
}

//updates the position of an server controlled object (sent reliably so it only
//needs to be done once.

void SendUpdateServerObject(int num)
{
	if(!ValidSObj(num)) return;
	UpdateServerObject update;
	update.type=UPDATESERVEROBJECT;
	update.num=num;
	update.updated=sobject[num];   //timeout is not sent
	char buf[UPDATESERVEROBJECTSIZE];
	int size=WriteUpdateServerObject(buf,sizeof(buf),&update);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(buf,size,0);
		}
	}
}

void ReceiveUpdateServerObject(UpdateServerObject *update)
{
	if(!ValidSObj(update->num)) return;
	int timeout=sobject[update->num].timeout;   //not sent: stays as it was
	sobject[update->num]=update->updated;
	sobject[update->num].timeout=timeout;
}

//called by console.cpp for the command, "createdome"
//basically creates a server controlled object

void CreateInstallation(Vector pos,char team)
{
	if(!server) return;
	ServerObject *cur=0;
	int i;
	for(i=0;i<MAXSERVEROBJS;i++)
	if(!sobject[i].type)
	{
		cur=&sobject[i];
		break;
	}
	if(!cur) return;
	cur->type=1;
	cur->pos=pos;
	cur->dir=Vector(1,0,0);
	cur->team=team;
	cur->vel=Vector(0,0,0);
	cur->attached=-1;
	cur->speedpenalty=1;
	SendUpdateServerObject(i);
}

//called when someone tries to pick up a server controlled object

void ClaimObject(int pnum)
{
	if(GetPlayer(pnum)->playstate!=10) return;
	if(!server)
	{
		SimpleBuf claim=MakeSimple(CLAIMOBJECT,0,0);
		datasock.SendReliable(claim.b,sizeof(claim.b),0);
		return;
	}
	Vector pos=GetPlayer(pnum)->pos;
	float leastmag=50;
	int candidate;
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		
		if(sobject[i].attached==-1)
		if(Magnitude(pos-sobject[i].pos)<leastmag&&GCanClaimObject(pnum,i))
		{
			leastmag=Magnitude(pos-sobject[i].pos);
			candidate=i;
		}
		if(sobject[i].attached==pnum&&sobject[i].type)
		{
			if(GDropObject(pnum,i))
			{
				sobject[i].attached=-1;
				sobject[i].pos.y-=1;
				SendUpdateServerObject(i);
			}
			return;
		}
	}
	if(leastmag<2.4)
	{
		GClaimObject(pnum,candidate);
	}
}
						
//resets the scores of all players

void ScoreReset()
{
	SimpleBuf scorereset=MakeSimple(SCORERESET,0,0);
	ConsoleDisplayText("Scores reset");
	for(int i=0;i<numplayers;i++)
	{
		GetPlayer(i)->kills=0;
		GetPlayer(i)->deaths=0;
	}
	if(server)
	for(int i=1;i<numplayers;i++)
	{
		clientsock[i].SendReliable(scorereset.b,sizeof(scorereset.b),0);
	}
}

void GiveCredit(int player, int credit, int number)
{
	if(!server) return;
	for(int i=0;i<numplayers;i++)
	{
		if(player==TARGTEAM||GetPlayer(i)->team==player-TARGTEAM||player==-1||i==player)
		{
			if(credit>=0)
			{
				if(number==0) GetPlayer(i)->credits[credit]=0;
				GetPlayer(i)->credits[credit]+=number;
			}
			else
			{
				ZeroMemory(&GetPlayer(i)->credits,sizeof(GetPlayer(i)->credits));
			}
			SimpleBuf crd;
			if(credit>=0)
				crd=MakeSimple(CREDITS,credit,GetPlayer(i)->credits[credit]);
			else
				crd=MakeSimple(CREDITS,credit,0);
			if(i>0) 
				clientsock[i].SendReliable(crd.b,sizeof(crd.b),0);
		}
	}
}