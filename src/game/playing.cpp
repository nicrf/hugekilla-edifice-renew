//functions to do all the things that have to do with gameplay


#include "platform.h"
#include "cfgfile.h"
#include "models.h"
#include "gameplay.h"
#include "hugekilla.h"
#include "ui.h"
#include "vector.h"
#include "playing.h"
#include "land.h"
#include "math.h"
#include "multiplay.h"
#include "sound.h"
#include "voice.h"
#include "hkkeys.h"
#include "hktime.h"
#include "effects.h"
#include "smoke.h"
#include "gamepad.h"
#include "touch.h"
#include "props.h"
#include "movement.h"
#include "predict.h"   //PREDICTSNAP
#include <chrono>


char menutext[5000];
char menu;

char talktext[1000];

Object object[MAXOBJS];
ServerObject sobject[MAXSERVEROBJS];

PhysicsVars physics;
ServerVars sv;



Player player[MAXPLAYERS];
int localnum;

Controls controls;


DWORD lasttime;
DWORD curtime;
float sf;

//a gamepad's stick (SetPadMove): how far it is pushed, in any direction, instead of
//the keys' all or nothing; the key flags still go on the wire
static struct
{
	int active;
	float fwd,strafe;
} padmove;

void SetPadMove(int active,float fwd,float strafe,int up,int down,int left,int right)
{
	Player *me=GetLocalPlayer();
	if(active)
	{
		me->up=up;
		me->down=down;
		me->lstrafe=left;
		me->rstrafe=right;
	}
	else if(padmove.active)
	{
		//let go: what the keyboard holds
		me->up=HKKeyDown(controls.forward);
		me->down=HKKeyDown(controls.back);
		me->lstrafe=HKKeyDown(controls.lstrafe);
		me->rstrafe=HKKeyDown(controls.rstrafe);
	}
	padmove.active=active;
	padmove.fwd=fwd;
	padmove.strafe=strafe;
}
	

//returns maximum capacity for this gun  (should have used an array)

int GetAmmoHold(int num)
{
	switch (num)
	{
	case 0:
		return sv.gatammohold;
	case 1:
		return sv.m16ammohold;
	case 2:
		return sv.steyrammohold;
	case 3:
		return sv.umpammohold;
	case 4:
		return sv.grenadeammohold;
	case 5:
		return sv.berettaammohold;
	case 6:
		return sv.sr1000ammohold;
	}
	return 0;   //not a gun: fell off the end (undefined) in the original
}

int GetReloadTime(int num)
{
	switch (num)
	{
	case 0:
		return sv.gatreloadtime;
	case 1:
		return sv.m16reloadtime;
	case 2:
		return sv.steyrreloadtime;
	case 3:
		return sv.umpreloadtime;
	case 4:
		return sv.grenadereloadtime;
	case 5:
		return sv.berettareloadtime;
	case 6:
		return sv.sr1000reloadtime;
	}
	return 0;   //not a gun: fell off the end (undefined) in the original
}


void SetLocalNum(int num)
{
	localnum=num;
}
int GetLocalNum()
{
	return localnum;
}
Player *GetLocalPlayer()
{
	return &player[localnum];
}
Player *GetPlayer(int num)
{
	return &player[num];
}

//this function is called while the player is firing to shoot bullets etc.

void FireGun(Player *cur)
{
	//the grenade's second mode: a smoke grenade the server flies (smoke.cpp)
	if(cur->weapon==4&&SmokeModeOn())
	{
		ThrowSmokeGrenade(cur);
		return;
	}
	if(curtime>=cur->nextfiretime)
	if(TryFireGun(GetLocalNum(),cur->pos,cur->dir,0))
	{
		for(int i=0;i<MAXOBJS;i++)
		{
			if(object[i].type==0)
			{
				object[i].type=2;
				object[i].pos=cur->pos+cur->dir*.4-Vector(0,.3,0)+CrossProduct(Vector(0,1,0),cur->dir)*-.2+cur->dir*.3;
				object[i].vel=Vector(0,.05,0)+Normalize(CrossProduct(Vector(0,1,0),cur->dir))*.1+cur->vel+Vector(RND,RND,RND)*.02;
				object[i].dir=cur->dir;
				object[i].pnum=GetLocalNum();
				object[i].timeout=curtime+2000;
				object[i].object=&projectile[1];
				break;
			}
		}
		switch(cur->weapon)
		{
		case 0:
			{
				cur->kickvel+=.16;
				cur->nextfiretime=curtime+sv.gattimeout;
				cur->gunupvel=-.08;
				cur->gunupvel/=powf(1.2,sf);	
				
			}
			break;
		
		case 1:
			{
				cur->kickvel+=.1;
				cur->nextfiretime=curtime+sv.m16timeout;
			}
			break;
		case 2:
			{
				cur->nextfiretime=curtime+sv.steyrtimeout;
				cur->kickvel+=.3;
			}
			break;
		case 3:
			{
				cur->nextfiretime=curtime+sv.umptimeout;
				cur->kickvel+=.2;
			}
			break;
		case 4:
			{
				cur->nextfiretime=curtime+sv.grenadetimeout;
				cur->kickvel+=.4;
			}
			break;
		case 5:
			{
				cur->nextfiretime=curtime+sv.berettatimeout;
				cur->kickvel+=.2;
			}
			break;
		case 6:
			{
				cur->nextfiretime=curtime+sv.sr1000timeout;
				cur->kickvel+=.3;
			}
			break;
		
		}
		cur->lastfiretime=curtime;
	}
}

//called all the time to do standard things (rotate the barrel etc.)

void TickGun(Player *cur)
{
	cur->gunup=Normalize(cur->gunup);
	if(Magnitude(cur->gunmainup)==0)
	{
		cur->gunmainup=Vector(0,1,0);
	}
	
	switch(cur->weapon)
	{
	case 0:
		{
			if(curtime>cur->lastfiretime+500) 
			{
				if(cur->gunup.y>0&&cur->gunup.z<.1)
				{
					cur->gunupvel-=cur->gunup.z/36*sf;cur->gunupvel/=powf(1.7,sf);
				}
			}
			else
			if(curtime>cur->lastfiretime+300) cur->gunupvel/=powf(1.01,sf);	

			cur->kickvel-=cur->gunkick/2*sf;
			cur->kickvel/=powf(1.2,sf);
			cur->gunkick+=cur->kickvel*sf;
			cur->kickvel-=.09*sf;
		}
		break;
	
	case 1:
		{
			cur->kickvel-=cur->gunkick/30*sf;
			cur->kickvel/=powf(1.4,sf);
			cur->gunkick+=cur->kickvel*sf;
			cur->kickvel-=.2*sf;
		}
		break;
	case 2:
		{
			cur->kickvel-=cur->gunkick/10*sf;
			cur->kickvel/=powf(1.4,sf);
			cur->gunkick+=cur->kickvel*sf;
			cur->kickvel-=.006*sf;
		}
		break;
	case 3:
		{
			cur->kickvel-=cur->gunkick/2*sf;
			cur->kickvel/=powf(1.7,sf);
			cur->gunkick+=cur->kickvel*sf;
			cur->kickvel-=.004*sf;
			if(RND*50<1) 
				cur->gunmainupforce=RND*.002-.001;
			cur->gunmainupforce/=powf(1.1,sf);
			cur->gunmainupvel/=powf(1.1,sf);
			cur->gunmainupvel+=cur->gunmainupforce;
			cur->gunmainup+=Normalize(CrossProduct(Vector(1,0,0),cur->gunmainup))*cur->gunmainupvel;
			cur->gunmainup=Normalize(cur->gunmainup);	
			
		}
		break;
	case 4:
		{
			cur->kickvel-=cur->gunkick/30*sf;
			cur->kickvel/=powf(1.6,sf);
			cur->gunkick+=cur->kickvel*sf;
			cur->kickvel+=.0001*sf;
			if(RND*50<1) 
				cur->gunmainupforce=RND*.002-.001;
			cur->gunmainupforce/=powf(1.1,sf);
			cur->gunmainupvel/=powf(1.1,sf);
			cur->gunmainupvel+=cur->gunmainupforce;
			cur->gunmainup+=Normalize(CrossProduct(Vector(1,0,0),cur->gunmainup))*cur->gunmainupvel;
			cur->gunmainup=Normalize(cur->gunmainup);	
		}
		break;
	case 5:
		{
			cur->kickvel-=cur->gunkick/3*sf;
			cur->kickvel/=powf(1.4,sf);
			cur->gunkick+=cur->kickvel*sf;
			cur->kickvel-=.04*sf;
		}
		break;
	case 6:
		{
			cur->kickvel-=cur->gunkick/10*sf;
			cur->kickvel/=powf(1.4,sf);
			cur->gunkick+=cur->kickvel*sf;
			cur->kickvel-=.006*sf;
		}
		break;
	
	}
	cur->gunmainup=(cur->gunmainup*5+Vector(0,1,0))/6;
	cur->gunup+=Normalize(CrossProduct(Vector(1,0,0),cur->gunup))*cur->gunupvel;
	cur->gunup=Normalize(cur->gunup);	
}


