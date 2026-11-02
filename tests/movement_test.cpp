// Unit test of a player's move (src/game/movement.h, docs/M16.md): on a made-up
// land (flat ground, a wall, a lake), falling, landing, walking, strafing, the
// stick, jumping, the jetpack (and not when dead), gliding, swimming, walls; the
// events for the sounds; and the same steps giving the same result to the bit
// (the server and a predicting client must agree, phases 2 and 3).
#include "movement.h"
#include "land.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

#define GROUND 30.0f       //above the water (WATERHEIGHT 23)
#define WALLX 720.0f       //a 6 high wall from there on x
#define LAKEZ 900.0f       //the ground drops to 10 beyond (under water)
#define STEP (1000.0f/60/40)   //a 60 Hz tick's sf (phase 1)

static float Height(Vector p)
{
	if(p.z>LAKEZ) return 10;
	if(p.x>=WALLX) return GROUND+6;
	return GROUND;
}
static int pushes;
static int Push(Vector &,Vector &,float,float,float) { pushes++; return 0; }
static const MoveWorld world={Height,Push};

static ServerVars Vars()
{
	ServerVars sv;
	memset(&sv,0,sizeof(sv));
	//data/sv.ini
	sv.walkaccelerate=.12f;
	sv.strafeaccelerate=.09f;
	sv.resistance=1.4f;
	sv.stopresistance=1.6f;
	sv.gravity=.04f;
	sv.chopperthrust=1;
	sv.thrustrechargerate=.007f;
	sv.thrustuse=.02f;
	return sv;
}
static const ServerVars vars=Vars();

static Player Standing(float x,float z)
{
	Player p;
	memset(&p,0,sizeof(p));
	p.pos=Vector(x,GROUND+1.45f,z);
	p.fwd=Vector(0,0,1);
	p.tilt=Vector(1,0,0);   //level
	p.dir=Vector(0,0,1);
	p.playstate=10;
	p.thrustfuel=1;
	return p;
}

static MoveEvents Step(Player *p,int n=1,float sf=STEP,MoveInput in=MoveInput())
{
	MoveEvents ev;
	for(int i=0;i<n;i++)
	{
		AimPlayer(p);
		MovePlayer(p,in,sf,vars,world,&ev);
	}
	return ev;
}

static float Horizontal(const Vector &v) { return sqrtf(v.x*v.x+v.z*v.z); }

