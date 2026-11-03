//Smoke grenades: the game's side (smoke.h). The server owns them: it flies each
//one (the same physics as a frag grenade, TickObjects), lands it as a cloud and
//removes it SMOKE_LIFETIME seconds later (GTickServerObjects, its timeout); the
//clients that understand them follow its flight and draw the cloud (effects.cpp).

#include "smoke.h"
#include "playing.h"
#include "multiplay.h"
#include "hugekilla.h"
#include "sound.h"
#include "props.h"
#include "models.h"   //projectile: the grenade we draw
#include <string.h>

static int smokemode;      //the local player's grenades: 0 frag, 1 smoke
static int smokeallowed;   //client: the server said it takes them (SMOKEMODE)

int SmokeAllowed()
{
	if(server) return ServerSmokeAllowed();
	return smokeallowed;
}

void SmokeSetAllowed(int allowed)
{
	smokeallowed=allowed!=0;
}

int SmokeModeOn()
{
	return smokemode&&SmokeAllowed();
}

void SmokeToggleMode()
{
	if(!SmokeAllowed())
	{
		smokemode=0;
		ConsoleDisplayText((char *)(server?"Smoke grenades are off: a player's game can't show them":
			"This server has no smoke grenades"));
		return;
	}
	smokemode=!smokemode;
	ConsoleDisplayText((char *)(smokemode?"Grenades: smoke":"Grenades: frag"));
	if(GetLocalPlayer()->weapon==4) GetLocalPlayer()->lastswitchtime=curtime;   //the names' wheel turns
}

void SmokeSetMode(int on)
{
	smokemode=on&&SmokeAllowed();
}

int ThrowSmokeGrenade(Player *cur)
{
	if(curtime<cur->nextfiretime) return 0;
	if(cur->ammoingun[4]<=0) return 0;
	cur->ammoingun[4]--;   //a grenade of the same magazine (the server counts it as one)
	cur->nextfiretime=curtime+sv.grenadetimeout;
	cur->kickvel+=.4;
	cur->lastfiretime=curtime;
	Vector pos=cur->pos;
	if(server) ServerThrowSmoke(GetLocalNum(),pos,cur->dir);
	else
	{
		//M16 phase 5: drawn at once, until the server's takes its place (TE_SMOKE)
		int objid=NetTickEventsOn()?NetNextObjId():0;
		SendThrowSmoke(pos,cur->dir,objid);
		for(int i=0;objid&&i<MAXOBJS;i++)
		{
			if(object[i].type) continue;
			Object *o=&object[i];
			memset(o,0,sizeof(*o));
			o->type=SMOKEPREDICT;
			o->pos=pos-Vector(0,.2f,0);   //as ServerThrowSmoke
			o->vel=cur->dir*(sv.grenadespeed*2);
			o->dir=Vector(0,0,1);
			o->pnum=(char)GetLocalNum();
			o->object=&projectile[2];
			o->objid=(unsigned short)objid;
			o->born=simtick;
			o->start=o->pos;
			//its flight at most, and a little more for the server's answer
			o->timeout=curtime+(int)((SMOKE_MAXFLIGHT+2)*1000);
			break;
		}
	}
	//the frag grenade's throw sound (HandleFireGun: weapon*4+2, the local one -1)
	SetSoundPosition(4*4+1,cur->pos);
	PlaySound(4*4+1,0);
	return 1;
}

int ServerThrowSmoke(int pnum,const Vector &pos,const Vector &dir)
{
	if(!server) return -1;
	int clouds=0,slot=-1;
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		if(sobject[i].type==SMOKECLOUD) clouds++;
		else if(!sobject[i].type&&slot<0) slot=i;
	}
	if(clouds>=SMOKE_MAXCLOUDS||slot<0) return -1;   //the air is full enough
	ServerObject *o=&sobject[slot];
	memset(o,0,sizeof(*o));
	o->type=SMOKECLOUD;
	//where HandleFireGun starts a grenade, at the frag grenade's speed
	o->pos=pos-Vector(0,.2f,0);
	o->vel=dir*(sv.grenadespeed*2);
	float d[3];
	SmokeEncode(d,0,0);
	o->dir=Vector(d[0],d[1],d[2]);
	o->team=GetPlayer(pnum)->team;
	o->attached=-1;
	o->speedpenalty=0;
	//GTickServerObjects removes it at its timeout: the flight, then a cloud's life at most
	o->timeout=curtime+(int)((SMOKE_MAXFLIGHT+SMOKE_LIFETIME)*1000);
	SendUpdateServerObject(slot);
	return slot;
}