//returns the height of the land and blocks at a certain place

float GetHeight(Vector pos)
{
	float height=GetLandSmooth(pos.x,pos.z);
	Vector blockpos=pos;
	blockpos/=BLOCKSIZE;
	
	for(int i=0;i<3;i++)
	{
		if(GetBlock(blockpos.x,blockpos.y,blockpos.z)->type&&pos.y<=BLOCKSIZE*BLOCKGRIDHEIGHT)
		{
			float newheight;
			switch (GetBlock(blockpos.x,blockpos.y,blockpos.z)->type)
			{
			case 1:
				newheight=(int)blockpos.y*BLOCKSIZE+BLOCKSIZE;
				break;
			case 2:
				newheight=(int)blockpos.y*BLOCKSIZE+(1-(blockpos.x-(int)blockpos.x))*BLOCKSIZE;
				break;
			case 3:
				newheight=(int)blockpos.y*BLOCKSIZE+((blockpos.z-(int)blockpos.z))*BLOCKSIZE;
				break;
			case 4:
				newheight=(int)blockpos.y*BLOCKSIZE+((blockpos.x-(int)blockpos.x))*BLOCKSIZE;
				break;
			case 5:
				newheight=(int)blockpos.y*BLOCKSIZE+(1-(blockpos.z-(int)blockpos.z))*BLOCKSIZE;
				break;
			}
			if(height<newheight) height=newheight;
		}
		blockpos.y-=1;
	}
	//the rocks are ground too (props.h): walked onto, bullets and grenades stop there
	float rock=PropsHeight(pos);
	if(height<rock) height=rock;
	return height;
}

//a step of mysf of grenade o in flight: off the trunks, into the water, down to
//the ground where it stays (effects: the splash and the sound of it landing)
static void GrenadeFly(Object *o,float mysf,int effects)
{
	float prevy=o->pos.y;
	Vector before=o->pos;
	o->pos+=o->vel*mysf;
	PropsBounce(o->pos,before,o->vel);   //a grenade off a trunk
	//into the water (M13.5): a splash where it went in
	if(effects&&fxprefs.impacts&&prevy>=WATERHEIGHT&&o->pos.y<WATERHEIGHT&&GetLandSmooth(o->pos.x,o->pos.z)<WATERHEIGHT)
		FxWaterSplash(o->pos,2);
	o->vel.y-=sv.gravity*mysf;
	if(o->pos.y<GetHeight(o->pos)) 
	{
		o->vel=Vector(0,0,0);
		if(!o->wasfroze)
		{
			o->wasfroze=1;
			if(effects)
			{
				SetSoundPosition(191,o->pos);
				PlaySound(191,0);
			}
		}
	}
}

//ticks of flight of grenade o at once, as TickObjects would fly them (not going off)
static void GrenadeFastForward(Object *o,int ticks)
{
	if(ticks>MAXTICKSPERFRAME*8) ticks=MAXTICKSPERFRAME*8;   //2 s: a grenade has landed by then
	for(int t=0;t<ticks;t++)
	{
		int numsteps=Magnitude(o->vel)+1;
		for(int k=0;k<numsteps;k++) GrenadeFly(o,sf/numsteps,0);
		MakeClose(&o->pos);
	}
}

//M16 phase 5 (docs/M16.md): the server's grenade (TE_GRENADE). Ours (owner: us):
//the one we drew at once has its number; it flies on from where the server threw it
//if that was elsewhere. Someone else's: made now (we draw its thrower at the
//moment it threw it), and goes off when the server says (TE_EXPLODE).
void GrenadeFromServer(int owner,int objid,Vector pos,Vector vel,int fusems)
{
	if(owner==GetLocalNum())
	{
		for(int i=0;i<MAXOBJS;i++)
		{
			Object *o=&object[i];
			if(o->type!=4||o->pnum!=owner||o->objid!=objid||o->netobj) continue;
			Vector d=pos-o->start;
			d.x=fmodf(d.x+LANDGRIDSIZE*1.5f,LANDGRIDSIZE)-LANDGRIDSIZE/2;   //the world repeats
			d.z=fmodf(d.z+LANDGRIDSIZE*1.5f,LANDGRIDSIZE)-LANDGRIDSIZE/2;
			if(Magnitude(d)>.5f)
			{
				o->pos=pos;
				o->vel=vel;
				o->wasfroze=0;
				MakeClose(&o->pos);
				GrenadeFastForward(o,(int)(simtick-o->born));
			}
			return;
		}
		return;   //gone off already
	}
	int i;
	for(i=0;i<MAXOBJS&&object[i].type;i++) {}
	if(i==MAXOBJS) return;
	Object *o=&object[i];
	memset(o,0,sizeof(*o));
	o->type=4;
	o->pos=pos;
	MakeClose(&o->pos);
	o->vel=vel;
	o->dir=Magnitude(vel)>0?Normalize(vel):Vector(0,0,1);
	o->pnum=(char)owner;
	o->object=&projectile[2];
	o->objid=(unsigned short)objid;
	o->netobj=1;
	o->born=simtick;
	o->start=o->pos;
	o->timeout=curtime+fusems+1000;   //if the server never says, a second late
	int soundnum=4*4+2;   //the throw, as HandleFireGun
	SetSoundPosition(soundnum,GetPlayer(owner)->pos);
	PlaySound(soundnum,0);
}

void GrenadeExplodes(int owner,int objid,Vector pos)
{
	for(int i=0;i<MAXOBJS;i++)
	{
		Object *o=&object[i];
		if(o->type!=4||o->pnum!=owner||o->objid!=objid) continue;
		o->pos=pos;
		MakeClose(&o->pos);
		o->vel=Vector(0,0,0);
		o->timeout=curtime-1;   //off at its next step
		return;
	}
}

//move grenades/bullets/etc, check for collisions with players, and take actions.

