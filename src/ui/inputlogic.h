//gamepad and touch input: the pure logic, apart from gamepad.cpp, touch.cpp and the
//menus' focus (ui.cpp) so that tests/input_test.cpp can check it without SDL or a
//window. Sticks are -1..1 with y down, as SDL reports them; menu rects are in the
//menus' units (-1..1 from the window center, y up, like mousepos); the touch layout
//is in window points from the top left, like SDL's finger positions times the size.

#ifndef INPUTLOGICH
#define INPUTLOGICH

#include <math.h>

//---- sticks and triggers ----

//radial dead zone, rescaled so that the stick goes from 0 just past it to 1 at its
//edge (no jump at the dead zone), then the response curve: magnitude^curve (1 is
//linear, 2 gives finer aim near the center). The direction is kept.
inline void InStick(float x,float y,float deadzone,float curve,float *ox,float *oy)
{
	float m=sqrtf(x*x+y*y);
	if(m<=deadzone||deadzone>=1)
	{
		*ox=*oy=0;
		return;
	}
	float r=(m-deadzone)/(1-deadzone);
	if(r>1) r=1;
	if(curve>0&&curve!=1) r=powf(r,curve);
	*ox=x/m*r;
	*oy=y/m*r;
}

//a trigger (0..1) presses past half way and releases under a third: no chatter
//around a single threshold
inline int InTrigger(float v,int wasdown)
{
	return wasdown?v>.33f:v>.5f;
}

//a stick (after InStick) as movement: the analog amounts the local player walks by,
//and the four key flags the rest of the game and the wire know (Player up, down,
//lstrafe, rstrafe: footsteps, friction, the remote players' legs)
struct InMove
{
	float fwd,strafe;      //-1..1, forward and right positive
	char up,down,left,right;
};

inline InMove InStickMove(float x,float y)
{
	InMove m;
	float mag=sqrtf(x*x+y*y);
	if(mag>1)
	{
		x/=mag;
		y/=mag;
		mag=1;
	}
	m.fwd=-y;
	m.strafe=x;
	//an axis counts when the stick is within 67.5 degrees of it: eight sectors, as
	//the keys give (a mostly forward push isn't also a strafe)
	float t=mag*.38f;
	m.up=mag>0&&m.fwd>t;
	m.down=mag>0&&m.fwd<-t;
	m.right=mag>0&&m.strafe>t;
	m.left=mag>0&&m.strafe<-t;
	return m;
}

//adds a fractional amount (mouse counts of a look) and takes its whole part, the
//rest waits for the next frame: slow stick turns aren't rounded away
inline int InTakeCounts(float *acc,float add)
{
	*acc+=add;
	int n=(int)*acc;   //toward 0, for both signs
	*acc-=n;
	return n;
}

//repeat of a held direction in the menus: the first step at once, the next after
//delay ms, then one every rate ms; returns the steps due at now (ms)
struct InRepeat
{
	int held;
	unsigned int next;
};

inline int InRepeatStep(InRepeat *r,int down,unsigned int now,unsigned int delay=380,unsigned int rate=110)
{
	if(!down)
	{
		r->held=0;
		return 0;
	}
	if(!r->held)
	{
		r->held=1;
		r->next=now+delay;
		return 1;
	}
	if((int)(now-r->next)<0) return 0;
	r->next=now+rate;
	return 1;
}

//---- focus of the menus (pad and keyboard) ----

struct InRect
{
	float x1,y1,x2,y2;
};

enum { IN_LEFT, IN_RIGHT, IN_UP, IN_DOWN };

//gap between two ranges (0 when they overlap)
inline float InGap(float a1,float a2,float b1,float b2)
{
	if(b1>a2) return b1-a2;
	if(a1>b2) return a1-b2;
	return 0;
}

//one pass of InFocusNext; cone: only what is beside cur or within an angle
inline int InFocusNextPass(const InRect *r,int n,InRect cur,int dir,int cone)
{
	float cx=(cur.x1+cur.x2)/2,cy=(cur.y1+cur.y2)/2;
	int best=-1;
	float bestscore=0;
	for(int i=0;i<n;i++)
	{
		float x=(r[i].x1+r[i].x2)/2,y=(r[i].y1+r[i].y2)/2;
		float along,side,sidegap;
		switch(dir)
		{
		case IN_LEFT: along=cx-x; side=y-cy; sidegap=InGap(cur.y1,cur.y2,r[i].y1,r[i].y2); break;
		case IN_RIGHT: along=x-cx; side=y-cy; sidegap=InGap(cur.y1,cur.y2,r[i].y1,r[i].y2); break;
		case IN_UP: along=y-cy; side=x-cx; sidegap=InGap(cur.x1,cur.x2,r[i].x1,r[i].x2); break;
		default: along=cy-y; side=x-cx; sidegap=InGap(cur.x1,cur.x2,r[i].x1,r[i].x2); break;
		}
		if(along<=.005f) continue;   //not that way (or cur itself)
		//sideways moves are stricter (27 degrees): rows are wide, their neighbours far
		if(cone&&sidegap>0&&fabsf(side)>along*(dir==IN_LEFT||dir==IN_RIGHT?.5f:1)) continue;
		//along: from edge to edge, so a wide neighbour isn't farther than a narrow one
		float edge=dir==IN_LEFT||dir==IN_RIGHT?InGap(cur.x1,cur.x2,r[i].x1,r[i].x2):InGap(cur.y1,cur.y2,r[i].y1,r[i].y2);
		float score=edge+along*.1f+sidegap*3+fabsf(side)*.3f;
		if(best<0||score<bestscore)
		{
			best=i;
			bestscore=score;
		}
	}
	return best;
}

