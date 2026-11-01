#ifndef MULTIPLAYSTRUCTH
#define MULTIPLAYSTRUCTH
#include "sock.h"
#include "land.h"
#include "playing.h"   //ServerObject, ServerVars

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
//server answers a REQUESTENTER without a valid join cookie: msg, msg2 = the cookie
//(8 bytes, little-endian), to send back in the next REQUESTENTER. Only to clients
//whose REQUESTENTER has HELLOCAP_COOKIE (version.h; M11 phase 2, docs/M11.md).
#define ENTERCOOKIE 27
//M11 phase 3a (docs/M11.md): the server decides hits and deaths. Only to clients
//whose REQUESTENTER has HELLOCAP_SERVERHITS (version.h); older ones never get them.
//HITMODE: msg 1 = the server decides, stop counting your own damage (0: count it
//as before). Sent at the join and when ac_serverhits changes.
//SERVERHIT: you were hit, msg = by whom, msg2 = your life now (0: KILLED follows).
#define SERVERHIT 28
#define HITMODE 29
//DELETEPLAYER: msg = the player who left, msg2 = reason (0 timed out, 1 left, 2
//kicked). With PLAYERLEFT_KEEPSLOTS (M11 phase 3b, docs/M11.md) the others keep
//their numbers and the slot is free; without it everyone after msg moves down one
//(what older builds always do). Only sent with it when every client has
//HELLOCAP_STABLESLOTS (version.h).
#define PLAYERLEFT_KEEPSLOTS 0x100
#define PLAYERLEFT_REASON 0xFF

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
