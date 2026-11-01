//the server's anti-cheat checks (roadmap M11 phases 1, 3a and 3b, docs/M11.md): the
//pure logic, apart from multiplay.cpp so that tests/anticheat_test.cpp can check it
//without the game. Times are in ms (curtime), distances in world units.

#ifndef ANTICHEATH
#define ANTICHEATH

#include <math.h>
#include <string.h>

#define ACWEAPONS 10   //weapon numbers a position can carry (ValidWeapon)

//---- token bucket: rate tokens per ms, at most burst ----

struct AcBucket
{
	float tokens;
	unsigned int last;
	int started;   //0: full at the first use
};

inline void AcRefill(AcBucket *b,unsigned int now,float rate,float burst)
{
	if(!b->started)
	{
		b->started=1;
		b->tokens=burst;
		b->last=now;
	}
	int dt=(int)(now-b->last);   //curtime never goes back, but don't trust it
	if(dt>0) b->tokens+=dt*rate;
	b->last=now;
	if(b->tokens>burst) b->tokens=burst;
}

//takes cost tokens if there are that many (1), else takes nothing (0)
inline int AcTake(AcBucket *b,unsigned int now,float rate,float burst,float cost=1)
{
	AcRefill(b,now,rate,burst);
	if(b->tokens<cost) return 0;
	b->tokens-=cost;
	return 1;
}

//---- per source IP (info port: REQUESTINFO, REQUESTENTER, anything) ----

struct AcIpEntry
{
	unsigned int ip;   //as in sin_addr, 0: free
	unsigned int lastseen;
	AcBucket packets,info,enter;
};

//the entry of ip, a free one, or the one unseen for the longest time (a flood of
//spoofed addresses only recycles entries, it never grows the table)
inline AcIpEntry *AcIpFind(AcIpEntry *table,int size,unsigned int ip,unsigned int now)
{
	AcIpEntry *oldest=&table[0];
	for(int i=0;i<size;i++)
	{
		if(table[i].ip==ip&&ip)
		{
			table[i].lastseen=now;
			return &table[i];
		}
		if(!table[i].ip||(int)(table[i].lastseen-oldest->lastseen)<0) oldest=&table[i];
		if(!oldest->ip) break;
	}
	memset(oldest,0,sizeof(*oldest));
	oldest->ip=ip;
	oldest->lastseen=now;
	return oldest;
}

//---- distances on the world, which wraps every size units on x and z ----

inline float AcWrap(float d,float size)
{
	d=fmodf(d,size);
	if(d>size/2) d-=size;
	if(d<-size/2) d+=size;
	return d;
}

inline float AcDistance(float ax,float ay,float az,float bx,float by,float bz,float size)
{
	float dx=AcWrap(ax-bx,size),dy=ay-by,dz=AcWrap(az-bz,size);
	return sqrtf(dx*dx+dy*dy+dz*dz);
}

//---- position jumps (log only): speed between two reported positions ----

struct AcMove
{
	float x,y,z;
	unsigned int time;
	int valid;   //0: no reference (joined, respawned)
};

//speed in units/s from the reference to this position once at least window ms
//went by (the reference then moves here); -1 while the window isn't over, or
//without a reference (this position becomes it)
inline float AcMoveSpeed(AcMove *m,float x,float y,float z,unsigned int now,int window,float worldsize)
{
	if(!m->valid||(int)(now-m->time)<0)
	{
		m->x=x;m->y=y;m->z=z;
		m->time=now;
		m->valid=1;
		return -1;
	}
	int dt=(int)(now-m->time);
	if(dt<window||dt<=0) return -1;
	float speed=AcDistance(x,y,z,m->x,m->y,m->z,worldsize)*1000/dt;
	m->x=x;m->y=y;m->z=z;
	m->time=now;
	return speed;
}

//---- building: a block near the player who sends it ----

//block bx,by,bz (block units, x and z wrap every gridsize) within maxblocks of
//the player at px,py,pz (world units) on x and z, and not more than maxblocks
//above it; any depth below (a structure's pillars go down to the ground)
inline int AcBuildNear(int bx,int by,int bz,float px,float py,float pz,int maxblocks,float blocksize,int gridsize)
{
	float dx=AcWrap(bx+.5f-px/blocksize,(float)gridsize);
	float dz=AcWrap(bz+.5f-pz/blocksize,(float)gridsize);
	float dy=by+.5f-py/blocksize;
	return fabsf(dx)<=maxblocks&&fabsf(dz)<=maxblocks&&dy<=maxblocks;
}

//---- ammunition: the magazine of each weapon, as the server counts it ----

//The weapon of a shot is the one of the last position (FIREGUN doesn't carry
//it), which may be late after a switch: the shots since that position are
//moved to the next position's weapon if it changed.
struct AcAmmo
{
	int mag[ACWEAPONS];
	int shots;      //shots counted since the last position
	int reloaded;   //a reload since the last position
	int weapon;     //of the last position
};