void TickObjects(float sf)
{
	float mysf=sf;
	Player *cur=GetLocalPlayer();
	for(int i=0;i<MAXOBJS;i++)
	if(object[i].type)
	{
		
		int numsteps=Magnitude(object[i].vel)+1;
		if(object[i].type==1||object[i].type==3) numsteps=20;
		mysf=sf/numsteps;
		for(int i2=0;i2<numsteps;i2++)
		{
			if(object[i].type==2)
			{
				if(object[i].pos.y<GetHeight(object[i].pos)+.1) 
				{
					object[i].vel.y=fabs(object[i].vel.y)/1.4;
					object[i].vel/=1.2;
				}
				object[i].vel.y-=sv.gravity*mysf;
				Vector before=object[i].pos;
				object[i].pos+=object[i].vel*mysf;
				PropsBounce(object[i].pos,before,object[i].vel);   //off a trunk
				
			}
			if(object[i].type==4)
			{
				GrenadeFly(&object[i],mysf,1);
				if(curtime>object[i].timeout)
				{
					AcGrenade(&object[i]);   //server: remote players hit (M11)
					SetSoundPosition(181,object[i].pos);
					PlaySound(181,0);
				
					int numplayers=GetNumPlayers();
					for(int i2=0;i2<numplayers;i2++)
					{
						Player *cur=GetPlayer(i2);
						Vector cmppos=cur->pos+Vector(0,-1,0);
						float mag=Magnitude(object[i].pos-cmppos);
						if(mag<8)
						{
							//(the server pushes the players it moves: our own comes back pushed)
							if(!(cur==GetLocalPlayer()&&NetPredicting()))
								cur->vel+=Normalize(cmppos-object[i].pos)*(1/(mag+2))*4;
							//the server's grenade does the damage when it decides hits (M11)
							if(cur==GetLocalPlayer()&&cur->life>0&&!ServerDecidesHits())
							{
								cur->life-=(.5/(mag*mag+4))*sv.grenadedamage;
								if(cur->life<=0)
								{
									Killed(object[i].pnum);
									HavePain(150);
								}
							}
						}
					}
					i2=0;
					//the new explosion (M13.5), or the original burst of particles
					if(fxprefs.explosions) FxExplosion(object[i].pos);
					else
					for(int i3=0;i3<200;i3++)
					{
						for(;i2<MAXPARTICLES;i2++)
						{
							if(particle[i2].type==0)
							{
								particle[i2].type=4;
								particle[i2].timeout=curtime+400*RND;
								particle[i2].pos=object[i].pos;
								float vel=.4;
								if(RND<.2) vel+=RND*.2;
								particle[i2].vel=Normalize(Vector(RND-.5,RND-.5,RND-.5))*vel;
								particle[i2].pos+=particle[i2].vel*-2;
								break;
							}
						}
					}
				}				
			}
			if(object[i].type==1||object[i].type==3)
			{
				if(object[i].pos.y<GetHeight(object[i].pos)) 
				{
					i2=0;
					//dust and grains (M13.5), or the original brown specks
					if(fxprefs.impacts)
					{
						Vector b=object[i].pos/BLOCKSIZE;
						int onblock=object[i].pos.y>GetLandSmooth(object[i].pos.x,object[i].pos.z)+.05f&&
							GetBlock((int)b.x,(int)b.y,(int)b.z)->type;
						FxBulletImpact(object[i].pos,Normalize(object[i].vel),onblock);
					}
					else
					for(int i3=0;i3<20;i3++)
					{
						for(;i2<MAXPARTICLES;i2++)
						{
							if(particle[i2].type==0)
							{
								particle[i2].type=6;
								particle[i2].timeout=curtime+100*RND;
								particle[i2].pos=object[i].pos;
								float vel=RND*.1+.1;
								particle[i2].vel=Normalize(Vector(RND-.5,RND-.5,RND-.5))*vel;
								particle[i2].pos+=particle[i2].vel*-1;
								break;
							}
						}
					}
					object[i].type=0;
				}
				
				Vector prev=object[i].pos;
				object[i].pos+=Normalize(object[i].vel)*20*mysf;
				//a rock or a trunk in the way: the bullet ends there, on the server too
				//(AcBulletTick tests only the part of the step before it)
				Vector propat;
				int inprop=PropsSegmentHit(prev,object[i].pos,&propat);
				if(inprop) object[i].pos=propat;
				AcBulletTick(&object[i],prev);   //server: the players it hits (M11)
				FxBulletThroughSmoke(prev,object[i].pos);   //Ultra: the smoke it crosses, pushed aside
				//the water (M13.5): a splash where it goes in, then a wake of bubbles
				//(the bullet itself keeps its speed: what it hits is the game's)
				if(fxprefs.impacts&&object[i].type&&object[i].pos.y<WATERHEIGHT)
				{
					Vector &now=object[i].pos;
					if(prev.y>=WATERHEIGHT&&GetLandSmooth(now.x,now.z)<WATERHEIGHT)
						FxWaterSplash(prev+(now-prev)*((prev.y-WATERHEIGHT)/(prev.y-now.y)),1);
					else
						FxBulletWake(prev,now);
				}
				if((GetPlayer(object[i].pnum)->team!=GetLocalPlayer()->team)||GetPlayer(object[i].pnum)->team==0)
				if(object[i].pnum!=GetLocalNum())
				{
					Vector hdiff;
					float vdiff;
					hdiff=object[i].pos-cur->pos;
					hdiff.y=0;
					vdiff=cur->pos.y-object[i].pos.y;
					if(Magnitude(hdiff)<.5&&vdiff<2&&vdiff>0&&cur->life>0)
					{
						//the push stays here and at once; the damage is the server's when
						//it decides hits (M11 phase 3a: SERVERHIT, KILLED). Not on a player the
						//server moves (M16): it doesn't push it, the push would be corrected
						//away. Debug aid (tests/net_hitreg.py): HK_NOPUSH=1, never pushed.
						int counted=!ServerDecidesHits();
						static int nopush=getenv("HK_NOPUSH")!=0;
						float push=NetPredicting()||nopush?0.f:1.f;
						if(object[i].type==1)
						{
							cur->vel+=Normalize(object[i].vel)*.3*push;
							if(counted) cur->life-=(sv.highbulletdamage+RND*sv.highbulletdamage)*Magnitude(object[i].vel);
						}
						if(object[i].type==3)
						{
							cur->vel+=Normalize(object[i].vel)*0.1*push;
							if(counted) cur->life-=(sv.lowbulletdamage+RND*sv.lowbulletdamage)*Magnitude(object[i].vel);
						}
						object[i].type=0;
						if(counted)
						{
							HavePain(150);
							if(cur->life<=0)
							{
								Killed(object[i].pnum);
							}
						}
					}
				}
				if(inprop&&object[i].type)
				{
					if(fxprefs.impacts) FxBulletImpact(object[i].pos,Normalize(object[i].vel),1);
					object[i].type=0;
				}
				
			}
			if(object[i].type)
			{
				MakeClose(&object[i].pos);
				if(curtime>object[i].timeout)  //why?
				{
					object[i].type=0;
				}
			}
		}
	}
}


void Play();


//zoom key: pressing it toggles a x3 zoom right away (eased); holding it and moving
//the mouse clearly up/down sets the zoom precisely, as in the original game
#define QUICKZOOM 12        //fzoom of the quick zoom: 1.1^12 = x3.1
#define ZOOMADJUST 30       //mouse counts moved while held before it adjusts the zoom
static int zoommoved;       //mouse movement since the zoom key went down
static float zoomtarget=-1; //-1: no eased zoom running

