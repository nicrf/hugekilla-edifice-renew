#ifndef MULTIPLAYH
#define MULTIPLAYH
#include "land.h"
#include "vector.h"
#include "playing.h"
#include "win32compat.h"   //SOCKADDR_IN, LRESULT, WORD
#include "version.h"

LRESULT CALLBACK Multiplay(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
#ifndef HK_WIN32_NATIVE
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
int JoinNetQueryP2P(const char *address,int port,const char *identity);
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
#endif
#define MAXPLAYERS 32
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
void SendPosition(char srcnum,char destnum);
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

#endif