inline void AcAmmoFill(AcAmmo *a,const int *hold)
{
	for(int w=0;w<ACWEAPONS;w++) a->mag[w]=hold[w];
	a->shots=0;
	a->reloaded=0;
}

//a shot with weapon: 1 if the magazine had a round (slack: rounds the count
//may be off by)
inline int AcAmmoFire(AcAmmo *a,int weapon,int slack)
{
	if(weapon<0||weapon>=ACWEAPONS) return 0;
	if(a->mag[weapon]<=-slack) return 0;
	a->mag[weapon]--;
	a->shots++;
	return 1;
}

inline void AcAmmoReload(AcAmmo *a,int weapon,const int *hold)
{
	if(weapon<0||weapon>=ACWEAPONS) return;
	a->mag[weapon]=hold[weapon];
	a->reloaded=1;
}

//a position with weapon
inline void AcAmmoPosition(AcAmmo *a,int weapon,const int *hold)
{
	if(weapon<0||weapon>=ACWEAPONS) return;
	if(weapon!=a->weapon&&a->weapon>=0&&a->weapon<ACWEAPONS)
	{
		a->mag[a->weapon]+=a->shots;
		a->mag[weapon]-=a->shots;
		if(a->reloaded) a->mag[weapon]=hold[weapon];
	}
	a->weapon=weapon;
	a->shots=0;
	a->reloaded=0;
}

//---- fire rate: a budget of ms, a shot costs its weapon's interval ----

//cost: the weapon's interval times the tolerance; burst: how much the shots may
//bunch up (network jitter). Returns 1 if the shot is allowed.
inline int AcFireAllowed(AcBucket *b,unsigned int now,float cost,float burst)
{
	return AcTake(b,now,1,burst+cost,cost);
}

//a reload: no shot for reloadtime (times the tolerance)
inline void AcFireReload(AcBucket *b,unsigned int now,float cost,float burst,float reloadcost)
{
	AcRefill(b,now,1,burst+cost);
	b->tokens-=reloadcost;
	if(b->tokens<-reloadcost) b->tokens=-reloadcost;
}

//---- damage the server could attribute to a player ----

//damage of a grenade exploding mag units away (playing.cpp, TickObjects)
inline float AcGrenadeDamage(float mag,float grenadedamage)
{
	return (.5f/(mag*mag+4))*grenadedamage;
}

//a bullet at bullet hits the player at pos (playing.cpp, TickObjects)
inline int AcBulletHits(float bx,float by,float bz,float px,float py,float pz,float worldsize)
{
	float hx=AcWrap(bx-px,worldsize),hz=AcWrap(bz-pz,worldsize);
	float vdiff=py-by;
	return sqrtf(hx*hx+hz*hz)<.5f&&vdiff<2&&vdiff>0;
}

//---- server-decided hits (M11 phase 3a): lag compensation, swept bullets ----

//where a player was: what the server had of it, one entry every few tens of ms
#define ACHISTORY 32
struct AcHistory
{
	float x[ACHISTORY],y[ACHISTORY],z[ACHISTORY];
	unsigned int time[ACHISTORY];
	int count;   //entries, newest at next-1; 0: none (joined, dead, respawned)
	int next;
};

//adds a position, at most one every spacing ms (32 entries then cover the
//longest rewind with a few to spare)
inline void AcHistoryAdd(AcHistory *h,float x,float y,float z,unsigned int now,int spacing)
{
	if(h->count)
	{
		int last=(h->next+ACHISTORY-1)%ACHISTORY;
		if((int)(now-h->time[last])<spacing) return;
	}
	h->x[h->next]=x;h->y[h->next]=y;h->z[h->next]=z;
	h->time[h->next]=now;
	h->next=(h->next+1)%ACHISTORY;
	if(h->count<ACHISTORY) h->count++;
}

//the position at time t, between the two entries around it; the current one
//(cx,cy,cz at now) is the newest, the oldest entry is as far back as it goes.
//Interpolated on the wrapped world (the server re-wraps positions near its own
//player).
inline void AcHistoryAt(const AcHistory *h,unsigned int t,float cx,float cy,float cz,unsigned int now,
	float *out,float worldsize)
{
	float nx=cx,ny=cy,nz=cz;
	unsigned int nt=now;
	for(int k=0;k<h->count;k++)
	{
		if((int)(t-nt)>=0) break;
		int e=(h->next+ACHISTORY-1-k)%ACHISTORY;
		float ex=h->x[e],ey=h->y[e],ez=h->z[e];
		unsigned int et=h->time[e];
		if((int)(t-et)>=0&&(int)(nt-et)>0)
		{
			float f=(float)(int)(t-et)/(float)(int)(nt-et);
			out[0]=ex+AcWrap(nx-ex,worldsize)*f;
			out[1]=ey+(ny-ey)*f;
			out[2]=ez+AcWrap(nz-ez,worldsize)*f;
			return;
		}
		nx=ex;ny=ey;nz=ez;
		nt=et;
	}
	out[0]=nx;out[1]=ny;out[2]=nz;
}