//the simulation's fixed tick (docs/M16.md, phase 1): 60 a second whatever the frame
//rate, so every machine simulates the same steps. sf was the frame's time /40 (the
//original game's 25 frames a second), and stays that scale: a tick is sf .41667.
static double tickleft;          //ms of real time not simulated yet
static Vector tickprev[MAXPLAYERS];   //where each player was before the last tick
static float tickalpha;          //how far the frame is between the last two ticks
DWORD simtick;
//phase 3: what the server corrected of the local player's prediction, still drawn
//where it was and melting away (x .85 a tick, ~100 ms), see PlayCorrection
static Vector correction;
#define CORRECTIONDECAY .85f

static void PlayLook(float framesf);

//the local player was moved outside a tick by shift (the server's state and the
//commands since, docs/M16.md phase 3): drawn where it was, then smoothly where it
//is; further than PREDICTSNAP (a respawn, a teleport) at once
void PlayCorrection(Vector shift)
{
	int i=GetLocalNum();
	correction-=shift;
	if(Magnitude(correction)>PREDICTSNAP)
	{
		correction=Vector(0,0,0);
		tickprev[i]=GetPlayer(i)->pos;
		return;
	}
	tickprev[i]+=shift;
}

void PlayCorrectionReset()
{
	correction=Vector(0,0,0);
}

static const MoveWorld moveworld={GetHeight,PropsPushPlayer};

const MoveWorld &GetMoveWorld()
{
	return moveworld;
}

//where a player comes back to life: anywhere, not into a tree or a rock (props.h).
//Debug aid (tests/net_hitreg.py): HK_SPAWNAT="x,z,dx,dz" puts the n-th one asked
//here (the host's own, then those the server chooses for its clients) at x+n*dx,
//z+n*dz, falling from 100
Vector RandomSpawnPos()
{
	static const char *at=getenv("HK_SPAWNAT");
	if(at)
	{
		static int n;
		float x=0,z=0,dx=0,dz=0;
		sscanf(at,"%f,%f,%f,%f",&x,&z,&dx,&dz);
		Vector p(x+n*dx,100,z+n*dz);
		n++;
		return p;
	}
	Vector pos=Vector(LANDGRIDSIZE+RND*LANDGRIDSIZE,100,LANDGRIDSIZE+RND*LANDGRIDSIZE);
	for(int tries=0;tries<20&&!PropsSpawnFree(pos.x,pos.z);tries++)
		pos=Vector(LANDGRIDSIZE+RND*LANDGRIDSIZE,100,LANDGRIDSIZE+RND*LANDGRIDSIZE);
	return pos;
}

//called once per frame: as many ticks as the time that passed holds, then the frame
//is drawn between the last two (PlayDrawBegin)

void Playloop()
{
	DWORD targtime;
	static DWORD targlasttime=0;

	targtime=HKPerfMillis();
	//first call (leaving the menu to join a game) or a long pause: start from now.
	//Interpolating from 0 set curtime to 0 for a frame, and a join request answered
	//within that frame got a timeout based on it, then "server timed out" at once.
	if(!targlasttime||(int)(targtime-targlasttime)>2000)
	{
		targlasttime=targtime;
		tickleft=0;
	}
	int dt=(int)(targtime-targlasttime);
	PlayLook((float)dt/40);   //the view follows the mouse every frame, not every tick
	tickleft+=dt;
	//a frame of more than 15 ticks (250 ms): the game slows down rather than run
	//them all at once (a stall, a breakpoint)
	if(tickleft>TICKMS*MAXTICKSPERFRAME) tickleft=TICKMS*MAXTICKSPERFRAME;
	int i;
	while(tickleft>=TICKMS)
	{
		tickleft-=TICKMS;
		simtick++;
		curtime=targtime-(DWORD)tickleft;
		lasttime=curtime;
		sf=(float)(TICKMS/40);
		for(i=0;i<MAXPLAYERS;i++) tickprev[i]=GetPlayer(i)->pos;
		correction*=CORRECTIONDECAY;
		if(Magnitude(correction)<1e-4f) correction=Vector(0,0,0);
		Play();
	}
	tickalpha=(float)(tickleft/TICKMS);
	Player *cur=GetLocalPlayer();
	if(curtime>cur->menudissapeartime&&menu!=BUILDING) 
	{
		menu=0;
		menutext[0]=0;
	}
		
	targlasttime=targtime;
	PropsRefresh();   //which rocks and trees are there (blocks, stations)
	for(i=0;i<MAXSERVEROBJS;i++)
	{
		if(sobject[i].type==1) sobject[i].dir=Normalize(CrossProduct(sobject[i].dir,Vector(0,1,0))*.01+sobject[i].dir);
		if(sobject[i].attached!=(-1))
		{
			sobject[i].pos=GetPlayer(sobject[i].attached)->pos+Vector(0,.3,0);
			sobject[i].dir=GetPlayer(sobject[i].attached)->fwd;
		}
	}
}

//what the dedicated server sleeps between two Playloop (src/platform/dedicated.cpp)
double PlayNextTickMs()
{
	return TICKMS-tickleft;
}

//Debug aid: HK_SYNCLOG=1 prints, each frame, where every player in the game is drawn,
//on the wall clock in microseconds (the same for every process of the machine):
//tests/net_sync.py compares where a player is with where the other game shows it
void SyncLogFrame()
{
	static int on=getenv("HK_SYNCLOG")!=0;
	if(!on) return;
	long long us=(long long)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	for(int i=0;i<GetNumPlayers();i++)
	{
		Player *p=GetPlayer(i);
		if(p->playstate<10) continue;
		fprintf(stderr,"synclog %lld %d %d %d %.3f %.3f %.3f\n",us,GetLocalNum(),i,p->playstate,p->pos.x,p->pos.y,p->pos.z);
	}
}

//HK_SYNCLOG=1, M16 phase 3: each state of our player from the server compared with
//the prediction: its command, the gaps, whether it was corrected (and how many
//commands were played again, how far it moved the player)
void SyncLogPredict(unsigned int cmd,float poserr,float velerr,int corrected,int replayed,float shift)
{
	static int on=getenv("HK_SYNCLOG")!=0;
	if(!on) return;
	long long us=(long long)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	fprintf(stderr,"syncpredict %lld %u %.5f %.5f %d %d %.4f\n",us,cmd,poserr,velerr,corrected,replayed,shift);
}

//the sounds and smoke of a player's move (MovePlayer)
static void PlayerMoveEffects(Player *cur,const MoveEvents &ev)
{
	int soundnum=cur==GetLocalPlayer()?170:171;   //the jetpack
	if(ev.thrust==1)
	{
		Vector targ=ev.thrustdir;
		SetSoundPosition(soundnum,ev.thrustpos);
		for(int i=0;i<MAXPARTICLES;i++)
		{
			if(particle[i].type==0)
			{
				particle[i].type=3;
				particle[i].pos=ev.thrustpos-targ+Vector(RND*2-1,0,RND*2-1)*.1;
				particle[i].vel=targ*-.4+Vector(RND-.5,RND-.5,RND-.5)*.1;
				particle[i].timeout=curtime+3000;
				break;
			}
		}
		if(!cur->wasthrusting)
		{
			PlaySound(soundnum,1);
			cur->wasthrusting=1;
		}
	}
	else if(ev.thrust==0&&cur->wasthrusting)
	{
		StopSound(soundnum);
		cur->wasthrusting=0;
	}
	if(ev.landed&&curtime>cur->nextfootsteps)
	{
		SetSoundPosition(120,ev.landpos+Vector(0,-2,0));
		PlaySound(120,0);
		cur->nextfootsteps=curtime+300;
		SetSoundPosition(130,ev.landpos+Vector(0,-1,0));
		PlaySound(130,0);
	}
	if(ev.walked&&curtime>cur->nextfootsteps)
	{
		SetSoundPosition(100,ev.walkpos+Vector(0,-2,0));
		PlaySound(100,0);
		cur->nextfootsteps=curtime+600;
	}
	if(ev.splash)
	{
		SetSoundPosition(140,ev.splashpos+Vector(0,-1,0));
		PlaySound(140,0);
	}
}

