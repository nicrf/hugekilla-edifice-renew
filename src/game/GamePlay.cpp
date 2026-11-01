#include "StdAfx.h"
#include "multiplay.h"
#include "gameplay.h"
#include "stdio.h"

extern int consolevisible;
extern DWORD curtime;
extern Object object[MAXOBJS];
extern AseObj projectile[20];
extern ServerVars sv;
extern ServerObject sobject[MAXSERVEROBJS];
extern LandDefs ldfs;
extern int numplayers;
extern int server;
extern RemoteGameData rgd;
GameData gd;
Team team[3];


char *teamname(int team)
{
	if(team==0) return "free";
	if(team==1) return "red";
	if(team==2) return "blue";
	return "unknown team";
}


int NextFreeServerObject()
{
	int num=-1;
	for(int i=0;i<MAXSERVEROBJS;i++)
	if(!sobject[i].type)
	{
		num=i;
		ZeroMemory(&sobject[i],sizeof(ServerObject));
		break;
	}
	return num;

}

int GetTeamSize(int teamnum,int living)
{
	int teamsize=0;
	for(int i=0;i<numplayers;i++)
	{
		if(GetPlayer(i)->playstate>=10||!living)
		if(teamnum==-1||GetPlayer(i)->team==teamnum) 
			teamsize++;
	}
	return teamsize;
}

int GetRandomPlayer(int teamnum)
{
	int playernum=0;
	playernum=GetTeamSize(teamnum)*RND;
	for(int i=0;i<numplayers;i++)
	{
		if(GetPlayer(i)->playstate>=10)
		if(teamnum==-1||GetPlayer(i)->team==teamnum) 
			playernum--;
		if(playernum<0) return i;
	}
	return -1;  //no players
	
}

void GPlayerEnterGame(int num)
{
#ifdef HKDS
	if(num==0)
		return;
#endif
	if(sv.gamemode==0)
	{
		int t1size=GetTeamSize(1,0);
		int t2size=GetTeamSize(2,0);
		if(t1size>t2size) 
			ChangeTeam(num,2);
		else
			ChangeTeam(num,1);
		GetPlayer(num)->playstate=11;
		GetPlayer(num)->respawntime=1;
	}
}

int GPlayerChangeTeam(int num,int team)
{
	if(sv.gamemode==0)
	{
		int t1size=GetTeamSize(1,0);
		int t2size=GetTeamSize(2,0);
		if(t1size>=t2size&&team==2)
			return 2;
		if(t2size>=t1size&&team==1)
			return 1;
	}
	return -1;
}

void GTickServerObjects()
{
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		if(sobject[i].type)
		{
			if(sobject[i].timeout&&curtime>sobject[i].timeout)
			{
				sobject[i].type=0;
				SendUpdateServerObject(i);
			}
		}
	}
}

void GPlayerLeave(int num)
{
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		if(sobject[i].type&&sobject[i].attached==num)
		{
			sobject[i].attached=-1;
			SendUpdateServerObject(i);
		}
	}

}


void GTick()
{
	if(sv.gamemode==0)
	{
		int t1size=GetTeamSize(1);
		int t2size=GetTeamSize(2);
		if(t1size&&t2size)//both teams have players
		{
			if(gd.basegivetime==0)
			{
				rgd.gamestate=1;
				SendRGD(-1);
		
				gd.basegivetime=curtime+20000;
				gd.gamestarttime=curtime;
				gd.gameon=1;
				SendChatMessage("Group with your team\rand find a good location",CMSG_OBJECTIVE,-1);

				GiveCredit(-1,0,4);
				GiveCredit(-1,1,1);
			}
		}
		else
		{
			if(gd.basegivetime!=1) gd.basegivetime=0;
		}
		if(gd.basegivetime>1&&curtime>gd.basegivetime)
		{
			gd.basegivetime=1;
			for(int team=1;team<=2;team++)
			{
				ServerObject *cur=0;
				int i=NextFreeServerObject();
				cur=&sobject[i];
				if(i>=0)
				{
					cur->type=WATERPUMP;
					cur->dir=Vector(1,0,0);
					cur->team=team;
					cur->vel=Vector(0,0,0);
					cur->attached=GetRandomPlayer(team);
					cur->speedpenalty=1;
					SendUpdateServerObject(i);
					SendChatMessage("Help your team place its station",CMSG_OBJECTIVE,TARGTEAM+team);
					SendChatMessage("Place this station",CMSG_OBJECTIVE,cur->attached);
				}
			}
		}
		if(gd.gameon)
		if((gd.gamestarttime&&curtime>gd.gamestarttime+sv.timelimit*1000*60)||(sv.capturelimit&&(team[1].score>=sv.capturelimit||team[2].score>=sv.capturelimit)))
		{
			char get[1000];
			if(team[1].score>team[2].score)
			{
				SendChatMessage("GAME OVER. RED TEAM WINS!",CMSG_OBJECTIVE,-1);
				HavePain(REDTEAMWINS);
			}
			else if(team[2].score>team[1].score)
			{
				SendChatMessage("GAME OVER. BLUE TEAM WINS!",CMSG_OBJECTIVE,-1);
				HavePain(BLUETEAMWINS);
			}
			else
				SendChatMessage("GAME OVER. TIED GAME",CMSG_OBJECTIVE,-1);
			rgd.gamestate=2;
			SendRGD(-1);
			
			gd.gameon=0;
			gd.gameendtime=curtime;
		}
		if(gd.gameendtime&&curtime>gd.gameendtime+30000)
		{
			if(!ReloadServerMap())   //a server started with a saved map plays it again
			{
				LoadSky();
				LightLand(GetSkySettings()->sunheight);
				RandomLand(ldfs);
				SendLandInfo(0);
				CreateLand(ldfs);
			}
			//GReset();
			GRestartPlayers();
			
			
		}
	}
	GTickServerObjects();	
}