//the rect to go to from cur in a direction, -1 when none: one whose center is that
//way, nearest along the direction; off to the side costs more (three times its gap,
//and a little of the centers' offset), so the focus stays in a row or a column.
//Only what is beside cur or within an angle counts, except up and down when
//nothing is (the next row may be shorter): Right on the last tab goes nowhere.
inline int InFocusNext(const InRect *r,int n,InRect cur,int dir)
{
	int best=InFocusNextPass(r,n,cur,dir,1);
	if(best<0&&(dir==IN_UP||dir==IN_DOWN)) best=InFocusNextPass(r,n,cur,dir,0);
	return best;
}

//where the focus starts on a screen: the top-most rect, the left-most of a row
inline int InFocusFirst(const InRect *r,int n)
{
	int best=-1;
	for(int i=0;i<n;i++)
		if(best<0||r[i].y2>r[best].y2+.01f||(fabsf(r[i].y2-r[best].y2)<=.01f&&r[i].x1<r[best].x1))
			best=i;
	return best;
}

//the same rect as before (the screen drew it again): centers within a hair
inline int InSameRect(InRect a,InRect b)
{
	return fabsf(a.x1-b.x1)<.002f&&fabsf(a.x2-b.x2)<.002f&&fabsf(a.y1-b.y1)<.002f&&fabsf(a.y2-b.y2)<.002f;
}

//---- touch controls (an on-screen gamepad) ----

enum
{
	TB_FIRE,TB_JUMP,TB_JETPACK,TB_RELOAD,TB_WEAPON,TB_ZOOM,TB_CLAIM,   //right thumb
	TB_BUILD,TB_SCORES,TB_MENU,                                        //top corners
	TB_COUNT
};

struct InCircle
{
	float x,y,r;   //window points from the top left
};

struct InTouchLayout
{
	float w,h;          //window, in points
	float scale;        //what the sizes were multiplied by
	InCircle stick;     //the move stick's base when no finger holds it
	float stickzone;    //a finger down left of this x (not on a button) holds the stick
	InCircle b[TB_COUNT];
};

//sizes in window points (CSS pixels on the web), which the system already scales to
//the screen's density: the fire button is about 14 mm across on a phone. size is
//the player's setting (1 = 100%); a small window shrinks everything to fit.
inline void InTouchLayoutFor(float w,float h,float size,InTouchLayout *l)
{
	float s=size;
	float fit=fminf(w/640,h/330);
	if(fit<1) s*=fit;
	l->w=w;
	l->h=h;
	l->scale=s;
	l->stick.x=110*s;
	l->stick.y=h-110*s;
	l->stick.r=60*s;
	l->stickzone=w*.4f;
	//right thumb, from the bottom right corner: fire biggest and nearest
	static const float right[7][3]=
	{
		{92,92,44},     //fire
		{200,52,30},    //jump
		{52,200,30},    //jetpack
		{180,140,24},   //reload
		{140,205,24},   //weapon
		{52,290,24},    //zoom
		{280,52,24},    //claim
	};
	for(int i=0;i<7;i++)
	{
		l->b[i].x=w-right[i][0]*s;
		l->b[i].y=h-right[i][1]*s;
		l->b[i].r=right[i][2]*s;
	}
	l->b[TB_BUILD].x=w-90*s;
	l->b[TB_BUILD].y=34*s;
	l->b[TB_SCORES].x=w-34*s;
	l->b[TB_SCORES].y=34*s;
	l->b[TB_MENU].x=34*s;
	l->b[TB_MENU].y=84*s;   //under the frame counter
	for(int i=TB_BUILD;i<TB_COUNT;i++) l->b[i].r=22*s;
}

//the button under a finger, -1 for none: the nearest one whose circle (a little
//bigger than drawn, fingers are wide) holds the point
inline int InTouchHit(const InTouchLayout *l,float x,float y)
{
	int best=-1;
	float bestd=0;
	for(int i=0;i<TB_COUNT;i++)
	{
		float dx=x-l->b[i].x,dy=y-l->b[i].y;
		float d=sqrtf(dx*dx+dy*dy);
		if(d<=l->b[i].r*1.2f&&(best<0||d/l->b[i].r<bestd))
		{
			best=i;
			bestd=d/l->b[i].r;
		}
	}
	return best;
}

//a finger at x,y on a stick of center cx,cy and radius r: -1..1 (y down), clamped to
//the circle
inline void InTouchStick(float cx,float cy,float r,float x,float y,float *sx,float *sy)
{
	float dx=(x-cx)/r,dy=(y-cy)/r;
	float m=sqrtf(dx*dx+dy*dy);
	if(m>1)
	{
		dx/=m;
		dy/=m;
	}
	*sx=dx;
	*sy=dy;
}

//where a floating stick goes for a finger down at x,y: there, but kept whole on the
//window
inline void InTouchStickCenter(const InTouchLayout *l,float x,float y,float *cx,float *cy)
{
	float r=l->stick.r;
	*cx=fminf(fmaxf(x,r),l->w-r);
	*cy=fminf(fmaxf(y,r),l->h-r);
}


#endif