//Where the frame draws each player: between where it was before the last tick and
//where it is now, tickalpha of the way (positions only: the view, the legs, the roll
//are the tick's). Around Redraw; PlayDrawEnd puts the simulation's positions back.
//Not across a jump of 5 units in one tick: an arrival, a respawn, the edge of the
//world, a position received from the network that far. The others from the server's
//snapshots (M16 phase 4) are where the snapshots have them at this frame's moment;
//a player with no position yet isn't drawn (out of the game for the frame).
static Vector drawsaved[MAXPLAYERS];
static char drawstate[MAXPLAYERS];
static int drawing;
#define HIDDENSTATE 9   //playstate while drawn: below 10, not in the game

void PlayDrawBegin()
{
	if(drawing) return;
	drawing=1;
	for(int i=0;i<GetNumPlayers()&&i<MAXPLAYERS;i++)
	{
		Player *p=GetPlayer(i);
		drawsaved[i]=p->pos;
		drawstate[i]=p->playstate;
		int snapview=i!=GetLocalNum()&&NetSnapshotView();
		if(snapview?!NetSnapshotDrawPos(i,&p->pos):!NetPlayerShown(i))
		{
			if(p->playstate>=10) p->playstate=HIDDENSTATE;
			continue;
		}
		Vector d=p->pos-tickprev[i];
		if(snapview||NetServerDrawPos(i,&p->pos)) {}
		else if(Magnitude(d)<5)
			p->pos=tickprev[i]+d*tickalpha;
		if(i==GetLocalNum()) p->pos+=correction;
		//a player the server moves from its commands stays where it is in the world
		//(M16 phase 2): drawn near us, as the others
		else MakeClose(&p->pos);
	}
	for(int i=0;i<MAXSERVEROBJS;i++)   //what players carry, with them
		if(sobject[i].attached!=(-1))
			sobject[i].pos=GetPlayer(sobject[i].attached)->pos+Vector(0,.3,0);
}

void PlayDrawEnd()
{
	if(!drawing) return;
	drawing=0;
	for(int i=0;i<GetNumPlayers()&&i<MAXPLAYERS;i++)
	{
		GetPlayer(i)->pos=drawsaved[i];
		GetPlayer(i)->playstate=drawstate[i];
	}
	for(int i=0;i<MAXSERVEROBJS;i++)
		if(sobject[i].attached!=(-1))
			sobject[i].pos=GetPlayer(sobject[i].attached)->pos+Vector(0,.3,0);
}

//the local player's view, every frame: the mouse (and the pad's right stick, which
//moves it) and the zoom. framesf: the frame's time in the game's scale (ms/40).

static void PlayLook(float framesf)
{
	if(GetLocalPlayer()->playstate>=10)
	{
		Player *cur=GetLocalPlayer();
		float mspeed=.003;
		int mxchange,mychange;
		GetMouseChange(mxchange,mychange);
		if(controls.mousereverse) mychange=-mychange;
		if(cur->zoom)
		{
			//a little mouse jitter while pressing the key must not cancel the quick zoom
			zoommoved+=abs(mychange);
			if(zoommoved<ZOOMADJUST) mychange=0;
			else zoomtarget=-1;
			cur->fzoom-=(float)mychange/10;
			if(cur->fzoom<0)cur->fzoom=0;
			if(cur->fzoom>40)cur->fzoom=40;
			mychange=0;
			mxchange=0; 
		}
		if(zoomtarget>=0)
		{
			float step=.25f*framesf;
			if(step>1) step=1;
			cur->fzoom+=(zoomtarget-cur->fzoom)*step;
			if(fabs(zoomtarget-cur->fzoom)<.05f)
			{
				cur->fzoom=zoomtarget;
				zoomtarget=-1;
			}
		}
		if(IsSpawnScreenOpen()) mxchange=mychange=0;   //no looking around before playing
		mspeed*=controls.mousesensitivity;
		mspeed/=powf(1.05,cur->fzoom);
		cur->fwd-=CrossProduct(Vector(0,1,0),cur->fwd)*mxchange*mspeed;
		cur->tilt-=CrossProduct(Vector(0,0,1),cur->tilt)*mychange*mspeed;
		if(cur->tilt.x<.01)cur->tilt.x=.01;
	}
}

//Debug aid (tests/net_hitreg.py): HK_AIMBOT="<player>[,fire]" turns the local player
//to where it draws that player each tick (its chest, a unit under its head) before
//its shot; with fire it holds the trigger, reloads when empty, and so comes back to
//life too. What a player that aims well does with what it sees.
static void AimBot(Player *cur)
{
	static const char *spec=getenv("HK_AIMBOT");
	if(!spec) return;
	int target=atoi(spec);
	int fire=strstr(spec,",fire")!=0;
	if(fire)
	{
		cur->firing=1;
		if(cur->playstate==10&&cur->ammoingun[cur->weapon]<=0) HandleKey(controls.reload,0);
	}
	if(target==GetLocalNum()||target<0||target>=GetNumPlayers()||!PlayerSlotUsed(target)) return;
	Player *t=GetPlayer(target);
	Vector at=t->pos;
	if(NetSnapshotView()&&!NetSnapshotDrawPos(target,&at)) return;   //not drawn
	if(t->playstate!=10) return;
	MakeClose(&at);
	Vector d=at-Vector(0,1,0)-cur->pos;
	float h=sqrtf(d.x*d.x+d.z*d.z);
	if(h<.01f) return;
	cur->fwd=Vector(d.x/h,0,d.z/h);
	float pitch=atan2f(d.y,h);
	cur->tilt=Vector(cosf(pitch),sinf(pitch),0);
	AimPlayer(cur);
}

//handles all physics, movement, etc.: one tick (sf)

