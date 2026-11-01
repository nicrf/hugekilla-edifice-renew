#ifndef MULTIPLAYSTRUCTH
#define MULTIPLAYSTRUCTH
#include "sock.h"

//main messages
#define REQUESTENTER 1
#define INFO 2
#define PLAYERPOSITION 3
#define LANDINFO 4
#define CONSOLEMESSAGE 6
#define BLOCKS 7
#define CHATMESSAGE 8
#define CREATEOBJ 9          //never sent
#define FIREGUN 10
#define SERVERVARS 11
#define UPDATESERVEROBJECT 12
#define SERVEROBJECTSNAPSHOT 13
#define KILLEDPOSITION 14
#define REMOTEGAMEDATA 15
#include "voicemsg.h"    //VOICE 16 (5 is SIMPLE, sock.h)


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

//Decoded messages. None of these structs goes onto the wire as it is: each
//message has explicit Write/Read functions (netmsg.h, src/net/wire.h).
//REQUESTENTER and INFO are built byte by byte: type, uint8 n, name[n] (<=31,
//<=99), NUL, version trailer (version.h).
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


void HandleFireGun(FireGunDirection *firegun);
void ReceiveKillMessage(int killed,int killer);
void ReceiveChangeTeam(int player,int team);
void ReceiveUpdateServerObject(UpdateServerObject *update);

#endif
