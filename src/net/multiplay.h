#ifndef MULTIPLAYH
#define MULTIPLAYH
#include "land.h"
#include "vector.h"
#include "playing.h"
#include "hksocket.h"   //SOCKADDR_IN, WORD
#include "version.h"

//a server found by the join screen (LAN search, address, recent servers)
struct JoinGame
{
	char name[100];
	char address[24];
	char version[16];
	int compatible;
	char reason[120];
	int ping;          //ms
	int players;       //-1: unknown (server older than 0.3.0)
	int maxplayers;
	int gamemode;      //sv.gamemode: 0 capture the flag, 1 sandbox
	int capturelimit;
};
void JoinNetOpen();
void JoinNetClose();
void JoinNetClearList();
void JoinNetSearchLAN();
int JoinNetQuery(const char *host,int port=0);
//webhost: the game is hosted in a browser (WebRTC only, lobby "host":"web")
int JoinNetQueryP2P(const char *address,int port,const char *identity,int webhost=0);
void JoinNetRequestInfo(const char *address,int port);
int JoinNetFind(const char *address,int port);
int JoinNetConnect(int index,char *reason,int reasonsize);
int JoinGameCount();
JoinGame *JoinGameAt(int i);
void JoinNetDebugAdd(const char *name,const char *addr,const char *version,int protocol,int ping,int players,int mode);
//join screen (src/ui/joinscreen.cpp)
void OpenJoinScreen();
int IsJoinScreenOpen();
void JoinScreenShowError(const char *text);
void JoinScreenJoinCode(const char *code);
int HandleJoinScreenKey(int sdlkey);
void HandleJoinScreenText(const char *text);
void JoinScreenClick(int clicks);
void DrawJoinScreen();
#define MAXPLAYERS 32
#define WEBHOSTMAXPLAYERS 8   //a server in a browser (host included), see ServerMaxPlayers
int ServerMaxPlayers();       //players this server takes, itself included
//what the join screen and the lobby list: without a dedicated server's own slot
int ServerListedPlayers();
int ServerListedMaxPlayers();
void SockLoop();
void CloseSocks();
void InitSocks();
void StartServer();
void Disconnect();
int IsServer();
int IsConnected();
int GetNumPlayers();         //one past the last player's number: free slots below it are possible
int PlayerSlotUsed(int i);   //0: nobody has number i (a player left it, M11 phase 3b)
void SendLandInfo(int num);
void ApplyLand(const LandDefs *defs,const SkySettings *sky);   //builds this land, clears the blocks
void SendSimple(char num,char type,DWORD msg,DWORD msg2);
void ReceiveBlockBuffer(char *buf,int pnum,int len);
void SendBlockBuffer();
void AddBlockBuffer(char x,char y,char z,char type,char team,char top,char side,char bottom,char del);
void StartBlockBuffer();
void SendChatMessage(char *text,int dest,int player=-1);
int TryFireGun(int num,Vector pos,Vector dir,char reload);
void HavePain(int sound,int player=-1);
void Killed(int killer);
void ResetPlayer(int num);
void SendServerVars(int num);
void ChangeTeam(int player,int team);
void SendUpdateServerObject(int num);
//smoke grenades (smoke.h): the client's throw to the server; server: every client
//in the game understands them
void SendThrowSmoke(const Vector &pos,const Vector &dir,int objid);
int ServerSmokeAllowed();
void CreateInstallation(Vector pos,char team);
void ClaimObject(int pnum);
void ScoreReset();
void DeletePlayer(int num,int reason);
void GiveCredit(int player, int credit, int number);
void SendRGD(int player);
void SendVoiceFrame(int seq,const unsigned char *data,int len);
//server anti-cheat (M11): the hits it sees on remote players (TickObjects)
void AcBulletTick(Object *o,Vector prev);
int ServerDecidesHits();   //1: don't count the local player's damage (M11 phase 3a)
void AcGrenade(Object *o);
//movement from input commands (docs/M16.md, phases 2 and 3), called by Play() each tick.
//Server: player i is moved by its commands (every client once in the game);
//NetServerMove runs this tick's (ev: the last one's), returns how many.
//Client: the server moves us and we predict (NETSYNCMODE); NetPredictTick applies
//this tick's command to the local player and sends it.
struct MoveCmd;
struct MoveEvents;
int NetServerMoves(int i);
int NetServerMove(int i,float speedpenalty,MoveEvents *ev);
int NetPredicting();
void NetPredictTick(const MoveCmd &c,float speedpenalty,MoveEvents *ev);
//phase 4: the other players in SNAPSHOT. Server: NetTickEnd at the end of each tick
//sends them (30 Hz). Client (NetSnapshotView: the server said so): NetSnapshotPlayers,
//each tick, puts every other player where the snapshots have it SNAPINTERP ticks ago
//(netsync.h); NetSnapshotDrawPos, each frame, where it is drawn (0: no data, hidden).
//NetPlayerShown: player i has data to be drawn (on any side: not before its first
//position, Player::nopos)
void NetTickEnd();
int NetSnapshotView();
void NetSnapshotPlayers();
int NetSnapshotDrawPos(int i,Vector *pos);
int NetPlayerShown(int i);
//host: where it draws player i that it moves from its commands, on a steady clock of
//them AUTHDRAWDELAY ticks back (the queue runs them by bursts with the jitter); 0: as
//the ticks have it
int NetServerDrawPos(int i,Vector *pos);
//phase 5: client, in the game the server rewinds our shots to the tick we drew and
//sends TICKEVENT (NetTickEventsOn); NetNextObjId numbers a grenade we draw before the
//server's comes; NetTickEvents, each tick, applies the events whose tick we now draw
int NetTickEventsOn();
int NetNextObjId();
void NetTickEvents();
//playing.cpp: the server's grenade of owner numbered objid, thrown from pos with vel,
//going off in fusems (TE_GRENADE: ours takes its place, someone else's is made);
//where it went off (TE_EXPLODE)
void GrenadeFromServer(int owner,int objid,Vector pos,Vector vel,int fusems);
void GrenadeExplodes(int owner,int objid,Vector pos);

#define CMSG_BROADCAST 0
#define CMSG_TEAM 1
#define CMSG_OBJECTIVE 2
#define CMSG_SCORE 3
#define CMSG_INFO 4

#define TARGTEAM 64

struct NetSettings
{
	WORD infosendport;
	WORD inforecvport;
	WORD datasendport;
	WORD datarecvport;
	char servername[100];
	char playername[32];
	DWORD maxplayers;
};

//multiplay.cpp
extern int server;          //1: this game is the host
extern int numplayers;      //see GetNumPlayers
extern NetSettings netsettings;
extern RemoteGameData rgd;
extern char curobjective[1000];   //HUD lines the server sends (CMSG_OBJECTIVE, CMSG_SCORE)
extern char curscore[1000];
extern int sv_props;        //server: the rocks and trees block (sv.ini props, docs/PROPS.md)

#endif