void Play()
{
	int numplayers=GetNumPlayers();
	if(GetLocalPlayer()->playstate>=10)
	{
		Player *cur=GetLocalPlayer();
		if(cur->gundowntime&&curtime>cur->nextfiretime)
		{
			cur->gundowntime=0;
		}
		AimBot(cur);
		if(cur->firing&&cur->playstate==10) 
				FireGun(cur);
			
			
			TickGun(cur);
			if(cur->pos.x<LANDGRIDSIZE)cur->pos.x+=LANDGRIDSIZE;
			if(cur->pos.z<LANDGRIDSIZE)cur->pos.z+=LANDGRIDSIZE;
			if(cur->pos.x>LANDGRIDSIZE*2)cur->pos.x-=LANDGRIDSIZE;
			if(cur->pos.z>LANDGRIDSIZE*2)cur->pos.z-=LANDGRIDSIZE;
			SetSoundPosition(0,cur->pos+Vector(20,0,0));
		SetPlayerSoundPosition(cur->pos,cur->dir,cur->vel);	
		if(cur->pos.y>WATERHEIGHT&&cur->pos.y+cur->vel.y<WATERHEIGHT)
		{
			//StopSound(0);
			SetRolloff(1);
		}
		if(cur->pos.y<WATERHEIGHT&&cur->pos.y+cur->vel.y>WATERHEIGHT)
		{
			//PlaySound(0,1);
			SetRolloff(.1);
			//SetSoundVolume(0,0);
		}
		if(cur->life<=0&&!cur->respawntime)
		{
			cur->playstate=11;
			cur->respawntime=curtime+5000;
			HavePain(160);
			cur->life=0;
		}
		if(curtime>cur->respawntime&&cur->respawntime&&cur->firing)  //respawn
		{
			cur->firing=0;
			cur->respawntime=0;
			//a server that moves us (M16 phase 2) chooses its own place: we are put
			//there when its state comes back
			cur->pos=RandomSpawnPos();
			cur->vel=Vector(0,0,0);
			cur->life=100;
			int i;
			for(i=0;i<20;i++)
			{
				cur->ammoingun[i]=GetAmmoHold(i);
				cur->ammoreserves[i]=500;
				cur->owngun[i]=0;
			}
			cur->owngun[0]=0;
			cur->owngun[1]=0;
			cur->owngun[2]=0;
			cur->owngun[3]=1;
			cur->owngun[4]=1;
			cur->owngun[5]=1;
			cur->owngun[6]=0;

			int extragun=RND*4;
			for(i=0;i<7;i++)
			{
				if(cur->owngun[i]==0)
				{
					extragun--;
					if(extragun<0) 
					{
						cur->owngun[i]=1;
						break;
					}
				}
			}

			cur->playstate=10;
			cur->weapon=3;
		}
		
	}
	TickObjects(sf);
	TickSmoke(sf);
	NetSnapshotPlayers();   //the others where the snapshots have them (M16 phase 4)
	NetTickEvents();        //what the server says happened at the tick we draw (phase 5)
	
	for(int i=0;i<numplayers;i++)
	if(GetPlayer(i)->playstate>=10)
	{
		float speedpenalty=GetSOSpeedPenalty(i);
		

		Player *cur=GetPlayer(i);
		int local=cur==GetLocalPlayer();
		if(!local&&NetSnapshotView())
		{
			//phase 4: the server's, in the past (NetSnapshotPlayers); a step of a copy
			//only for the legs, the footsteps and the jetpack's smoke
			if(!NetPlayerShown(i)) continue;
			AimPlayer(cur);
			Player look=*cur;
			look.thrustfuel=1;
			MoveInput in;
			in.speedpenalty=speedpenalty;
			MoveEvents ev;
			MovePlayer(&look,in,sf,sv,moveworld,&ev);
			cur->runtime=look.runtime;
			cur->legframe=look.legframe;
			PlayerMoveEffects(cur,ev);
			continue;
		}
		//a client moves no one else (it isn't in the game yet: no snapshot)
		if(!local&&!IsServer()) continue;
		//the server moves each client's player from its commands (docs/M16.md, phase
		//2): where they put it in the world, not moved near us (PlayDrawBegin is);
		//the others here are its dummy players (console createplayer)
		int bycmd=!local&&NetServerMoves(i);
		if(!local&&!bycmd)
			MakeClose(&cur->pos);
		AimPlayer(cur);
		
		if(cur!=GetLocalPlayer()&&cur->timeout>curtime+100000&&IsServer())
		{
			if(RND*700<2) cur->jetpack=1-cur->jetpack;
			if(RND*200<2) cur->up=1-cur->up;
			if(RND*200<2) cur->down=1-cur->down;
			if(RND*200<2) cur->lstrafe=1-cur->rstrafe;
			if(RND*100<2) cur->hangglide=1-cur->hangglide;
			if(RND*1000<2) cur->jump=1-cur->jump;
			if(RND*1000<2) cur->curturn=(RND-.5)*.06;
			if(RND*2000<2) cur->weapon=RND*6;
			if(cur!=GetLocalPlayer())
			{
				cur->fwd=Normalize(cur->fwd+CrossProduct(cur->fwd,Vector(0,1,0))*cur->curturn);
			}
		
		}
		MoveEvents ev;
		if(bycmd)
		{
			if(!NetServerMove(i,speedpenalty,&ev)) continue;   //no command this tick
		}
		else if(local&&NetPredicting())
		{
			//phase 3: this tick's command, applied at once and sent to the server
			MoveCmd c=MakeMoveCmd(cur,padmove.active,padmove.fwd,padmove.strafe,simtick);
			NetPredictTick(c,speedpenalty,&ev);
		}
		else
		{
			MoveInput in;
			in.local=local;
			in.pad=in.local&&padmove.active;
			in.padfwd=padmove.fwd;
			in.padstrafe=padmove.strafe;
			in.speedpenalty=speedpenalty;
			MovePlayer(cur,in,sf,sv,moveworld,&ev);
		}
		PlayerMoveEffects(cur,ev);
	}	
	NetTickEnd();   //the server's snapshots of this tick (M16 phase 4)
}

//not in use right now, but was used to buy guns

void BuyGun(int gun,int pnum)
{
	int guncost=0;
	switch (gun)
	{
	case 0: guncost=600; break;
	case 1: guncost=140; break;
	case 2: guncost=320; break;
	case 3: guncost=250; break;
	case 4: guncost=30; break;
	case 5: guncost=50; break;
	case 6: guncost=400; break;
	default: return;
	}
	if(GetPlayer(pnum)->money<guncost)
	{
		ConsoleDisplayText("Not enough money");
		return;
	}
	GetPlayer(pnum)->money-=guncost;
	GetPlayer(pnum)->owngun[gun]=1;
	if(pnum==GetLocalNum()) SwitchGun(gun);
}


int CanBuild()
{
	if(GetTeamSize(-1,0)==1) // can always build when only 1 player in server
		return 1;
	if(!GetLocalPlayer()->credits[0])
		return 0;
	//return 1;
	if(sv.gamemode==0)
	{
		for(int i=0;i<MAXSERVEROBJS;i++)
		{
			if(sobject[i].type==WATERPUMP&&sobject[i].team==GetLocalPlayer()->team&&sobject[i].attached==-1)
			{
				MakeClose(&sobject[i].pos);
				if(Magnitude(GetLocalPlayer()->pos-sobject[i].pos)<BUILDZONE)
				{
					return 1;
				}
			}
		}
		return 0;
	}
	return 1;

}

//allows for buying and building

