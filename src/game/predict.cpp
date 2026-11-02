//The client's prediction of its own player and its reconciliation with the server
//(docs/M16.md, phase 3; predict.h).

#include <math.h>
#include <string.h>
#include "predict.h"
#include "land.h"   //LANDGRIDSIZE

void PredictReset(Prediction *pr)
{
	memset(pr,0,sizeof(*pr));
}

const PredictEntry *PredictFind(const Prediction *pr,unsigned int num)
{
	const PredictEntry *e=&pr->ring[num%PREDICTRING];
	return e->valid&&e->num==num?e:0;
}

void PredictTick(Prediction *pr,Player *p,unsigned int num,const MoveCmd &c,float speedpenalty,
	float sf,const ServerVars &sv,const MoveWorld &w,MoveEvents *ev)
{
	if(pr->anyack) speedpenalty=pr->speedpenalty;
	ApplyMoveCmd(p,c,speedpenalty,sf,sv,w,ev,1);
	PredictEntry *e=&pr->ring[num%PREDICTRING];
	e->num=num;
	e->cmd=c;
	GetMoveState(p,&e->after);
	e->speedpenalty=speedpenalty;
	e->playstate=p->playstate;
	e->valid=1;
	pr->latest=num;
	pr->any=1;
}

//b - a, x and z the short way round the repeating world
static Vector WrapDiff(const Vector &a,const Vector &b)
{
	Vector d=b-a;
	if(d.x>LANDGRIDSIZE/2) d.x-=LANDGRIDSIZE;
	if(d.x<-LANDGRIDSIZE/2) d.x+=LANDGRIDSIZE;
	if(d.z>LANDGRIDSIZE/2) d.z-=LANDGRIDSIZE;
	if(d.z<-LANDGRIDSIZE/2) d.z+=LANDGRIDSIZE;
	return d;
}

PredictResult PredictReconcile(Prediction *pr,Player *p,unsigned int num,const MoveState &server,
	float speedpenalty,float sf,const ServerVars &sv,const MoveWorld &w)
{
	PredictResult r;
	memset(&r,0,sizeof(r));
	PredictEntry *e=&pr->ring[num%PREDICTRING];
	if(!pr->any||!e->valid||e->num!=num||CmdNewer(num,pr->latest)) return r;
	if(pr->anyack&&!CmdNewer(num,pr->acked)) return r;   //overtaken on the way, or again
	pr->acked=num;
	pr->anyack=1;
	pr->speedpenalty=speedpenalty;
	//the commands after it: with the server's penalty from now on
	int penalty=0;
	for(unsigned int k=num+1;CmdNewer(k,num)&&!CmdNewer(k,pr->latest);k++)
	{
		PredictEntry *n=&pr->ring[k%PREDICTRING];
		if(n->valid&&n->num==k&&n->speedpenalty!=speedpenalty)
		{
			n->speedpenalty=speedpenalty;
			penalty=1;
		}
	}
	r.poserr=Magnitude(WrapDiff(e->after.pos,server.pos));
	r.velerr=Magnitude(server.vel-e->after.vel);
	r.kind=PREDICT_OK;
	if(r.poserr<PREDICTPOSERR&&r.velerr<PREDICTVELERR&&!penalty) return r;
	r.kind=PREDICT_CORRECTED;
	//from the server's state, the commands after it again; what isn't the move's
	//(the gun's sway, the keys held now, the playstate) stays as it is
	Vector before=p->pos;
	Vector gunoffset=p->gunoffset,gunoffsetvel=p->gunoffsetvel,inaccuracyvel=p->inaccuracyvel;
	char up=p->up,down=p->down,lstrafe=p->lstrafe,rstrafe=p->rstrafe,jetpack=p->jetpack,hangglide=p->hangglide;
	char playstate=p->playstate;
	MoveState s=server;
	s.legframe=e->after.legframe;
	e->after=s;
	SetMoveState(p,s);
	for(unsigned int k=num+1;CmdNewer(k,num)&&!CmdNewer(k,pr->latest);k++)
	{
		PredictEntry *n=&pr->ring[k%PREDICTRING];
		if(!n->valid||n->num!=k) continue;   //a tick without a command (not in the game)
		MoveEvents ev;
		p->playstate=n->playstate;
		ApplyMoveCmd(p,n->cmd,n->speedpenalty,sf,sv,w,&ev,1);
		GetMoveState(p,&n->after);
		r.replayed++;
	}
	p->playstate=playstate;
	p->gunoffset=gunoffset;
	p->gunoffsetvel=gunoffsetvel;
	p->inaccuracyvel=inaccuracyvel;
	p->up=up;
	p->down=down;
	p->lstrafe=lstrafe;
	p->rstrafe=rstrafe;
	p->jetpack=jetpack;
	p->hangglide=hangglide;
	r.shift=WrapDiff(before,p->pos);
	return r;
}
