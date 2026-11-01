//the server's anti-cheat checks (roadmap M11 phase 1, docs/M11.md): the pure
//logic, apart from multiplay.cpp so that tests/anticheat_test.cpp can check it
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

#endif