int HandleMenuKey(DWORD key)
{
	GetLocalPlayer()->menudissapeartime=curtime+7000;
	if(key==controls.buy&&0)
	{
		strcpy(menutext,"Buy:\r\r1. Handguns\r2. Rifles\r3. Automatics\r4. Accessories");
		menu=BUY;
		return 1;
	}
	if(key==controls.build)
	{
		//if(sv.gamemode==1)
		if(GetTeamSize(-1,0)==1)
			GetLocalPlayer()->credits[0]++;
		else
			if(!CanBuild())
			{
				ConsoleDisplayText("Too far from station to build");
				return 1;
			}
		OpenBuildMenu();   //src/ui/buildmenu.cpp
		return 1;
	}
	if(key=='0')
	{
		menutext[0]=0;
		menu=NOMENU;
		return 1;
	}
	if(key<'0'||key>'9') return 0;
	int number=key-'0';
	switch (menu)
	{
		case BUY:
		{
			switch (number)
			{
			case 1:
				strcpy(menutext,"1. Beretta (50)");
				menu=BUYHANDGUN;
				return 1;
			case 2:
				strcpy(menutext,"1. M16 (140)\r2. Steyr ACR (320)\r3. H&K Sniper (400)");
				menu=BUYRIFLE;
				return 1;
			case 3:
				strcpy(menutext,"1. H&K UMP 45 (250)\r2. Gat (600)");
				menu=BUYAUTOMATIC;
				return 1;
			case 4:
				strcpy(menutext,"1. Grenades [4] (30)");
				menu=BUYACCESSORY;
				return 1;
			default:
				menu=0;
			}
			return 1;
		}
		case BUYHANDGUN:
		{
			switch(number)
			{
			case 1: BuyGun(5,GetLocalNum()); break;
			}
			menu=0;
			return 1;
		}
		case BUYRIFLE:
		{
			switch(number)
			{
			case 1: BuyGun(1,GetLocalNum()); break;
			case 2: BuyGun(2,GetLocalNum()); break;
			case 3: BuyGun(6,GetLocalNum()); break;
			}
			menu=0;
			return 1;
		}
		case BUYAUTOMATIC:
		{
			switch(number)
			{
			case 1: BuyGun(3,GetLocalNum()); break;
			case 2: BuyGun(0,GetLocalNum()); break;
			}
			menu=0;
			return 1;
		}
		case BUYACCESSORY:
		{
			switch(number)
			{
			case 1: BuyGun(4,GetLocalNum()); break;
			}
			menu=0;
			return 1;
		}
		case BUILD:
		{

			char *name=GetStructureFilename(number);
			if(name==0)
				return 1;
			if(name[0]==0)
			{
				DisplayBuildingType(0);
				return 1;
			}
			LoadStructure(name);
			menu=BUILDING;
			BuildPreviewText();
			return 1;
		}
		
		case BUILDING:
		{
			if(!CanBuild()) 
			{
				ConsoleDisplayText("Too far from station to build");
				return 1;
			}
			if(number==1)
			{
				//aimed at the sky or past MAXBUILDDIST, or into a rock or a tree: the
				//preview is red, keep placing
				int placed=PlaceStructure(1);
				if(placed<=0)
				{
					ConsoleDisplayText((char *)(placed<0?"A rock or a tree is in the way":"Too far to build"));
					return 1;
				}
				menutext[0]=0;
				menu=NOMENU;
				return 1;
			}
			if(number==2) 
			{
				menu=NOMENU;
				menutext[0]=0;
				return 1;
			}
			//any other key closes it. This was the fall through into case INFOMENU,
			//which itself never matched: 0x80 doesn't fit the signed char menu
			menu=NOMENU;
			menutext[0]=0;
			return 1;
		}
	}
	return 0;
}

//called for keyboard input while the player is typing a chat message
char* l33tify(char *msg)
{
	int len=strlen(msg);
	
	static char newtext[2000];   //returned: was a local, read after it was gone
	char basel33t=(char)('~'+2);   //the font's extra glyphs after '~' (0x80, 0x81)

	int newtextpos=0;
	for(int i=0;i<len;)
	{
		if(strncmp("w00t",&msg[i],4)==0)
		{
			newtext[newtextpos++]=basel33t+0;
			i+=4;
		}
		else
		if(strncmp(":)",&msg[i],2)==0)
		{
			newtext[newtextpos++]=basel33t+1;
			i+=2;
		}
		else
		{
			newtext[newtextpos++]=msg[i];
			i+=1;
		}
		
	}
	newtext[newtextpos]=0;
	return newtext;


}

int HandleTalkKey(DWORD key,DWORD flags)
{
	if(!GetLocalPlayer()->talking) return 0;
	if(key==27)
	{
		GetLocalPlayer()->talking=0;
		talktext[0]=0;
		return 1;
	}
	if(key==KEY_BACK)
	{
		int len=strlen(talktext);
		if(len>0)
		{
			if(talktext[len-1]=='\r'&&len>1) 
			{
				len--;
			}
			talktext[len-1]=0;
		}
	}
	if(key==KEY_RETURN)
	{
		char text[2000];
		strcpy(text,l33tify(talktext));
	
		if(text[0]) SendChatMessage(text,GetLocalPlayer()->talking-1);
		talktext[0]=0;
		
		GetLocalPlayer()->talking=0;
		return 1;
	}
	if(key==KEY_TAB)   //switch between everyone and team
	{
		GetLocalPlayer()->talking=GetLocalPlayer()->talking==1?2:1;
		return 1;
	}
	//characters arrive as text input (ChatHandleText, keyboard layout aware);
	//the keys are still swallowed so typing doesn't move the player
	if(GetRealKey(key,flags)) return 1;
	return key==KEY_BACK;
}

//converts strings representing keys into their virtual scan codes, like changing alt to KEY_ALT

int TextToKey(char *text)
{
	if(!text) return 0;
	if(strlen(text)==1)
	{
		if(text[0]>='a'&&text[0]<='z')
		{
			return text[0]-32;
		}
		if(text[0]>='A'&&text[0]<='Z')
		{
			return text[0];
		}
		if(text[0]>='0'&&text[0]<='9')
		{
			return text[0];
		}
	}
	
	if(strcasecmp(text,"alt")==0)
	{
		return KEY_MENU;
	}
	if(strcasecmp(text,"ctrl")==0)
	{
		return KEY_CONTROL;
	}
	if(strcasecmp(text,"shift")==0)
	{
		return KEY_SHIFT;
	}
	if(strcasecmp(text,"tab")==0)
	{
		return KEY_TAB;
	}
	if(strcasecmp(text,"space")==0)
	{
		return KEY_SPACE;
	}
	if(strcasecmp(text,"lbutton")==0)
	{
		return KEY_LBUTTON;
	}
	if(strcasecmp(text,"rbutton")==0)
	{
		return KEY_RBUTTON;
	}
	if(strcasecmp(text,"up")==0)
	{
		return KEY_UP;
	}
	if(strcasecmp(text,"down")==0)
	{
		return KEY_DOWN;
	}
	if(strcasecmp(text,"left")==0)
	{
		return KEY_LEFT;
	}
	if(strcasecmp(text,"right")==0)
	{
		return KEY_RIGHT;
	}

	return 0;
}
	
	
//reads a configuration file

void LoadControls()
{
	CfgFile controlini;
	controlini.Load(UserFile("control.ini"));
	controls.mousereverse=controlini.GetValuei("mousereverse");
	controls.mousesensitivity=controlini.GetValuef("mousesensitivity");
	if(controls.mousesensitivity<=0) controls.mousesensitivity=1;
	controls.forward=TextToKey(controlini.GetValues("forward"));
	controls.back=TextToKey(controlini.GetValues("back"));
	controls.lstrafe=TextToKey(controlini.GetValues("lstrafe"));
	controls.rstrafe=TextToKey(controlini.GetValues("rstrafe"));
	controls.build=TextToKey(controlini.GetValues("build"));
	controls.buy=TextToKey(controlini.GetValues("buy"));
	controls.zoom=TextToKey(controlini.GetValues("zoom"));
	controls.fire=TextToKey(controlini.GetValues("fire"));
	controls.showscores=TextToKey(controlini.GetValues("showscores"));
	controls.jump=TextToKey(controlini.GetValues("jump"));
	controls.talk=TextToKey(controlini.GetValues("talk"));
	controls.talkteam=TextToKey(controlini.GetValues("talkteam"));
	controls.reload=TextToKey(controlini.GetValues("reload"));
	controls.secondfire=TextToKey(controlini.GetValues("secondfire"));
	controls.jetpack=TextToKey(controlini.GetValues("jetpack"));
	controls.screenshot=TextToKey(controlini.GetValues("screenshot"));
	controls.claim=TextToKey(controlini.GetValues("claim"));
	controls.duck=TextToKey(controlini.GetValues("duck"));
	controls.hangglide=TextToKey(controlini.GetValues("hangglide"));
	controls.seemoney=TextToKey(controlini.GetValues("seemoney"));
	controls.pushtotalk=TextToKey(controlini.GetValues("pushtotalk"));
	if(!controls.pushtotalk) controls.pushtotalk='T';   //control.ini from before voice chat
	PadLoadConfig();   //pad_* (src/platform/gamepad.cpp)
	TouchLoadConfig();   //touchcontrols, touchsize (src/platform/touch.cpp)
}

