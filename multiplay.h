#include "land.h"
#include "vector.h"
#include "playing.h"
#include "winsock2.h"

LRESULT CALLBACK Multiplay(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
#define MAXPLAYERS 32
void SockLoop();
void CloseSocks();
void InitSocks();
void StartServer();
void Disconnect();
int IsServer();
int IsConnected();
int GetNumPlayers();
void SendLandInfo(int num);
void SendSimple(char num,char type,DWORD msg,DWORD msg2);
void SendPosition(char srcnum,char destnum);
void ReceiveBlockBuffer(char *buf,int pnum);
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




