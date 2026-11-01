//functions to do all the things that have to do with gameplay


#include "StdAfx.h"
#include "hugekilla.h"
#include "ui.h"
#include "vector.h"
#include "playing.h"
#include "land.h"
#include "math.h"
#include "mmsystem.h" 
#include "multiplay.h"
#include "sound.h"
#include "voice.h"


char menutext[5000];
char menu;

char talktext[1000];
extern char consoletext[100][500];
extern char consolecommand[500];
extern int consolevisible;
extern ServerVars sv;
extern char curstructurepath[1000];

extern AseObj projectile[4];
extern Particle particle[MAXPARTICLES];

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
	return height;
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
				object[i].pos+=object[i].vel*mysf;
				
			}
			if(object[i].type==4)
			{
				object[i].pos+=object[i].vel*mysf;
				object[i].vel.y-=sv.gravity*mysf;
				if(object[i].pos.y<GetHeight(object[i].pos)) 
				{
					object[i].vel=Vector(0,0,0);
					if(!object[i].wasfroze)
					{
						object[i].wasfroze=1;
						SetSoundPosition(191,object[i].pos);
						PlaySound(191,0);
					}
				}
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
							cur->vel+=Normalize(cmppos-object[i].pos)*(1/(mag+2))*4;
							if(cur==GetLocalPlayer()&&cur->life>0)
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
					for(int i3=0;i3<200;i3++)
					{
						for(i2;i2<MAXPARTICLES;i2++)
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
					for(int i3=0;i3<20;i3++)
					{
						for(i2;i2<MAXPARTICLES;i2++)
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
				
				object[i].pos+=Normalize(object[i].vel)*20*mysf;
				AcBulletTick(&object[i]);   //server: remote players hit (M11)
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
						if(object[i].type==1)
						{
							cur->vel+=Normalize(object[i].vel)*.3;
							cur->life-=(sv.highbulletdamage+RND*sv.highbulletdamage)*Magnitude(object[i].vel);
						}
						if(object[i].type==3)
						{
							cur->vel+=Normalize(object[i].vel)*0.1;
							cur->life-=(sv.lowbulletdamage+RND*sv.lowbulletdamage)*Magnitude(object[i].vel);
						}
						object[i].type=0;
						HavePain(150);
						if(cur->life<=0)
						{
							Killed(object[i].pnum);
						}
					}
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

//called once per frame, calls Play a number of times according to how much time has passed

void Playloop()
{
	int numsteps;
	LARGE_INTEGER time;
	LARGE_INTEGER freq;
	DWORD targtime;
	static DWORD targlasttime=0;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&time);
	
	targtime=((double)time.QuadPart)/(double)freq.QuadPart*1000;
	//first call (leaving the menu to join a game) or a long pause: start from now.
	//Interpolating from 0 set curtime to 0 for a frame, and a join request answered
	//within that frame got a timeout based on it, then "server timed out" at once.
	if(!targlasttime||(int)(targtime-targlasttime)>2000)
		targlasttime=targtime;
	
	
	sf=(float)(targtime-targlasttime)/40;
	if(sf>80) sf=0;
	numsteps=((float)sf)*(float)2+1;
	sf/=(float)numsteps;
	int i;
	for(i=0;i<numsteps;i++)
	{
		curtime=((float)i/numsteps)*targtime+((float)1-(float)i/numsteps)*targlasttime;
		lasttime=curtime;
		Play();
	}
	Player *cur=GetLocalPlayer();
	if(curtime>cur->menudissapeartime&&menu!=BUILDING) 
	{
		menu=0;
		menutext[0]=0;
	}
		
	targlasttime=targtime;
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

//handles all physics, movement, etc.

void Play()
{
	int numplayers=GetNumPlayers();
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
			float step=.25f*sf;
			if(step>1) step=1;
			cur->fzoom+=(zoomtarget-cur->fzoom)*step;
			if(fabs(zoomtarget-cur->fzoom)<.05f)
			{
				cur->fzoom=zoomtarget;
				zoomtarget=-1;
			}
		}
		mspeed*=controls.mousesensitivity;
		mspeed/=powf(1.05,cur->fzoom);
		cur->fwd-=CrossProduct(Vector(0,1,0),cur->fwd)*mxchange*mspeed;
		cur->tilt-=CrossProduct(Vector(0,0,1),cur->tilt)*mychange*mspeed;
		if(cur->tilt.x<.01)cur->tilt.x=.01;
		if(cur->gundowntime&&curtime>cur->nextfiretime)
		{
			cur->gundowntime=0;
		}
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
			cur->pos=Vector(LANDGRIDSIZE+RND*LANDGRIDSIZE,100,LANDGRIDSIZE+RND*LANDGRIDSIZE);
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
	
	for(int i=0;i<numplayers;i++)
	if(GetPlayer(i)->playstate>=10)
	{
		float speedpenalty=GetSOSpeedPenalty(i);
		

		Player *cur=GetPlayer(i);
		if(cur!=GetLocalPlayer())
			MakeClose(&cur->pos);
		Vector prevvel=cur->vel;
		
		cur->fwd=Normalize(cur->fwd);
		cur->tilt=Normalize(cur->tilt);
		cur->dir=Normalize(VectMult(cur->tilt,cur->fwd,Vector(0,1,0),CrossProduct(cur->dir,Vector(0,1,0)))+cur->inaccuracy);
		cur->inaccuracy+=cur->inaccuracyvel;
		cur->inaccuracyvel/=1.33;
		cur->inaccuracy/=1.12;

		if(cur->gunkick<0){cur->kickvel=0;cur->gunkick=0;}
		
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
		float oyv=cur->vel.y;
		int underwater=0;
		if(cur->pos.y-2<WATERHEIGHT) underwater=1;
		if(underwater)
		{
			cur->vel.y-=.01*sf;
			cur->vel/=powf(2.3,sf);
			cur->vel+=((cur->up*cur->dir-cur->down*cur->dir)*sv.walkaccelerate)*sf;
			Vector right=Normalize(CrossProduct(cur->dir,Vector(0,1,0)));
			cur->vel+=((cur->rstrafe-cur->lstrafe)*right*sv.strafeaccelerate)*sf;
		}
		else
		{
			cur->vel.y-=sv.gravity*sf;
		}
		float yoffset=.4;
		float centerheight=GetHeight(cur->pos-Vector(0,yoffset,0));
		float groundheight=centerheight;
		float xpheight=GetHeight(cur->pos+Vector(.4,-yoffset,0));
		float xnheight=GetHeight(cur->pos+Vector(-.4,-yoffset,0));
		float zpheight=GetHeight(cur->pos+Vector(0,-yoffset,.4));
		float znheight=GetHeight(cur->pos+Vector(0,-yoffset,-.4));

#define EQUALHEIGHT -1.1
		if(xpheight<cur->pos.y-EQUALHEIGHT)
		{
			if(xpheight>groundheight) groundheight=xpheight;
		}
		if(xnheight<cur->pos.y-EQUALHEIGHT)
		{
			if(xnheight>groundheight) groundheight=xnheight;
		}
		if(zpheight<cur->pos.y-EQUALHEIGHT)
		{
			if(zpheight>groundheight) groundheight=zpheight;
		}
		if(znheight<cur->pos.y-EQUALHEIGHT)
		{
			if(znheight>groundheight) groundheight=znheight;
		}
		xpheight=GetHeight(cur->pos+Vector(.6,-yoffset,0));
		xnheight=GetHeight(cur->pos+Vector(-.6,-yoffset,0));
		zpheight=GetHeight(cur->pos+Vector(0,-yoffset,.6));
		znheight=GetHeight(cur->pos+Vector(0,-yoffset,-.6));

		cur->thrustfuel+=sv.thrustrechargerate*sf;
		if(cur->thrustfuel>1) cur->thrustfuel=1;
		if(cur->hanggliding==0)
		if(cur->jetpack&&cur->thrustfuel>0&&sv.thrustrechargerate)
		{
			cur->thrustfuel-=sv.thrustuse*sf;
			Vector targ;
			if(sv.chopperthrust)
			{
				targ=GetRight(cur->dir,Vector(0,1,0));
				targ.x/=4;
				targ.z/=4;
			}
			else
			{
				targ.x=targ.z=0;
				targ.y=1;
			}
			cur->vel+=targ*.05*sf;
			if(speedpenalty) cur->vel.y-=.02*sf;
			int soundnum=cur==GetLocalPlayer()?170:171;
			SetSoundPosition(soundnum,cur->pos);
			for(int n=0;n<1;n++)
			{
				for(int i=0;i<MAXPARTICLES;i++)
				{
					if(particle[i].type==0)
					{
						particle[i].type=3;
						particle[i].pos=cur->pos-targ+Vector(RND*2-1,0,RND*2-1)*.1;
						particle[i].vel=targ*-.4+Vector(RND-.5,RND-.5,RND-.5)*.1;
						particle[i].timeout=curtime+3000;
						break;
					}
				}
			}
			if(!cur->wasthrusting)
			{
				PlaySound(soundnum,1);
				cur->wasthrusting=1;
			}
		}
		else
		{
			if(cur->wasthrusting)
			{
				int soundnum=cur==GetLocalPlayer()?170:171;
				StopSound(soundnum);
				cur->wasthrusting=0;
			}
		}
		Vector topheadheight=cur->pos+Vector(0,.4,0);
		if(GetHeight(topheadheight)>topheadheight.y)
		{
			cur->vel.y=cur->vel.y/powf(1.6,sf)-.1*sf;
			cur->vel/=powf(1.3,sf);
			float f=.5*sf;
			if(cur->pos.y<groundheight+1.5)
			{
				cur->vel.x+=(xnheight-xpheight)*f;
				cur->vel.z+=(znheight-zpheight)*f;
				cur->vel.x/=powf(2.4,fabs(xnheight-xpheight));
				cur->vel.z/=powf(2.4,fabs(znheight-zpheight));
			}
		}
		if(cur->hangglide&&cur->vel.y<-.2&&Magnitude(cur->vel)>.4)
		{
			cur->hanggliding=1;
		}
		//the glider folds when its key is released (it stayed out, and the jetpack
		//sharing that key did nothing, until landing). Other players' gliding state
		//comes from the network, so only the local player.
		if(cur==GetLocalPlayer()&&!cur->hangglide)
			cur->hanggliding=0;
		float roll;
		roll=0;
		if(cur->hanggliding)
		{
			Vector right=Normalize(CrossProduct(cur->fwd,Vector(0,1,0)));
			Vector mul=right*cur->vel;
			roll=DotProduct(right,cur->vel)*-80;
		}
		if(cur->hanggliding)
		{
			cur->vel=(cur->vel*10+Magnitude(cur->vel)*Normalize(cur->dir+Vector(0,.2,0)))/11;
			cur->vel/=powf(1.004,sf);
			if(Magnitude(cur->vel)<.2) cur->hanggliding=0;
			cur->roll=(cur->roll*15+roll)/16;
			if(Magnitude(cur->vel)>1.2) cur->vel/=powf(1.02,sf);
		}
		else
		{
			cur->roll/=powf(1.1,sf);
		}
		if(fabs(cur->roll)>50) cur->roll/=1.01;
		if(cur->pos.y<groundheight+1.45) cur->pos.y=groundheight+1.45;
		if(cur->pos.y<groundheight+1.5)
		{
			cur->hanggliding=0;
			if(cur->jump&&cur->playstate==10)
			{
				cur->vel.y+=.45;
				cur->jump=0;
			}
	
			if(cur->vel.y<=-.8&&curtime>cur->nextfootsteps)
			{
				SetSoundPosition(120,cur->pos+Vector(0,-2,0));
				PlaySound(120,0);
				cur->nextfootsteps=curtime+300;
				SetSoundPosition(130,cur->pos+Vector(0,-1,0));
				PlaySound(130,0);
			}
			Vector right=Normalize(CrossProduct(cur->dir,Vector(0,1,0)));
			Vector targ=((float)cur->up*cur->fwd-(float)cur->down*cur->fwd)*sv.walkaccelerate+
				((cur->rstrafe-cur->lstrafe)*right*sv.strafeaccelerate);
			if(cur->up||cur->down||cur->lstrafe||cur->rstrafe)
			{
				cur->runtime+=sf;
				if(cur->runtime>15) cur->runtime-=15;
			}
			else 
			{
				if(cur->runtime) cur->runtime+=sf;
				if(cur->runtime>15) cur->runtime=0;
			}
			cur->legframe=sin(cur->runtime/15*2*pi)*2.49+2.495;
			float omag=Magnitude(targ);
			if(omag!=0&&cur->playstate==10)
			{
				float heightdiff=(GetHeight(Normalize(targ)/2+cur->pos)-GetHeight(cur->pos))*2;
				float heightdiff2=(GetHeight(Normalize(targ)/1+cur->pos)-GetHeight(Normalize(targ)/2+cur->pos))*2;
				targ=Normalize(targ);
				//if(heightdiff<1.3||heightdiff>2)//||cur->pos.y>GetHeight(cur->pos)+.5) 
				{
					/*if(heightdiff>1.3) omag/=8;
					else
						if(heightdiff>.9) 
						{
							cur->vel.y=(cur->vel.y*5+Magnitude(cur->vel-Vector(0,cur->vel.y,0)))/6;
							omag/=1.3;
						}
					
					//if(heightdiff>1.3) heightdiff=0;
					if(heightdiff<-1)heightdiff=0;
					//if(heightdiff>0) heightdiff/=1.3;
					
					targ.y=heightdiff*omag;
					*/
					targ.y=heightdiff;
					if(fabs(heightdiff)>1.1) targ.y=0;
					if(targ.y>1) omag/=1+targ.y-1;
					targ=Normalize(targ)*omag;
					
					targ/=1+speedpenalty;
					cur->vel+=targ*sf;
				}


			}

			if(((cur->up||cur->down||cur->lstrafe||cur->rstrafe)&&curtime>cur->nextfootsteps)&&cur->playstate==10)
			{
				SetSoundPosition(100,cur->pos+Vector(0,-2,0));
				PlaySound(100,0);
				cur->nextfootsteps=curtime+600;
			}
			if((cur->up||cur->down||cur->lstrafe||cur->rstrafe))
			{
				cur->vel.x/=powf(sv.resistance,sf);
				cur->vel.z/=powf(sv.resistance,sf);
			}
			else
			{
				cur->vel.x/=powf(sv.stopresistance,sf);
				cur->vel.z/=powf(sv.stopresistance,sf);
			}
			
			//r->vel.y+=(groundheight+1.5-cur->pos.y)/4.4*sf;
			//if(cur->vel.y<.01&&cur->vel.y>-.01&&fabs(1.2-cur->pos.y)<.1) cur->vel.y=0;
			//if(cur->pos.y<groundheight+.8){cur->pos.y=groundheight+.8;if(cur->vel.y<0)cur->vel.y=0;}
			if(cur->vel.y<0)cur->vel.y/=powf(2.1,sf);//else cur->vel.y/=powf(1.1,sf);
			
			#define PUSHHEIGHT 1
			#define MAXPUSHHEIGHT 3

			if(xpheight>centerheight+PUSHHEIGHT+.3)
			{
				if(cur->vel.x>0) cur->vel.x/=powf(1.3,sf);
			}
			if(xnheight>centerheight+PUSHHEIGHT+.3)
			{
				if(cur->vel.x<0) cur->vel.x/=powf(1.3,sf);
			}
			if(zpheight>centerheight+PUSHHEIGHT+.3)
			{
				if(cur->vel.z>0) cur->vel.z/=powf(1.3,sf);
			}
			if(znheight>centerheight+PUSHHEIGHT+.3)
			{
				if(cur->vel.z<0) cur->vel.z/=powf(1.3,sf);
			}
		}
		
		int scraped=0;
		int thumped=0;
		float thumpspeed=.3;
		float scrapespeed=.5;
		float wmh=cur->pos.y-.5;
		if(xpheight>wmh)
		{
			if(cur->vel.x>0) cur->vel.x=0;
			
		}
		if(xnheight>wmh)
		{
			if(cur->vel.x<0) cur->vel.x=0;
			
		}
		if(zpheight>wmh)
		{
			if(cur->vel.z>0) cur->vel.z=0;
			
		}
		if(znheight>wmh)
		{
			if(cur->vel.z<0) cur->vel.z=0;
			
		}
		Vector cv;
		cv=Normalize(Vector(1,0,1));if(GetHeight(cur->pos+cv*.7)>wmh){float dp=DotProduct(cv,cur->vel);if(dp>0) cur->vel-=dp*cv;}
		cv=Normalize(Vector(1,0,-1));if(GetHeight(cur->pos+cv*.7)>wmh){float dp=DotProduct(cv,cur->vel);if(dp>0) cur->vel-=dp*cv;}
		cv=Normalize(Vector(-1,0,-1));if(GetHeight(cur->pos+cv*.7)>wmh){float dp=DotProduct(cv,cur->vel);if(dp>0) cur->vel-=dp*cv;}
		cv=Normalize(Vector(-1,0,1));if(GetHeight(cur->pos+cv*.7)>wmh){float dp=DotProduct(cv,cur->vel);if(dp>0) cur->vel-=dp*cv;}
		if(scraped&&curtime>cur->nextwallscrape)
		{
			SetSoundPosition(110,cur->pos+Vector(0,-1,0));
			PlaySound(110,0);
			cur->nextwallscrape=curtime+1000;
		}
		if(thumped)
		{
			SetSoundPosition(130,cur->pos+Vector(0,-1,0));
			//PlaySound(130,0);
		}
		cur->inaccuracyvel.y+=(prevvel.y-cur->vel.y)/10;
		cur->gunoffsetvel+=(prevvel-cur->vel)*sf/powf(10,sf);
		cur->gunoffset+=cur->gunoffsetvel*sf;
		cur->gunoffset/=powf(1.3,sf);
		cur->gunoffsetvel=cur->gunoffsetvel/powf(1.8,sf)-cur->gunoffset/20*sf;
		if(cur->pos.y+cur->vel.y*sf<WATERHEIGHT&&cur->pos.y>WATERHEIGHT)
		{
			SetSoundPosition(140,cur->pos+Vector(0,-1,0));
			PlaySound(140,0);
		}
		cur->pos+=cur->vel*sf;
	}	
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
				if(Magnitude(GetLocalPlayer()->pos-sobject[i].pos)<40)
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
#ifdef HK_WIN32_NATIVE
		DisplayBuildingType("structures");
		menu=BUILD;
#else
		OpenBuildMenu();   //src/ui/buildmenu.cpp
#endif
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
			strcpy(menutext,"Left click / 1: build here\rRight click / 2: cancel");
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
				PlaceStructure(1);
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
		}
		case INFOMENU:
			{
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
	
	char newtext[2000];
	char basel33t='~'+2;

	int newtextpos=0;
	for(int i=0;i<len;i)
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
	if(key==VK_BACK)
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
	if(key==VK_RETURN)
	{
		char text[2000];
		strcpy(text,l33tify(talktext));
	
		if(text[0]) SendChatMessage(text,GetLocalPlayer()->talking-1);
		talktext[0]=0;
		
		GetLocalPlayer()->talking=0;
		return 1;
	}
#ifndef HK_WIN32_NATIVE
	if(key==VK_TAB)   //switch between everyone and team
	{
		GetLocalPlayer()->talking=GetLocalPlayer()->talking==1?2:1;
		return 1;
	}
	//characters arrive as text input (ChatHandleText, keyboard layout aware);
	//the keys are still swallowed so typing doesn't move the player
	if(GetRealKey(key,flags)) return 1;
	return key==VK_BACK;
#endif
	char realkey=GetRealKey(key,flags);
	if(realkey)
	{
		int len=strlen(talktext);
		int timesinceline=0;
		for(int i=0;i<len;i++)
		{
			if(talktext[i]=='\r')
			{
				timesinceline=0;
			}
			timesinceline++;
		}

		if(timesinceline>20&&realkey==' ')
		{
			realkey='\r';
		}
		talktext[len]=realkey;
		talktext[len+1]=0;
		return 1;
	}
	return 0;
}

//converts strings representing keys into their virtual scan codes, like changing alt to VK_ALT

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
	
	if(stricmp(text,"alt")==0)
	{
		return VK_MENU;
	}
	if(stricmp(text,"ctrl")==0)
	{
		return VK_CONTROL;
	}
	if(stricmp(text,"shift")==0)
	{
		return VK_SHIFT;
	}
	if(stricmp(text,"tab")==0)
	{
		return VK_TAB;
	}
	if(stricmp(text,"space")==0)
	{
		return VK_SPACE;
	}
	if(stricmp(text,"lbutton")==0)
	{
		return VK_LBUTTON;
	}
	if(stricmp(text,"rbutton")==0)
	{
		return VK_RBUTTON;
	}
	if(stricmp(text,"up")==0)
	{
		return VK_UP;
	}
	if(stricmp(text,"down")==0)
	{
		return VK_DOWN;
	}
	if(stricmp(text,"left")==0)
	{
		return VK_LEFT;
	}
	if(stricmp(text,"right")==0)
	{
		return VK_RIGHT;
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
#ifndef HK_WIN32_NATIVE
	if(key==controls.pushtotalk) VoicePushToTalk(1);
#endif
	//placing a structure: left click (fire) builds it, right click cancels
	if(menu==BUILDING&&key==controls.fire)
	{
		HandleMenuKey('1');
		return;
	}
	if(menu==BUILDING&&key==VK_RBUTTON)
	{
		HandleMenuKey('2');
		return;
	}
	if(HandleMenuKey(key)) return;
	int extended;
	if(flags&(1<<24)) extended=1;
	if(key==VK_F4) 
	{
		consolevisible=1;
		LoadTextures();
		LoadHighTextures();
		consolevisible=0;
	}
	if(key==controls.talk)
	{
		GetLocalPlayer()->talking=1;
#ifndef HK_WIN32_NATIVE
		ChatOpened(key);
		return;
#endif
	}
	if(key==controls.talkteam)
	{
		GetLocalPlayer()->talking=2;
#ifndef HK_WIN32_NATIVE
		ChatOpened(key);
		return;
#endif
	}
	if(key==VK_F1) LoadGuns();
	if(key==VK_F2) LoadRandom("textures\\ground\\",1);
	if(key==VK_F5) LoadControls();
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
	if(key>='1'&&key<='7') SwitchGun(key-'1');
	if(key==controls.screenshot) GetLocalPlayer()->takescreenshot=1;
	if(key==controls.seemoney) GetLocalPlayer()->seemoney=1;
	if(key==controls.claim)
	{
		ClaimObject(GetLocalNum());
	}
	if(key==VK_F3) LoadSounds();
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
#ifndef HK_WIN32_NATIVE
	if(key==controls.pushtotalk) VoicePushToTalk(0);
#endif
}

//called to switch weapons (when the mouse wheel is scrolled etc.)

void SwitchGun(int gun)
{
	Player *cur=GetLocalPlayer();
	int ogun=GetLocalPlayer()->weapon;
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