//general keyboard input handler

void HandleKey(DWORD key,DWORD flags)
{
	if(key==192) // '`' 
	{
		consolevisible=1-consolevisible;
		return;
	}
	if(consolevisible)
	{
		HandleConsoleKey(key,flags);
		return;
	}
	if(HandleTalkKey(key,flags)) return;
	if(flags&(1<<30)) return;
	if(key==controls.pushtotalk) VoicePushToTalk(1);
	//placing a structure: left click (fire) builds it, right click cancels
	if(menu==BUILDING&&key==controls.fire)
	{
		HandleMenuKey('1');
		return;
	}
	if(menu==BUILDING&&key==KEY_RBUTTON)
	{
		HandleMenuKey('2');
		return;
	}
	if(HandleMenuKey(key)) return;
	int extended;
	if(flags&(1<<24)) extended=1;
	if(key==KEY_F4) 
	{
		consolevisible=1;
		LoadTextures();
		LoadHighTextures();
		consolevisible=0;
	}
	if(key==controls.talk)
	{
		GetLocalPlayer()->talking=1;
		ChatOpened(key);
		return;
	}
	if(key==controls.talkteam)
	{
		GetLocalPlayer()->talking=2;
		ChatOpened(key);
		return;
	}
	if(key==KEY_F1) LoadGuns();
	if(key==KEY_F2) LoadRandom("textures/ground/",TEX_LAND);
	if(key==KEY_F5) LoadControls();
	if(key==controls.forward) GetLocalPlayer()->up=1;
	if(key==controls.back) GetLocalPlayer()->down=1;
	if(key==controls.showscores) GetLocalPlayer()->scores=1;
	if(key==controls.lstrafe) GetLocalPlayer()->lstrafe=1;
	if(key==controls.rstrafe) GetLocalPlayer()->rstrafe=1;
	if(key==controls.fire) GetLocalPlayer()->firing=1;
	if(key==controls.zoom)
	{
		GetLocalPlayer()->zoom=1;
		zoommoved=0;
		float current=zoomtarget>=0?zoomtarget:GetLocalPlayer()->fzoom;
		zoomtarget=current>1?0:QUICKZOOM;
		if(getenv("HK_KEYLOG")) fprintf(stderr,"zoom key: fzoom %.1f -> target %.0f\n",GetLocalPlayer()->fzoom,zoomtarget);
	}
	if(key==controls.jump) GetLocalPlayer()->jump=1;
	if(key==controls.jetpack) GetLocalPlayer()->jetpack=1;
	if(key==controls.hangglide) GetLocalPlayer()->hangglide=1;
	//the grenade's key again: frag or smoke grenades
	if(key=='5'&&GetLocalPlayer()->weapon==4) SmokeToggleMode();
	else if(key>='1'&&key<='7') SwitchGun(key-'1');
	if(key==controls.screenshot) GetLocalPlayer()->takescreenshot=1;
	if(key==controls.seemoney) GetLocalPlayer()->seemoney=1;
	if(key==controls.claim)
	{
		ClaimObject(GetLocalNum());
	}
	if(key==KEY_F3) LoadSounds();
	if(key==controls.reload&&curtime>GetLocalPlayer()->nextfiretime) 
	{
		int transferamt=GetAmmoHold(GetLocalPlayer()->weapon);
		transferamt-=GetLocalPlayer()->ammoingun[GetLocalPlayer()->weapon];
		if(transferamt)
		{
			TryFireGun(GetLocalNum(),GetLocalPlayer()->pos,GetLocalPlayer()->dir,1);
			if(GetLocalPlayer()->ammoreserves[GetLocalPlayer()->weapon]<transferamt)
			{
				transferamt=GetLocalPlayer()->ammoreserves[GetLocalPlayer()->weapon];
			}
			GetLocalPlayer()->gundowntime=curtime;
			GetLocalPlayer()->ammoingun[GetLocalPlayer()->weapon]+=transferamt;
			GetLocalPlayer()->nextfiretime=curtime+GetReloadTime(GetLocalPlayer()->weapon);
		}
	}
}

//general keyboard handler

void HandleKeyUp(DWORD key,DWORD flags)
{
	if(key==controls.lstrafe) GetLocalPlayer()->lstrafe=0;
	if(key==controls.rstrafe) GetLocalPlayer()->rstrafe=0;
	if(key==controls.forward) GetLocalPlayer()->up=0;
	if(key==controls.back) GetLocalPlayer()->down=0;
	if(key==controls.showscores) GetLocalPlayer()->scores=0;
	if(key==controls.fire) GetLocalPlayer()->firing=0;
	if(key==controls.zoom) GetLocalPlayer()->zoom=0;
	if(key==controls.jump) GetLocalPlayer()->jump=0;	
	if(key==controls.jetpack) GetLocalPlayer()->jetpack=0;
	if(key==controls.hangglide) GetLocalPlayer()->hangglide=0;	
	if(key==controls.seemoney) GetLocalPlayer()->seemoney=0;
	if(key==controls.pushtotalk) VoicePushToTalk(0);
}

//called to switch weapons (when the mouse wheel is scrolled etc.)

void SwitchGun(int gun)
{
	Player *cur=GetLocalPlayer();
	int ogun=GetLocalPlayer()->weapon;
	//the wheel stops at the smoke grenade between the grenade and the next weapon
	//(smoke.h), where the server takes them
	if(ogun==4&&SmokeAllowed()&&(gun==51?!SmokeModeOn():gun==50&&SmokeModeOn()))
	{
		SmokeSetMode(gun==51);
		cur->lastswitchtime=curtime;
		return;
	}
	if(gun==51)
	{
		for(int i=GetLocalPlayer()->weapon+1;i<8;i++)
		{
			if(GetLocalPlayer()->owngun[i])
			{
				GetLocalPlayer()->weapon=i;
				break;
			}
		}
	}
	if(gun==50)
	{
		for(int i=GetLocalPlayer()->weapon-1;i>=0;i--)
		{
			if(GetLocalPlayer()->owngun[i])
			{
				GetLocalPlayer()->weapon=i;
				break;
			}
		}
	}
	
	if(gun<20)GetLocalPlayer()->weapon=gun;
	if(GetLocalPlayer()->weapon<0)GetLocalPlayer()->weapon=0;
	if(GetLocalPlayer()->weapon>=6)GetLocalPlayer()->weapon=6;
	if(!GetLocalPlayer()->owngun[GetLocalPlayer()->weapon]) 
	{
		GetLocalPlayer()->weapon=ogun;
	}
	if(ogun==GetLocalPlayer()->weapon) return;
	//onto the grenade: from above its smoke step, from below (or its key) as it was
	if(cur->weapon==4&&(gun==50||gun==51)) SmokeSetMode(gun==50);
	if(curtime+500>GetLocalPlayer()->nextfiretime)
	{
		GetLocalPlayer()->nextfiretime=curtime+500;
		GetLocalPlayer()->gundowntime=curtime-1000;
	}
	GetLocalPlayer()->lastswitchtime=curtime;
}

float GetSOSpeedPenalty(char pnum)
{
	float speedpenalty=0;
	for(int i2=0;i2<MAXSERVEROBJS;i2++)
	{
		if(sobject[i2].type&&sobject[i2].attached==pnum)
		{
			speedpenalty+=sobject[i2].speedpenalty;
		}
	}
	return speedpenalty;
}