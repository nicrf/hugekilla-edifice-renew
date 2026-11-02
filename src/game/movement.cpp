//The physics of a player's move (docs/M16.md, phase 0): Play() did it inline for
//every player. Nothing here but the player, the land and the server's variables: no
//sounds, particles, clock or rand(), so the server and a client predicting its own
//player (phases 2 and 3) can run the same steps and get the same result.

#include <math.h>
#include "movement.h"
#include "land.h"   //WATERHEIGHT

#define pi 3.14159265358979326846264   //redrawing.h's (the legs' animation)

void AimPlayer(Player *cur)
{
	cur->fwd=Normalize(cur->fwd);
	cur->tilt=Normalize(cur->tilt);
	cur->dir=Normalize(VectMult(cur->tilt,cur->fwd,Vector(0,1,0),CrossProduct(cur->dir,Vector(0,1,0)))+cur->inaccuracy);
	cur->inaccuracy+=cur->inaccuracyvel;
	cur->inaccuracyvel/=1.33;
	cur->inaccuracy/=1.12;

	if(cur->gunkick<0){cur->kickvel=0;cur->gunkick=0;}
}

void MovePlayer(Player *cur,const MoveInput &in,float sf,const ServerVars &sv,const MoveWorld &w,MoveEvents *ev)
{
	*ev=MoveEvents();
	Vector prevvel=cur->vel;
	int underwater=0;
	if(cur->pos.y-2<WATERHEIGHT) underwater=1;
	if(underwater)
	{
		cur->vel.y-=.01*sf;
		cur->vel/=powf(2.3,sf);
		Vector right=Normalize(CrossProduct(cur->dir,Vector(0,1,0)));
		if(in.pad)
			cur->vel+=(in.padfwd*cur->dir*sv.walkaccelerate+in.padstrafe*right*sv.strafeaccelerate)*sf;
		else
		{
			cur->vel+=((cur->up*cur->dir-cur->down*cur->dir)*sv.walkaccelerate)*sf;
			cur->vel+=((cur->rstrafe-cur->lstrafe)*right*sv.strafeaccelerate)*sf;
		}
	}
	else
	{
		cur->vel.y-=sv.gravity*sf;
	}
	float yoffset=.4;
	float centerheight=w.height(cur->pos-Vector(0,yoffset,0));
	float groundheight=centerheight;
	float xpheight=w.height(cur->pos+Vector(.4,-yoffset,0));
	float xnheight=w.height(cur->pos+Vector(-.4,-yoffset,0));
	float zpheight=w.height(cur->pos+Vector(0,-yoffset,.4));
	float znheight=w.height(cur->pos+Vector(0,-yoffset,-.4));

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
	xpheight=w.height(cur->pos+Vector(.6,-yoffset,0));
	xnheight=w.height(cur->pos+Vector(-.6,-yoffset,0));
	zpheight=w.height(cur->pos+Vector(0,-yoffset,.6));
	znheight=w.height(cur->pos+Vector(0,-yoffset,-.6));

	cur->thrustfuel+=sv.thrustrechargerate*sf;
	if(cur->thrustfuel>1) cur->thrustfuel=1;
	if(cur->hanggliding==0)   //gliding: the jetpack neither pushes nor stops
	{
		//alive only (playstate 10): a dead player waiting to respawn fell up with it
		if(cur->jetpack&&cur->playstate==10&&cur->thrustfuel>0&&sv.thrustrechargerate)
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
			if(in.speedpenalty) cur->vel.y-=.02*sf;
			ev->thrust=1;
			ev->thrustpos=cur->pos;
			ev->thrustdir=targ;
		}
		else
			ev->thrust=0;
	}
	Vector topheadheight=cur->pos+Vector(0,.4,0);
	if(w.height(topheadheight)>topheadheight.y)
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
	if(cur->hangglide&&cur->playstate==10&&cur->vel.y<-.2&&Magnitude(cur->vel)>.4)
	{
		cur->hanggliding=1;
	}
	//the glider folds when its key is released (it stayed out, and the jetpack
	//sharing that key did nothing, until landing). Other players' gliding state
	//comes from the network, so only the local player.
	if(in.local&&!cur->hangglide)
		cur->hanggliding=0;
	float roll;
	roll=0;
	if(cur->hanggliding)
	{
		Vector right=Normalize(CrossProduct(cur->fwd,Vector(0,1,0)));
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

		if(cur->vel.y<=-.8)
		{
			ev->landed=1;
			ev->landpos=cur->pos;
		}
		Vector right=Normalize(CrossProduct(cur->dir,Vector(0,1,0)));
		Vector targ=((float)cur->up*cur->fwd-(float)cur->down*cur->fwd)*sv.walkaccelerate+
			((cur->rstrafe-cur->lstrafe)*right*sv.strafeaccelerate);
		if(in.pad)
			targ=in.padfwd*cur->fwd*sv.walkaccelerate+in.padstrafe*right*sv.strafeaccelerate;
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
			float heightdiff=(w.height(Normalize(targ)/2+cur->pos)-w.height(cur->pos))*2;
			targ=Normalize(targ);
			//if(heightdiff<1.3||heightdiff>2)//||cur->pos.y>w.height(cur->pos)+.5) 
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
				
				targ/=1+in.speedpenalty;
				cur->vel+=targ*sf;
			}


		}

		if((cur->up||cur->down||cur->lstrafe||cur->rstrafe)&&cur->playstate==10)
		{
			ev->walked=1;
			ev->walkpos=cur->pos;
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
	cv=Normalize(Vector(1,0,1));if(w.height(cur->pos+cv*.7)>wmh){float dp=DotProduct(cv,cur->vel);if(dp>0) cur->vel-=dp*cv;}
	cv=Normalize(Vector(1,0,-1));if(w.height(cur->pos+cv*.7)>wmh){float dp=DotProduct(cv,cur->vel);if(dp>0) cur->vel-=dp*cv;}
	cv=Normalize(Vector(-1,0,-1));if(w.height(cur->pos+cv*.7)>wmh){float dp=DotProduct(cv,cur->vel);if(dp>0) cur->vel-=dp*cv;}
	cv=Normalize(Vector(-1,0,1));if(w.height(cur->pos+cv*.7)>wmh){float dp=DotProduct(cv,cur->vel);if(dp>0) cur->vel-=dp*cv;}
	//the trunks: round walls, from the feet to the top of the head (props.h)
	w.pushprops(cur->pos,cur->vel,.5f,1.45f,.4f);
	cur->inaccuracyvel.y+=(prevvel.y-cur->vel.y)/10;
	cur->gunoffsetvel+=(prevvel-cur->vel)*sf/powf(10,sf);
	cur->gunoffset+=cur->gunoffsetvel*sf;
	cur->gunoffset/=powf(1.3,sf);
	cur->gunoffsetvel=cur->gunoffsetvel/powf(1.8,sf)-cur->gunoffset/20*sf;
	if(cur->pos.y+cur->vel.y*sf<WATERHEIGHT&&cur->pos.y>WATERHEIGHT)
	{
		ev->splash=1;
		ev->splashpos=cur->pos;
	}
	cur->pos+=cur->vel*sf;
}
