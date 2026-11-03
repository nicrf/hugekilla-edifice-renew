//the server's anti-cheat checks (roadmap M11 phases 1, 3a and 3b, docs/M11.md): the
//pure logic, apart from multiplay.cpp so that tests/anticheat_test.cpp can check it
//without the game. Times are in ms (curtime), distances in world units.

#ifndef ANTICHEATH
#define ANTICHEATH

#include <math.h>
#include <string.h>

#define ACWEAPONS 10   //weapon numbers a command can carry (ValidWeapon)

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

//The weapon of a shot is the one of the last command (FIREGUN doesn't carry
//it), which may be late after a switch: the shots since that command are
//moved to the next command's weapon if it changed.
struct AcAmmo
{
	int mag[ACWEAPONS];
	int shots;      //shots counted since the last command
	int reloaded;   //a reload since the last command
	int weapon;     //of the last command
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

//a command with weapon
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

//---- server-decided hits (M11 phase 3a): swept bullets ----

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

//---- M16 phase 5 (docs/M16.md): where each player was at each server tick ----

//the last ACTICKS ticks of a player, by tick number (~1 s at 60 Hz): where it was
//after that tick, and whether it was alive then
#define ACTICKS 64
struct AcTickRing
{
	unsigned int tick[ACTICKS];
	float x[ACTICKS],y[ACTICKS],z[ACTICKS];
	unsigned char alive[ACTICKS];
	unsigned char used[ACTICKS];
};

inline void AcTickAdd(AcTickRing *r,unsigned int tick,float x,float y,float z,int alive)
{
	int k=tick%ACTICKS;
	r->tick[k]=tick;
	r->x[k]=x;r->y[k]=y;r->z[k]=z;
	r->alive[k]=alive!=0;
	r->used[k]=1;
}

//the entry of tick, 0 if it isn't kept (too old, not recorded)
inline int AcTickFind(const AcTickRing *r,unsigned int tick)
{
	int k=tick%ACTICKS;
	return r->used[k]&&r->tick[k]==tick?k+1:0;
}

//the player at the moment tick + frac (frac 0..1), between the ticks around it as
//a client interpolates the snapshots (netsync.h SnapSample): out of the world's
//copies (x and z wrap on worldsize), the first one alone after a jump of more than
//teleport units (a respawn). 0 if it wasn't alive then, or that tick isn't kept.
inline int AcTickAt(const AcTickRing *r,unsigned int tick,float frac,float *out,float worldsize,float teleport)
{
	int a=AcTickFind(r,tick);
	if(!a||!r->alive[a-1]) return 0;
	a--;
	out[0]=r->x[a];out[1]=r->y[a];out[2]=r->z[a];
	int b=frac>0?AcTickFind(r,tick+1):0;
	if(!b||!r->alive[b-1]) return 1;
	b--;
	float dx=AcWrap(r->x[b]-r->x[a],worldsize),dy=r->y[b]-r->y[a],dz=AcWrap(r->z[b]-r->z[a],worldsize);
	if(sqrtf(dx*dx+dy*dy+dz*dz)>teleport) return 1;
	out[0]+=dx*frac;out[1]+=dy*frac;out[2]+=dz*frac;
	return 1;
}

//ticks to rewind a shot by: from lastdone (the server's last tick) back to what the
//shooter drew (viewtick + viewfrac/256), at most maxticks, never ahead
inline float AcShotLag(unsigned int lastdone,unsigned int viewtick,int viewfrac,float maxticks)
{
	float lag=(float)(int)(lastdone-viewtick)-viewfrac/256.f;
	if(!(lag>0)) return 0;   //ahead of the server (or nonsense): not rewound
	return lag<maxticks?lag:(maxticks>0?maxticks:0);
}

//---- M11 phase 3b: stable slots ----

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
