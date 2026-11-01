// Unit test of src/ui/inputlogic.h: the gamepad's dead zone and response curve, a
// stick as movement and look, the menus' repeat and focus moves.
#include "inputlogic.h"
#include <stdio.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)
#define NEAR(a,b) (fabsf((a)-(b))<1e-4f)

int main()
{
	// dead zone: nothing inside, rescaled outside, full at the edge, direction kept
	float x,y;
	InStick(.1f,.1f,.2f,1,&x,&y);
	CHECK(x==0&&y==0);
	InStick(.2f,0,.2f,1,&x,&y);
	CHECK(x==0&&y==0);                      // on the edge of the dead zone: still 0
	InStick(.6f,0,.2f,1,&x,&y);
	CHECK(NEAR(x,.5f)&&y==0);               // half way between the dead zone and the edge
	InStick(0,-1,.2f,1,&x,&y);
	CHECK(x==0&&NEAR(y,-1));
	InStick(1,1,.2f,1,&x,&y);               // a corner (past the circle): clamped to 1
	CHECK(NEAR(sqrtf(x*x+y*y),1)&&NEAR(x,y));
	InStick(.6f,0,.2f,2,&x,&y);             // curve 2: half way gives a quarter
	CHECK(NEAR(x,.25f));
	InStick(-.6f,0,.2f,2,&x,&y);            // and keeps the sign
	CHECK(NEAR(x,-.25f));
	InStick(.5f,.5f,1,1,&x,&y);             // a dead zone of 1 (a broken setting): off
	CHECK(x==0&&y==0);
	// small moves just past the dead zone start from 0, not from the dead zone
	InStick(.21f,0,.2f,1,&x,&y);
	CHECK(x>0&&x<.02f);

	// triggers: hysteresis
	CHECK(!InTrigger(.4f,0)&&InTrigger(.6f,0));
	CHECK(InTrigger(.4f,1)&&!InTrigger(.3f,1));

	// stick as movement (y down: -1 is forward)
	InMove m=InStickMove(0,-1);
	CHECK(m.up&&!m.down&&!m.left&&!m.right&&NEAR(m.fwd,1)&&NEAR(m.strafe,0));
	m=InStickMove(0,.5f);
	CHECK(m.down&&!m.up&&NEAR(m.fwd,-.5f));
	m=InStickMove(.7f,-.7f);                // diagonal: both flags
	CHECK(m.up&&m.right&&!m.left&&!m.down);
	m=InStickMove(.2f,-.9f);                // mostly forward: no strafe flag, but a little strafe
	CHECK(m.up&&!m.right&&m.strafe>.19f);
	m=InStickMove(-1,0);
	CHECK(m.left&&!m.up&&!m.down&&NEAR(m.strafe,-1));
	m=InStickMove(0,0);
	CHECK(!m.up&&!m.down&&!m.left&&!m.right&&m.fwd==0&&m.strafe==0);
	m=InStickMove(3,4);                     // never faster than a full push
	CHECK(NEAR(sqrtf(m.fwd*m.fwd+m.strafe*m.strafe),1));

	// fractional look counts carry over
	float acc=0;
	int total=0;
	for(int i=0;i<10;i++) total+=InTakeCounts(&acc,.25f);
	CHECK(total==2&&NEAR(acc,.5f));
	acc=0;
	total=0;
	for(int i=0;i<10;i++) total+=InTakeCounts(&acc,-.3f);
	CHECK(total==-3&&fabsf(acc)<.01f);

	// menu repeat: at once, after the delay, then at the rate; released: again at once
	InRepeat rep={0,0};
	CHECK(InRepeatStep(&rep,1,1000)==1);
	CHECK(InRepeatStep(&rep,1,1200)==0);
	CHECK(InRepeatStep(&rep,1,1380)==1);
	CHECK(InRepeatStep(&rep,1,1400)==0);
	CHECK(InRepeatStep(&rep,1,1490)==1);
	CHECK(InRepeatStep(&rep,0,1500)==0);
	CHECK(InRepeatStep(&rep,1,1501)==1);

	// focus: a column of rows with two tabs above and two buttons below
	InRect r[]=
	{
		{-.5f,.79f,-.22f,.88f},   // 0 tab 1
		{-.2f,.79f,.08f,.88f},    // 1 tab 2
		{-.89f,.645f,.91f,.72f},  // 2 row 0
		{-.89f,.57f,.91f,.645f},  // 3 row 1
		{-.89f,.495f,.91f,.57f},  // 4 row 2
		{.22f,-.87f,.52f,-.76f},  // 5 Back
		{.6f,-.87f,.9f,-.76f},    // 6 Start
	};
	int n=sizeof(r)/sizeof(r[0]);
	CHECK(InFocusFirst(r,n)==0);
	CHECK(InFocusNext(r,n,r[0],IN_RIGHT)==1);
	CHECK(InFocusNext(r,n,r[1],IN_RIGHT)==-1);
	CHECK(InFocusNext(r,n,r[1],IN_LEFT)==0);
	CHECK(InFocusNext(r,n,r[0],IN_DOWN)==2);
	CHECK(InFocusNext(r,n,r[2],IN_DOWN)==3);  // the next row, not a button
	CHECK(InFocusNext(r,n,r[3],IN_UP)==2);
	CHECK(InFocusNext(r,n,r[4],IN_DOWN)==5||InFocusNext(r,n,r[4],IN_DOWN)==6);
	CHECK(InFocusNext(r,n,r[5],IN_RIGHT)==6);
	CHECK(InFocusNext(r,n,r[6],IN_LEFT)==5);
	CHECK(InFocusNext(r,n,r[6],IN_UP)==4);
	CHECK(InFocusNext(r,n,r[4],IN_LEFT)==-1);   // nothing beside a full-width row
	// a grid: right goes along the row even if a row below is closer to the side
	InRect g[]=
	{
		{-.9f,.0f,-.5f,.1f},{-.4f,.0f,0,.1f},{.1f,.0f,.5f,.1f},
		{-.9f,-.12f,-.5f,-.02f},{-.4f,-.12f,0,-.02f},
	};
	CHECK(InFocusNext(g,5,g[0],IN_RIGHT)==1);
	CHECK(InFocusNext(g,5,g[1],IN_RIGHT)==2);
	CHECK(InFocusNext(g,5,g[1],IN_DOWN)==4);
	CHECK(InFocusNext(g,5,g[2],IN_DOWN)==4);    // the nearest below, though not under it
	CHECK(InFocusNext(g,5,g[3],IN_UP)==0);
	CHECK(InSameRect(g[0],g[0])&&!InSameRect(g[0],g[1]));
	CHECK(InFocusFirst(r,0)==-1);

	if(failures)
	{
		printf("%d failure(s)\n",failures);
		return 1;
	}
	printf("input: all tests passed\n");
	return 0;
}