//everyone plays again on a new land (the server sent it with SendLandInfo):
//clients wait for it, the host is in at once

void GRestartPlayers()
{
	for(int i=0;i<numplayers;i++)
	{
		GetPlayer(i)->playstate=1;
		GetPlayer(i)->team=0;
	}
#ifndef HKDS
	GetLocalPlayer()->playstate=10;
	GPlayerEnterGame(0);
#endif
}

void GReset()
{
	ZeroMemory(&rgd,sizeof(rgd));
	ZeroMemory(&gd,sizeof(gd));
	ZeroMemory(&team,sizeof(team));
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		if(sobject[i].type)
		{
			sobject[i].type=0;
			SendUpdateServerObject(i);
		}
	}	
	SendChatMessage("",CMSG_OBJECTIVE);
	SendChatMessage("",CMSG_SCORE);
	SendChatMessage("Game reset",CMSG_INFO);
	GiveCredit(-1,-1,0);
	ScoreReset();
	SendRGD(-1);
}

int GCanClaimObject(int pnum,int sonum)
{
	ServerObject *to=&sobject[sonum];
	if(to->type==DOME)
	{
		return 1;
	}
	if(to->type==WATERPUMP)
	{
		return 1;
		//return 0;
	}
		
	return 1;
}


int CloseObject(Vector pos,int type,float dist)
{
	int candidate=-1;
	float canddist=dist;
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		if(type==-1||sobject[i].type==type)
		{
			if(Magnitude(sobject[i].pos-pos)<canddist)
			{
				canddist=Magnitude(sobject[i].pos-pos);
				candidate=i;
			}
		}
	}
	return candidate;
}
	
void GUpdateScores()
{
	char tscores[1000];
	sprintf(tscores,"Score: red: %i blue: %i",team[1].score,team[2].score);
	SendChatMessage(tscores,CMSG_SCORE);


}


int GDropObject(int pnum,int sonum)
{
	if(sv.gamemode==0)
	{
		ServerObject *to=&sobject[sonum];
		Player *pl=GetPlayer(pnum);
		
		if(to->type==FLAG)
		{
			int co=CloseObject(to->pos,WATERPUMP,4);
			if(co!=-1&&pl->team==sobject[co].team&&to->team!=sobject[co].team)
			{
				team[pl->team].score++;
				to->type=0;
				char capmsg[1000];
				sprintf(capmsg,"%s captured the %s flag",pl->name,teamname(to->team));
				SendChatMessage(capmsg,CMSG_INFO);
				GUpdateScores();
				HavePain(CAPTUREDFLAG,pl->team+TARGTEAM);
				GiveCredit(pnum,0,4);
				GiveCredit(pnum,1,1);
			}
				
		}
		if(to->type==WATERPUMP)
		{
			SendChatMessage("Find and return the enemy flag",CMSG_OBJECTIVE,pl->team+TARGTEAM);

		}
	}
	return 1;
}

int GClaimObject(int pnum,int sonum)
{
	if(sv.gamemode==0)
	{
		int pickup=0,alterteam=0,update=0;
		ServerObject *to=&sobject[sonum];
		Player *pl=GetPlayer(pnum);
		switch (to->type)
		{
		case DOME: pickup=1;alterteam=1; break;
		case WATERPUMP: 
			{
				if(to->team==pl->team) 
				{
					if(pl->credits[1]) 
					{
						GiveCredit(pnum,1,-1);
						pickup=1;
					}
				}
				else
				{
					int newsonum=NextFreeServerObject();
					if(newsonum!=-1)
					{
						ServerObject *cur=&sobject[newsonum];
						cur->type=FLAG;
						cur->attached=pnum;
						cur->speedpenalty=.1;
						cur->team=to->team;
						cur->timeout=curtime+120000;
						SendUpdateServerObject(newsonum);
						HavePain(STOLENFLAG,to->team+TARGTEAM);
						HavePain(RETURNFLAG,pl->team+TARGTEAM);
						char capmsg[1000];
						sprintf(capmsg,"%s picked up the %s flag",pl->name,teamname(to->team));
						SendChatMessage(capmsg,CMSG_INFO);
				
					}
				}
			}
			break;
		case FLAG: pickup=1; break;
		}
		if(pickup) to->attached=pnum;
		if(alterteam) to->team=GetPlayer(pnum)->team;
		update+=pickup+alterteam;
		if(update)SendUpdateServerObject(sonum);
	}

	return 0;
}