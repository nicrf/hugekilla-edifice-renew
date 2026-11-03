// Unit test of src/net/anticheat.h (roadmap M11 phases 1 and 3a, M16 phase 5): rate
// limits, wrapped distances, build distance, the server's magazine count and fire
// rate, the damage rules the server copies from TickObjects, a bullet's step
// against a player's hitbox, the players at a tick (lag compensation) and the slots.
#include "anticheat.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

int main()
{
	// token bucket: full at first, then rate per ms
	AcBucket b;
	memset(&b,0,sizeof(b));
	int n=0;
	for(int i=0;i<100;i++) n+=AcTake(&b,1000,.01f,5);
	CHECK(n==5);
	CHECK(!AcTake(&b,1050,.01f,5));    // .5 token
	CHECK(AcTake(&b,1100,.01f,5));     // 1 token
	CHECK(!AcTake(&b,1100,.01f,5));
	CHECK(AcTake(&b,900,.01f,5)==0);   // time going back adds nothing
	n=0;
	for(int i=0;i<100;i++) n+=AcTake(&b,100000,.01f,5);
	CHECK(n==5);                       // never more than burst

	// IP table: same entry per IP, recycled oldest when full
	AcIpEntry table[4];
	memset(table,0,sizeof(table));
	AcIpEntry *e1=AcIpFind(table,4,0x0100007f,10);
	e1->packets.tokens=42;
	CHECK(AcIpFind(table,4,0x0100007f,20)==e1&&e1->packets.tokens==42);
	AcIpFind(table,4,2,30);
	AcIpFind(table,4,3,40);
	AcIpFind(table,4,4,50);
	AcIpFind(table,4,0x0100007f,55);  // seen again: not the oldest
	AcIpEntry *e5=AcIpFind(table,4,5,60);   // evicts 2 (seen at 30)
	CHECK(e5->ip==5&&e5->packets.started==0);
	int found2=0,found1=0;
	for(int i=0;i<4;i++) { found2+=table[i].ip==2; found1+=table[i].ip==0x0100007f; }
	CHECK(!found2&&found1);

	// wrapped distances
	CHECK(fabsf(AcWrap(510,512)-(-2))<1e-4f);
	CHECK(fabsf(AcWrap(-510,512)-2)<1e-4f);
	CHECK(fabsf(AcDistance(1,0,1,511,0,511,512)-sqrtf(8))<1e-3f);

	// building: per axis on x and z (wrapping), any depth below
	CHECK(AcBuildNear(50,30,50,100,60,100,10,2,256));
	CHECK(AcBuildNear(59,30,41,100,60,100,10,2,256));
	CHECK(!AcBuildNear(70,30,50,100,60,100,10,2,256));
	CHECK(AcBuildNear(50,0,50,100,60,100,10,2,256));       // pillar down to the ground
	CHECK(!AcBuildNear(50,50,50,100,60,100,10,2,256));    // far above
	CHECK(AcBuildNear(-2,30,50,1020,60,100,10,2,256));    // char x across the edge: 510/2 = block 254
	CHECK(AcBuildNear(1,30,50,510,60,100,10,2,256));

	// magazine: slack, reload, weapon switch seen late
	int hold[ACWEAPONS]={50,16,17,30,4,14,4,0,0,0};
	AcAmmo a;
	memset(&a,0,sizeof(a));
	AcAmmoFill(&a,hold);
	a.weapon=6;
	n=0;
	for(int i=0;i<10;i++) n+=AcAmmoFire(&a,6,2);
	CHECK(n==6);                       // 4 rounds + 2 of slack
	AcAmmoReload(&a,6,hold);
	CHECK(a.mag[6]==4);
	CHECK(!AcAmmoFire(&a,-1,2)&&!AcAmmoFire(&a,10,2));
	AcAmmoPosition(&a,6,hold);
	// switched to the gat, but the server still thinks sr1000: the shots move over
	n=0;
	for(int i=0;i<5;i++) n+=AcAmmoFire(&a,6,2);
	CHECK(n==5&&a.mag[6]==-1);
	AcAmmoPosition(&a,0,hold);
	CHECK(a.mag[6]==4&&a.mag[0]==45);
	// a reload of the new weapon seen with the old one
	AcAmmoReload(&a,0,hold);           // refills the gat (current one)
	a.mag[3]=1;
	AcAmmoReload(&a,0,hold);           // actually of the ump, already switched
	AcAmmoPosition(&a,3,hold);
	CHECK(a.mag[3]==30);

	// fire rate: gat 100 ms, tolerance .75, burst 300 ms
	AcBucket f;
	memset(&f,0,sizeof(f));
	float cost=100*.75f;
	n=0;
	for(int t=0;t<2000;t+=100) n+=AcFireAllowed(&f,10000+t,cost,300);
	CHECK(n==20);                      // the real rate always passes
	memset(&f,0,sizeof(f));
	n=0;
	for(int t=0;t<1000;t+=5) n+=AcFireAllowed(&f,10000+t,cost,300);
	CHECK(n>=14&&n<=19);               // (300+75)/75 = 5 at once, then one per 75 ms
	memset(&f,0,sizeof(f));
	n=0;
	for(int t=0;t<2000;t+=100) n+=AcFireAllowed(&f,10000+(t/300)*300,cost,300);   // bunched by 3
	CHECK(n==20);
	// a reload: no shot for .75 of its 3000 ms
	memset(&f,0,sizeof(f));
	CHECK(AcFireAllowed(&f,20000,cost,300));
	AcFireReload(&f,20000,cost,300,3000*.75f);
	CHECK(!AcFireAllowed(&f,21000,cost,300));
	CHECK(AcFireAllowed(&f,23000,cost,300));
	// sr1000 1500 ms: a lone shot always passes
	memset(&f,0,sizeof(f));
	CHECK(AcFireAllowed(&f,0,1500*.75f,300));
	CHECK(!AcFireAllowed(&f,500,1500*.75f,300));
	CHECK(AcFireAllowed(&f,1200,1500*.75f,300));

	// damage rules
	CHECK(fabsf(AcGrenadeDamage(0,3600)-450)<.01f);
	CHECK(fabsf(AcGrenadeDamage(4,3600)-90)<.01f);
	CHECK(AcBulletHits(100,49.8f,100,100.3f,50,100,512));
	CHECK(!AcBulletHits(100,50.2f,100,100,50,100,512));   // above the player's position
	CHECK(!AcBulletHits(100,47.9f,100,100,50,100,512));   // 2.1 below
	CHECK(!AcBulletHits(100,49.8f,100,100.6f,50,100,512));
	CHECK(AcBulletHits(511.8f,49.8f,100,.1f,50,100,512)); // across the edge

	// M11 phase 3a: a bullet's step against a player's hitbox (.5 around on x
	// and z, from 2 below its position to its position), with a tolerance
	CHECK(AcSegmentHits(90,49,100,110,49,100,100,50,100,0,512));     // straight through
	CHECK(!AcSegmentHits(90,49,101,110,49,101,100,50,100,0,512));    // 1 unit beside
	CHECK(AcSegmentHits(90,49,100.9f,110,49,100.9f,100,50,100,.5f,512));  // within the tolerance
	CHECK(!AcSegmentHits(90,49,101.1f,110,49,101.1f,100,50,100,.5f,512));
	CHECK(!AcSegmentHits(90,50.2f,100,110,50.2f,100,100,50,100,0,512));   // over its head
	CHECK(AcSegmentHits(90,50.2f,100,110,50.2f,100,100,50,100,.5f,512));
	CHECK(!AcSegmentHits(90,47.9f,100,110,47.9f,100,100,50,100,0,512));   // under its feet
	CHECK(!AcSegmentHits(90,49,100,99,49,100,100,50,100,0,512));     // stops short of it
	CHECK(AcSegmentHits(90,49,100,99.6f,49,100,100,50,100,0,512));   // ends inside it
	CHECK(!AcSegmentHits(101,49,100,110,49,100,100,50,100,0,512));   // starts past it
	CHECK(AcSegmentHits(100,60,100,100,40,100,100,50,100,0,512));    // from above, through
	CHECK(AcSegmentHits(100.2f,49,100,100.2f,49,100,100,50,100,0,512));   // no length, inside
	CHECK(!AcSegmentHits(100.7f,49,100,100.7f,49,100,100,50,100,0,512));
	CHECK(AcSegmentHits(505,49,100,515,49,100,.5f,50,100,0,512));    // across the edge (512 = 0)
	CHECK(AcSegmentHits(508,49,100,509,49,100,509+512.f,50,100,0,512));   // re-wrapped player
	CHECK(!AcSegmentHits(90,49,100,110,49,110,100,50,100,0,512));    // diagonal, passes 5 off
	CHECK(AcSegmentHits(90,49,95,110,49,105,100,50,100,0,512));      // diagonal, through

	// M16 phase 5: where a player was at a tick, as a client draws it
	static AcTickRing tr;
	float at[3];
	memset(&tr,0,sizeof(tr));
	CHECK(!AcTickAt(&tr,100,0,at,512,8));                            // nothing kept
	for(unsigned int t=100;t<200;t++) AcTickAdd(&tr,t,510+(t-100)*.5f,50,100,t!=150);   // .5 a tick on x, dead at 150
	CHECK(!AcTickAt(&tr,100,0,at,512,8));                            // 64 ticks kept: 100 is gone
	CHECK(AcTickAt(&tr,140,0,at,512,8)&&fabsf(at[0]-530)<1e-4f);
	CHECK(AcTickAt(&tr,140,.5f,at,512,8)&&fabsf(at[0]-530.25f)<1e-4f);  // between two ticks
	CHECK(!AcTickAt(&tr,150,0,at,512,8));                            // dead then: not there
	CHECK(AcTickAt(&tr,149,.5f,at,512,8)&&fabsf(at[0]-534.5f)<1e-4f);   // the next one dead: held
	CHECK(AcTickAt(&tr,199,.5f,at,512,8)&&fabsf(at[0]-559.5f)<1e-4f);   // the newest: held
	CHECK(!AcTickAt(&tr,200,0,at,512,8));                            // not yet
	AcTickAdd(&tr,200,511.9f+512,50,100,1);
	AcTickAdd(&tr,201,.1f,50,100,1);                                 // across the edge of the world
	CHECK(AcTickAt(&tr,200,.5f,at,512,8)&&fabsf(AcWrap(at[0]-512,512))<1e-3f);
	AcTickAdd(&tr,202,200,50,100,1);                                 // a respawn: not across it
	CHECK(AcTickAt(&tr,201,.9f,at,512,8)&&fabsf(at[0]-.1f)<1e-4f);
	// how far back a shot goes: from the server's last tick to what the shooter drew
	CHECK(fabsf(AcShotLag(1000,980,128,30)-19.5f)<1e-4f);
	CHECK(AcShotLag(1000,900,0,30)==30);                            // at most ac_lagcomp
	CHECK(AcShotLag(1000,1003,0,30)==0&&AcShotLag(1000,1000,0,30)==0);  // ahead: not rewound
	CHECK(AcShotLag(1000,980,0,0)==0);                              // ac_lagcomp 0: no rewind
	CHECK(fabsf(AcShotLag(5,0xFFFFFFFEu,0,30)-7)<1e-4f);            // across the wrap of tick numbers

	// stable slots: the first free one, numplayers after a leave, renumbering
	char used[8]={1,1,0,1,0,0,0,0};
	CHECK(SlotFirstFree(used,1,8)==2);                               // a hole first
	used[2]=1;
	CHECK(SlotFirstFree(used,1,8)==4);                               // else after the last
	char full[3]={1,1,1};
	CHECK(SlotFirstFree(full,1,3)==-1);
	char trim[6]={1,1,0,1,0,0};
	CHECK(SlotTrim(trim,6)==4);                                      // trailing free ones dropped
	trim[3]=0;
	CHECK(SlotTrim(trim,4)==2);
	char alone[2]={1,0};
	CHECK(SlotTrim(alone,2)==1);                                     // the host stays
	CHECK(SlotRenumber(3,2,0)==3&&SlotRenumber(1,2,0)==1);           // kept slots: nobody moves
	CHECK(SlotRenumber(3,2,1)==2&&SlotRenumber(1,2,1)==1);           // compacted: after it, down one
	CHECK(SlotRenumber(2,2,0)==-1&&SlotRenumber(2,2,1)==-1);         // the leaver itself

	if(failures) printf("%d failure(s)\n",failures);
	else printf("anticheat: all checks passed\n");
	return failures?1:0;
}
