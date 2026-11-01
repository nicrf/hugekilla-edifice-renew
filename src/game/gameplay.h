
#ifndef GAMEPLAY_
#define GAMEPLAY_

#include "win32compat.h"   //DWORD

#define DOME 1
#define WATERPUMP 2
#define FLAG 3

#define GSOUNDS 600

#define STOLENFLAG GSOUNDS+0
#define RETURNFLAG GSOUNDS+1
#define CAPTUREDFLAG GSOUNDS+2
#define REDTEAMWINS GSOUNDS+3
#define BLUETEAMWINS GSOUNDS+4

void GPlayerEnterGame(int num);

int GPlayerChangeTeam(int num,int team);

void GTick();
void GPlayerLeave(int num);
void GPlayerRenumber(int left);   //server: the players after left moved down one
void GReset();
void GRestartPlayers();
int GCanClaimObject(int pnum,int sonum);
int GClaimObject(int pnum,int sonum);
int GDropObject(int pnum,int sonum);
int GetTeamSize(int teamnum,int living=1);

struct Team
{
	int score;
	int canbuild;
};

struct GameData
{
	DWORD basegivetime;
	DWORD gamestarttime;
	DWORD gameendtime;
	int gameon;
};

extern GameData gd;   //GamePlay.cpp

#endif