int main()
{
	//falls from 20 above and lands on the ground, at rest
	{
		Player p=Standing(600,600);
		p.pos.y=GROUND+20;
		Step(&p,60);
		CHECK(p.pos.y<GROUND+15);
		Step(&p,300);
		//held at 1.45 above the ground, then the step's last bit of falling
		CHECK(p.pos.y>GROUND+1.4f&&p.pos.y<GROUND+1.5f);
		CHECK(fabsf(p.vel.y)<.05f);
		CHECK(Horizontal(p.vel)<1e-4f);
	}
	//walks forward along fwd, at a steady speed
	{
		Player p=Standing(600,600);
		p.up=1;
		MoveEvents ev=Step(&p,120);
		CHECK(ev.walked&&ev.thrust==0&&!ev.splash);
		CHECK(p.pos.z>605&&fabsf(p.pos.x-600)<1e-3f);
		float v1=p.vel.z;
		Step(&p,60);
		CHECK(fabsf(p.vel.z-v1)<1e-3f);   //terminal
		float speed=p.vel.z/(40/1000.0f);   //units a second
		CHECK(speed>5&&speed<12);
		printf("walking speed %.3f units/s\n",speed);
		//lets go: stops
		p.up=0;
		ev=Step(&p,120);
		CHECK(!ev.walked);
		CHECK(Horizontal(p.vel)<1e-3f);
		//backwards and strafing go their way
		Player b=Standing(600,600);
		b.down=1;
		Step(&b,60);
		CHECK(b.pos.z<599);
		Player s=Standing(600,600);
		s.rstrafe=1;
		Step(&s,60);
		CHECK(fabsf(s.pos.x-600)>1&&fabsf(s.pos.z-600)<1e-3f);
	}
	//the stick: half pushed walks slower than the key, and only for the local player
	{
		Player k=Standing(600,600),h=Standing(600,600);
		k.up=h.up=1;
		MoveInput in;
		in.local=in.pad=1;
		in.padfwd=.5f;
		Step(&k,120);
		Step(&h,120,STEP,in);
		CHECK(h.pos.z>600.5f&&h.pos.z<k.pos.z-1);
	}
	//a frame rate of 30 instead of 60 goes about as far (the old variable step)
	{
		Player a=Standing(600,600),b=Standing(600,600);
		a.up=b.up=1;
		Step(&a,120);
		Step(&b,60,STEP*2);
		CHECK(fabsf(a.pos.z-b.pos.z)<.25f*(a.pos.z-600));
	}
	//jumps from the ground, once
	{
		Player p=Standing(600,600);
		p.jump=1;
		Step(&p);
		CHECK(p.jump==0);
		CHECK(p.vel.y>.3f);
		Step(&p,10);
		CHECK(p.pos.y>GROUND+2);
		Step(&p,200);
		CHECK(p.pos.y<GROUND+1.5f);
		//dead (waiting to respawn): no jump
		Player d=Standing(600,600);
		d.playstate=11;
		d.jump=1;
		Step(&d);
		CHECK(d.vel.y<=0);
	}
	//the jetpack lifts and burns fuel, which comes back; not when dead
	{
		Player p=Standing(600,600);
		p.jetpack=1;
		MoveEvents ev=Step(&p,60);
		CHECK(ev.thrust==1);
		CHECK(p.pos.y>GROUND+3);
		CHECK(p.thrustfuel<1);
		CHECK(fabsf(ev.thrustpos.y-p.pos.y)<1&&Magnitude(ev.thrustdir)>0);
		p.jetpack=0;
		ev=Step(&p,400);
		CHECK(ev.thrust==0);
		CHECK(p.thrustfuel>.99f);
		Player d=Standing(600,600);
		d.playstate=11;
		d.jetpack=1;
		ev=Step(&d,60);
		CHECK(ev.thrust==0&&d.pos.y<GROUND+1.5f);
		//until the fuel runs out
		Player e=Standing(600,600);
		e.jetpack=1;
		Step(&e,2000);
		CHECK(e.thrustfuel<.05f);
	}
	//the glider: opens falling fast, holds the jetpack as it was, folds when let go
	{
		Player p=Standing(600,600);
		p.pos.y=GROUND+80;
		p.up=1;
		MoveInput in;
		in.local=1;
		p.hangglide=1;
		p.vel=Vector(0,-.5f,.3f);
		Step(&p,1,STEP,in);
		CHECK(p.hanggliding==1);
		MoveEvents ev=Step(&p,1,STEP,in);
		CHECK(ev.thrust==-1);
		Step(&p,30,STEP,in);
		CHECK(p.hanggliding==1&&p.pos.y>GROUND+40);
		p.hangglide=0;
		Step(&p,1,STEP,in);
		CHECK(p.hanggliding==0);
		//another player's comes from the network: it stays
		Player o=Standing(600,600);
		o.pos.y=GROUND+80;
		o.hanggliding=1;
		o.vel=Vector(0,-.5f,.3f);
		Step(&o,5);
		CHECK(o.hanggliding==1);
	}
	//a wall: walking into it stops at it
	{
		Player p=Standing(WALLX-3,600);
		p.fwd=Vector(1,0,0);
		p.up=1;
		Step(&p,300);
		CHECK(p.pos.x<WALLX&&p.pos.x>WALLX-1.5f);
		CHECK(p.pos.y<GROUND+1.5f);
	}
	//in the lake: sinks slowly, swims, splashes going in
	{
		Player p=Standing(600,LAKEZ+5);
		p.pos.y=WATERHEIGHT+.3f;
		p.vel.y=-2;
		MoveEvents ev=Step(&p);
		CHECK(ev.splash);
		Step(&p,120);
		CHECK(p.pos.y<WATERHEIGHT-.1f&&p.pos.y>10);
		float z=p.pos.z;
		p.up=1;
		Step(&p,60);
		CHECK(p.pos.z>z+1);
	}
	//landing hard: the thump
	{
		Player p=Standing(600,600);
		p.pos.y=GROUND+1.46f;
		p.vel.y=-1;
		MoveEvents ev=Step(&p);
		CHECK(ev.landed);
	}
	//the trunks are asked every step
	{
		pushes=0;
		Player p=Standing(600,600);
		Step(&p,10);
		CHECK(pushes==10);
	}
	//the same steps, the same result to the bit
	{
		Player a=Standing(600,600),b=Standing(600,600);
		for(int i=0;i<600;i++)
		{
			char keys=(char)((i*7)/50);
			a.up=b.up=keys&1;
			a.rstrafe=b.rstrafe=(keys>>1)&1;
			a.jetpack=b.jetpack=(keys>>2)&1;
			a.jump=b.jump=(i%97)==0;
			a.fwd=b.fwd=Vector(sinf(i*.01f),0,cosf(i*.01f));
			Step(&a);
			Step(&b);
		}
		CHECK(!memcmp(&a.pos,&b.pos,sizeof(a.pos))&&!memcmp(&a.vel,&b.vel,sizeof(a.vel)));
		CHECK(a.thrustfuel==b.thrustfuel&&a.roll==b.roll&&a.runtime==b.runtime);
	}
	if(failures) printf("%d failures\n",failures);
	else printf("movement: all passed\n");
	return failures?1:0;
}
