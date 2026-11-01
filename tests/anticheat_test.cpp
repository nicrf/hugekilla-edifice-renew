// Unit test of src/net/anticheat.h (roadmap M11 phase 1): rate limits, wrapped
// distances, position speed, build distance, the server's magazine count and
// fire rate, and the damage rules the server copies from TickObjects.
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

	// position speed: needs a window, follows the wrap
	AcMove m;
	memset(&m,0,sizeof(m));
	CHECK(AcMoveSpeed(&m,10,50,10,1000,250,512)<0);       // reference
	CHECK(AcMoveSpeed(&m,20,50,10,1100,250,512)<0);       // window not over
	float sp=AcMoveSpeed(&m,20,50,10,1500,250,512);       // 10 units in .5 s
	CHECK(fabsf(sp-20)<.01f);
	sp=AcMoveSpeed(&m,500,50,10,2000,250,512);            // 32 units across the edge
	CHECK(fabsf(sp-64)<.01f);
	sp=AcMoveSpeed(&m,244,50,10,2300,250,512);            // a teleport
	CHECK(sp>500);

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

	if(failures) printf("%d failure(s)\n",failures);
	else printf("anticheat: all checks passed\n");
	return failures?1:0;
}