//one step of a flight; 1 when it landed
static int Fly(Vector &pos,Vector &vel,float sf)
{
	//in steps no longer than the frag grenade's (TickObjects: |vel|+1 of them)
	int steps=(int)Magnitude(vel)+1;
	for(int k=0;k<steps;k++)
	{
		float p[3]={pos.x,pos.y,pos.z},v[3]={vel.x,vel.y,vel.z};
		Vector before=pos;
		Vector next=pos+vel*(sf/steps);
		int landed=SmokeFly(p,v,sf/steps,sv.gravity,GetHeight(next));   //rock tops included
		pos=Vector(p[0],p[1],p[2]);
		vel=Vector(v[0],v[1],v[2]);
		if(landed) return 1;
		PropsBounce(pos,before,vel);   //off a trunk, as a frag grenade
	}
	return 0;
}

void SmokeTakeOver(int objid,int slot)
{
	for(int i=0;i<MAXOBJS;i++)
	{
		Object *o=&object[i];
		if(o->type!=SMOKEPREDICT||o->pnum!=GetLocalNum()||o->objid!=objid) continue;
		o->type=0;
		if(slot<0||slot>=MAXSERVEROBJS||sobject[slot].type!=SMOKECLOUD) return;
		float d[3]={sobject[slot].dir.x,sobject[slot].dir.y,sobject[slot].dir.z};
		if(SmokeLanded(d)) return;   //the server's has landed already: it is where it says
		//the server's flies the same flight from the same throw, a round trip later:
		//it goes on from where ours is (it lands where the server says)
		sobject[slot].pos=o->pos;
		sobject[slot].vel=o->vel;
		return;
	}
}

void TickSmoke(float sf)
{
	//M16 phase 5: our own, until the server's takes its place (SmokeTakeOver)
	for(int i=0;i<MAXOBJS;i++)
		if(object[i].type==SMOKEPREDICT&&Magnitude(object[i].vel)>0)
			Fly(object[i].pos,object[i].vel,sf);
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		ServerObject *o=&sobject[i];
		if(o->type!=SMOKECLOUD) continue;
		float d[3]={o->dir.x,o->dir.y,o->dir.z};
		if(SmokeLanded(d)) continue;
		int landed=Fly(o->pos,o->vel,sf);
		if(!server) continue;   //a client follows: the server says where it lands
		if(!landed&&(int)(o->timeout-curtime)>(int)(SMOKE_LIFETIME*1000)) continue;
		//on the ground (or flown too long): a cloud from now on
		o->vel=Vector(0,0,0);
		SmokeEncode(d,1,0);
		o->dir=Vector(d[0],d[1],d[2]);
		o->timeout=curtime+(int)(SMOKE_LIFETIME*1000);
		SendUpdateServerObject(i);
	}
}

void SmokeReceived(int i)
{
	if(server||i<0||i>=MAXSERVEROBJS||sobject[i].type!=SMOKECLOUD) return;
	float d[3]={sobject[i].dir.x,sobject[i].dir.y,sobject[i].dir.z};
	if(!SmokeLanded(d))
	{
		sobject[i].timeout=0;   //flying: the landing comes as another update
		return;
	}
	int left=SmokeRemainingMs(d);
	if(!left)
	{
		sobject[i].type=0;   //nonsense, or already over
		return;
	}
	sobject[i].timeout=curtime+left;
}

float SmokeCloudAge(int i)
{
	if(i<0||i>=MAXSERVEROBJS||sobject[i].type!=SMOKECLOUD) return -1;
	float d[3]={sobject[i].dir.x,sobject[i].dir.y,sobject[i].dir.z};
	if(!SmokeLanded(d)||!sobject[i].timeout) return -1;
	return SMOKE_LIFETIME-(float)(int)(sobject[i].timeout-curtime)/1000;
}
