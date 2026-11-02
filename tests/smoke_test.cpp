// Unit test of src/game/smoke.h (smoke grenades): the cloud's radius and opacity
// over its life, its lumps and sprites (the same on every client, inside the
// cloud), a flight that lands, and the state a server object carries on the wire,
// hidden from clients without HELLOCAP_SMOKE.
#include "smoke.h"
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

int main()
{
	// radius: nothing before landing, grows, nearly full in 4 s, spreads at the end
	CHECK(SmokeRadius(-1)==0);
	CHECK(SmokeRadius(0)==0);
	float last=0;
	for(float t=.1f;t<SMOKE_LIFETIME;t+=.1f)
	{
		float r=SmokeRadius(t);
		CHECK(r>=last);
		CHECK(r<=SMOKE_RADIUS*1.16f);
		last=r;
	}
	CHECK(SmokeRadius(4)>SMOKE_RADIUS*.85f);
	CHECK(SmokeRadius(SMOKE_LIFETIME-.01f)>SMOKE_RADIUS*1.1f);

	// opacity: thick within a second, whole in mid-life, thinning out, then gone
	CHECK(SmokeStrength(0)==0);
	CHECK(SmokeStrength(1)==1);
	CHECK(SmokeStrength(SMOKE_LIFETIME/2)==1);
	CHECK(SmokeStrength(SMOKE_LIFETIME-SMOKE_FADE/2)>.4f&&SmokeStrength(SMOKE_LIFETIME-SMOKE_FADE/2)<.6f);
	CHECK(SmokeStrength(SMOKE_LIFETIME)==0);
	CHECK(SmokeStrength(SMOKE_LIFETIME+5)==0);

	// parts: deterministic, inside the cloud, above the ground, of a sensible size
	for(int slot=0;slot<100;slot+=7)
		for(int k=0;k<SMOKE_SPRITES;k++)
		{
			float a[4],b[4];
			SmokePart(slot,k,SMOKE_SPRITES,6,3,a);
			SmokePart(slot,k,SMOKE_SPRITES,6,3,b);
			CHECK(a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3]);
			CHECK(sqrtf(a[0]*a[0]+a[2]*a[2])<=6*.61f);
			CHECK(a[1]>0&&a[1]<=6*.61f);
			CHECK(a[3]>=6*.54f&&a[3]<=6*.71f);
		}
	{
		// the first lump sits in the middle, the others go around
		float p[4];
		SmokePart(3,0,SMOKE_LUMPS,7,1,p);
		CHECK(p[0]==0&&p[2]==0);
		int quadrants[4]={0,0,0,0};
		for(int k=1;k<SMOKE_LUMPS;k++)
		{
			SmokePart(3,k,SMOKE_LUMPS,7,1,p);
			quadrants[(p[0]>0)+2*(p[2]>0)]++;
		}
		CHECK(quadrants[0]&&quadrants[1]&&quadrants[2]&&quadrants[3]);
	}

	// flight: thrown up and forward over flat ground at 10, it lands and stops there
	{
		float pos[3]={0,12,0},vel[3]={1,.5f,0};
		int landed=0,steps=0;
		while(!landed&&steps<10000)
		{
			landed=SmokeFly(pos,vel,.1f,.05f,10);
			steps++;
		}
		CHECK(landed);
		CHECK(pos[1]==10);
		CHECK(pos[0]>5);
		CHECK(vel[0]==0&&vel[1]==0&&vel[2]==0);
		// a step on the ground doesn't move it again
		CHECK(SmokeFly(pos,vel,.1f,.05f,10)==1);
		CHECK(pos[1]==10);
	}

	// the wire: flying, then landed with its age; what is left of its life
	{
		float d[3];
		SmokeEncode(d,0,0);
		CHECK(!SmokeLanded(d));
		SmokeEncode(d,1,5);
		CHECK(SmokeLanded(d));
		CHECK(SmokeRemainingMs(d)==(int)((SMOKE_LIFETIME-5)*1000));
		SmokeEncode(d,1,-3);   // a clock behind: never a negative age
		CHECK(d[1]==0);
		d[1]=SMOKE_LIFETIME+1;
		CHECK(SmokeRemainingMs(d)==0);
		d[1]=NAN;
		CHECK(SmokeRemainingMs(d)==0);
		d[1]=-1;
		CHECK(SmokeRemainingMs(d)==0);
	}

	// older clients never see one; everything else goes to everyone
	CHECK(SmokeHiddenFrom(SMOKECLOUD,0));
	CHECK(!SmokeHiddenFrom(SMOKECLOUD,1));
	for(int type=0;type<SMOKECLOUD;type++) CHECK(!SmokeHiddenFrom(type,0));

	if(failures) printf("%d failure(s)\n",failures);
	else printf("smoke_test: all passed\n");
	return failures?1:0;
}