//does the segment a -> b (one step of a bullet) cross the hitbox of a player at p:
//the client's rule (AcBulletHits: within .5 on x and z, between 2 below its
//position and its position), tol larger on every side, tested along the whole
//step instead of at its end (a fast bullet or a slow server frame skips over it)
inline int AcSegmentHits(float ax,float ay,float az,float bx,float by,float bz,
	float px,float py,float pz,float tol,float worldsize)
{
	float dx=AcWrap(bx-ax,worldsize),dy=by-ay,dz=AcWrap(bz-az,worldsize);
	float cx=AcWrap(px-ax,worldsize),cz=AcWrap(pz-az,worldsize);
	float r=.5f+tol;
	float t0=0,t1=1;
	//on x and z: |t*d - c| < r
	float A=dx*dx+dz*dz,B=-2*(dx*cx+dz*cz),C=cx*cx+cz*cz-r*r;
	if(A<1e-12f)
	{
		if(C>=0) return 0;
	}
	else
	{
		float disc=B*B-4*A*C;
		if(disc<=0) return 0;
		float sq=sqrtf(disc);
		float e0=(-B-sq)/(2*A),e1=(-B+sq)/(2*A);
		if(e0>t0) t0=e0;
		if(e1<t1) t1=e1;
		if(t0>=t1) return 0;
	}
	//in height: py-2-tol < ay + t*dy < py+tol
	float lo=py-2-tol,hi=py+tol;
	if(fabsf(dy)<1e-12f) return ay>lo&&ay<hi;
	float h0=(lo-ay)/dy,h1=(hi-ay)/dy;
	if(h0>h1) { float k=h0;h0=h1;h1=k; }
	if(h0>t0) t0=h0;
	if(h1<t1) t1=h1;
	return t0<t1;
}

//how far back to look for a shooter with this ping (round trip, ms): what it saw
//when it fired had come from the server about a round trip earlier
inline int AcRewind(int ping,int lagcomp)
{
	if(ping<0) ping=0;
	return ping<lagcomp?ping:lagcomp;
}

//---- M11 phase 3b: numbered positions, movement bound, stable slots ----

//the sequence numbers (uint16, wrapping) of a client's positions
struct AcSeq
{
	unsigned short last;   //of the newest accepted
	int valid;             //0: none yet (joined): the next one is accepted
};

//1 if seq is newer than the last accepted (it becomes the last), 0 if it is that
//one again or an older one (a duplicate, or overtaken on the way). Serial number
//arithmetic: up to 32767 ahead is newer, so a few lost positions don't matter.
inline int AcSeqNewer(AcSeq *s,unsigned int seq)
{
	seq&=0xFFFF;
	if(!s->valid)
	{
		s->valid=1;
		s->last=(unsigned short)seq;
		return 1;
	}
	short d=(short)(unsigned short)(seq-s->last);
	if(d<=0) return 0;
	s->last=(unsigned short)seq;
	return 1;
}

//where the server lets a player be (ac_moveclamp): the last position it kept
struct AcClamp
{
	float x,y,z;
	unsigned int time;
	int valid;   //0: none (joined, respawned): the next position is kept as it is
};

//moves the claimed position x,y,z (world wrapping every size on x and z) at most
//maxspeed units/s away from the last one kept, over the time since it (at least
//mindt ms, positions arrive bunched up). Returns 1 if it had to be pulled back.
//Pulled back, the player catches up as soon as it moves slower than maxspeed.
inline int AcMoveClamp(AcClamp *c,float *x,float *y,float *z,unsigned int now,float maxspeed,int mindt,float size)
{
	int clamped=0;
	if(c->valid&&(int)(now-c->time)>=0)
	{
		int dt=(int)(now-c->time);
		if(dt<mindt) dt=mindt;
		float dx=AcWrap(*x-c->x,size),dy=*y-c->y,dz=AcWrap(*z-c->z,size);
		float d=sqrtf(dx*dx+dy*dy+dz*dz);
		float allowed=maxspeed*dt/1000;
		if(d>allowed&&d>0)
		{
			float f=allowed/d;
			*x=fmodf(c->x+dx*f+size,size);
			*y=c->y+dy*f;
			*z=fmodf(c->z+dz*f+size,size);
			clamped=1;
		}
	}
	c->x=*x;c->y=*y;c->z=*z;
	c->time=now;
	c->valid=1;
	return clamped;
}

//player slots: a joiner takes the first free one (from first, the host is 0),
//-1 if none is
inline int SlotFirstFree(const char *used,int first,int max)
{
	for(int i=first;i<max;i++)
		if(!used[i]) return i;
	return -1;
}

//numplayers once slots were freed: one past the last used (at least 1, the host)
inline int SlotTrim(const char *used,int n)
{
	while(n>1&&!used[n-1]) n--;
	return n;
}

//the number of player i once player left left: -1 for left itself; one lower
//after it if the slots are compacted (everyone after it moved down)
inline int SlotRenumber(int i,int left,int compact)
{
	if(i==left) return -1;
	if(compact&&i>left) return i-1;
	return i;
}

#endif
