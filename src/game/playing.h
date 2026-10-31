
#ifndef PLAYINGH
#define PLAYINGH
#include "ase.h"

int GetAmmoHold(int num);
struct PhysicsVars
{
	float walkaccelerate;
	float strafeaccelerate;
	float resistance;
	float stopresistance;
};

struct RemoteGameData
{
	int gamestate;
};


struct ServerVars
{
	float walkaccelerate;
	float strafeaccelerate;
	float resistance;
	float stopresistance;
	float gravity;
	int chopperthrust;
	int radarenable;
	int gattimeout,m16timeout,steyrtimeout,umptimeout,grenadetimeout,berettatimeout,sr1000timeout;
	int grenadeexplodetime;
	int gatammohold,m16ammohold,steyrammohold,umpammohold,grenadeammohold,berettaammohold,sr1000ammohold;
	int gatreloadtime,m16reloadtime,steyrreloadtime,umpreloadtime,grenadereloadtime,berettareloadtime,sr1000reloadtime;
	
	float gatspeed,m16speed,steyrspeed,umpspeed,grenadespeed,berettaspeed,sr1000speed;
	float highbulletdamage;
	float lowbulletdamage;
	float grenadedamage;
	float scale;
	float thrustrechargerate;
	float thrustuse;

	int capturelimit;
	int timelimit;
	int gamemode;

	int positionresendtime;
};

struct Player
{
	Vector pos,vel,dir,fwd,tilt,inaccuracy,inaccuracyvel;
	char up,down,lstrafe,rstrafe;
	char weapon;
	char firing;
	char scores; 
	char jump;
	char jetpack;
	char takescreenshot;
	char hangglide;
	char duck;
	char zoom;
	char seemoney;
	
	float gunkick;
	float kickvel;
	float gunupvel;
	float thrustfuel;

	float curturn;
	float roll;
	Vector gunoffset,gunoffsetvel;

	char owngun[20];
	int ammoingun[20];
	int ammoreserves[20];

	DWORD timeout;
	WORD ping;
	WORD packetloss;
	DWORD lastpingtime;
	char pingwasrecv;
	char pingid;
	DWORD nextfootsteps;
	DWORD nextwallscrape;
	DWORD gundowntime;
	DWORD menudissapeartime;
	DWORD respawntime;
	int money;


	char hanggliding;

	char team;
	int kills,deaths;

	Vector gunmainup;
	float gunmainupvel;
	float gunmainupforce;
	int wasthrusting;

	Vector gunup;
	DWORD lastfiretime,nextfiretime;
	DWORD lastswitchtime;
	float curexplosionrot;
	float fzoom;
	short life;
	char playstate;
	char talking;
	char name[32];

	float runtime;
	int legframe;

	char credits[10];

};
struct Object
{
	char pnum;
	char type;
	AseObj *object;
	Vector pos;
	Vector dir;
	Vector vel;
	DWORD timeout;
	char wasfroze;
};

struct ServerObject
{
	char type;
	Vector pos,dir,vel; //make more efficient sometime
	char team;
	char attached;
	float speedpenalty;
	int timeout; //not sent
};

struct Controls
{
	char mousereverse;
	int forward,back,lstrafe,rstrafe,fire,zoom,build,buy,showscores,
		jump,talk,talkteam,reload,secondfire,jetpack,screenshot,claim,
		duck,hangglide,seemoney,pushtotalk;
	float mousesensitivity;
	
};

void LoadControls();
void SetLocalNum(int num);
int GetLocalNum();
Player *GetLocalPlayer();
Player *GetPlayer(int player);
void Playloop();
void HandleKey(DWORD key,DWORD flags);
void HandleKeyUp(DWORD key,DWORD flags);
int HandleConsoleKey(DWORD key,DWORD flags);
char GetRealKey(DWORD key,DWORD flags);
void AddConsoleVar(float *var,char *name,char servervar);
void AddConsoleVar(int *var,char *name,char servervar);
void AddConsoleVar(char *var,char *name,char servervar);
void DrawConsoleNonGL();
void ConsoleDisplayText(char *text);
void SwitchGun(int gun);
float GetSOSpeedPenalty(char pnum);

char *GetStructureFilename(int number);
int LoadStructure(char *filename);
void DisplayBuildingType(char *path);
void PlaceStructure(int task);
void InitConsole();

#define MAXOBJS 200
#define MAXSERVEROBJS 100

#define NOMENU 0x00
#define BUILD 0x1
#define BUY 0x2
#define BUYHANDGUN 0x20
#define BUYRIFLE 0x21
#define BUYAUTOMATIC 0x22
#define BUYACCESSORY 0x23
#define BUILDING 0x40
#define INFOMENU 0x80
#endif