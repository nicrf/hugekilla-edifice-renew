#include "sock.h"

//main messages
#define REQUESTENTER 1
#define INFO 2
#define PLAYERPOSITION 3
#define LANDINFO 4
#define CONSOLEMESSAGE 6
#define BLOCKS 7
#define CHATMESSAGE 8
#define CREATEOBJ 9
#define FIREGUN 10
#define SERVERVARS 11
#define UPDATESERVEROBJECT 12
#define SERVEROBJECTSNAPSHOT 13
#define KILLEDPOSITION 14
#define REMOTEGAMEDATA 15


//simple messages
#define REQUESTINFO 1
#define ENTER 2
#define EXIT 3
#define PORTACK 4
#define RECEIVEDLANDINFO 5
#define PLAYERNAMES 6
#define RECEIVEDPLAYERNAMES 7
#define JOINGAME 8
#define RECEIVEDJOINGAME 9
#define STATECHANGE 10
#define DELETEPLAYER 11
#define PING 12
#define PINGREPLY 13
#define PINGINFO 14
#define SERVERFULL 15
#define RECEIVEDBLOCKINFO 16
#define FIREGUNTMP 17
#define PAIN 18
#define KILLED 19
#define CHANGETEAM 20
#define CLAIMOBJECT 21
#define SCORERESET 22
#define PORTOPENER 23
#define CREDITS 24
#define GAMEOVER 25
#define VERSIONMISMATCH 26   //server refused REQUESTENTER: msg = server protocol (version.h)

struct RequestEnter
{
	char type;
	char namelen;
	char name[32];
};
struct Info
{
	char type;
	char namelen;
	char name[100];
};
struct LandInfo
{
	char type;
	LandDefs ldfs;
	SkySettings skysets;
};


struct PlayerPosition
{
	char type;
	char pnum;
	char weapon;
	char keys;
	char state;
	unsigned char fwd,tilt;
	WORD posx,posy,posz;
	char velx,vely,velz;
};

struct FireGunDirection
{
	char type;
	char pnum;
	char reload;
	Vector dir;
	Vector pos;
};

struct CreateObject
{
	char type;
	char objecttype;
	Vector dir;
	Vector vel;
	Vector pos;
};

struct UpdateServerObject
{
	char type;
	int num;
	ServerObject updated;
};

struct KilledPosition
{
	char type;
	char killed;
	char killer;
	Vector pos;	
};


void HandleFireGun(int num,Message *msg);
void ReceiveKillMessage(int killed,int killer);
void ReceiveChangeTeam(int player,int team);
void ReceiveUpdateServerObject(UpdateServerObject *update);
