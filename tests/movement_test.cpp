// Unit test of a player's move (src/game/movement.h, docs/M16.md): on a made-up
// land (flat ground, a wall, a lake), falling, landing, walking, strafing, the
// stick, jumping, the jetpack (and not when dead), gliding, swimming, walls; the
// events for the sounds; and the same steps giving the same result to the bit
// (the server and a predicting client must agree, phases 2 and 3). Then the input
// commands (MoveCmd: keys, stick, view), the same commands giving the same state to
// the bit, and the client's prediction: kept, compared with the server's state, the
// commands since played again when it differs (src/game/predict.h).
#include "movement.h"
#include "predict.h"
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
	//---- input commands (docs/M16.md, phases 2 and 3) ----
	//a command carries the keys, the stick and the view
	{
		Player p=Standing(600,600);
		p.up=p.rstrafe=p.jetpack=p.hangglide=p.jump=1;
		p.fwd=Normalize(Vector(1,0,1));
		p.tilt=Normalize(Vector(1,.3f,0));
		p.weapon=4;
		MoveCmd c=MakeMoveCmd(&p,1,.5f,-1,7);
		CHECK(c.keys==(MOVEKEY_UP|MOVEKEY_RIGHT|MOVEKEY_JETPACK|MOVEKEY_GLIDE|MOVEKEY_JUMP|MOVEKEY_PAD));
		CHECK(c.padfwd==64&&c.padstrafe==-127);
		CHECK(c.weapon==4&&c.flags==MOVECMD_ALIVE);
		CHECK(c.msec==16||c.msec==17);
		Vector fwd,tilt,dir;
		MoveCmdView(c,&fwd,&tilt,&dir);
		CHECK(Magnitude(fwd-p.fwd)<1e-3f&&Magnitude(tilt-p.tilt)<1e-3f);
		CHECK(fabsf(Magnitude(dir)-1)<1e-5f&&dir.y>.2f);
		//a dead player asks to come back; 60 ticks last a second
		p.playstate=11;
		CHECK(MakeMoveCmd(&p,0,0,0,0).flags==0);
		int ms=0;
		for(unsigned int n=1000;n<1060;n++) ms+=MakeMoveCmd(&p,0,0,0,n).msec;
		CHECK(ms==1000);
		//the client's view stays its own (keepview), the server's is the command's
		Player cl=Standing(600,600),srv=Standing(600,600);
		cl.fwd=srv.fwd=Vector(0,0,1);
		MoveCmd turn=c;
		turn.yaw=16384;   //a quarter turn: +x
		MoveEvents ev;
		ApplyMoveCmd(&cl,turn,0,STEP,vars,world,&ev,1);
		ApplyMoveCmd(&srv,turn,0,STEP,vars,world,&ev,0);
		CHECK(cl.fwd.z==1&&srv.fwd.x>.99f);
		CHECK(!memcmp(&cl.pos,&srv.pos,sizeof(cl.pos))&&!memcmp(&cl.vel,&srv.vel,sizeof(cl.vel)));
	}
	//the same commands from the same state: the same state, to the bit, whatever
	//else differs (the aim's wobble, the view between ticks); across the world's edge
	{
		Player a=Standing(LANDGRIDSIZE*2-3,600),b=Standing(LANDGRIDSIZE*2-3,600);
		b.inaccuracy=Vector(.1f,.2f,.3f);
		b.dir=Vector(1,0,0);
		MoveState sa,sb;
		for(int i=0;i<900;i++)
		{
			MoveCmd c;
			memset(&c,0,sizeof(c));
			int k=(i*7)/50;
			c.keys=(unsigned char)((k&1?MOVEKEY_UP:0)|(k&2?MOVEKEY_RIGHT:0)|(k&4?MOVEKEY_JETPACK:0)|
				(i%97==0?MOVEKEY_JUMP:0)|(i>300&&i<400?MOVEKEY_GLIDE:0)|(i>600&&i<700?MOVEKEY_PAD:0));
			c.padfwd=(signed char)(i%255-127);
			c.padstrafe=40;
			c.yaw=(unsigned short)(i*300);
			c.pitch=(short)((i%200)*30-3000);
			c.flags=MOVECMD_ALIVE;
			MoveEvents ev;
			ApplyMoveCmd(&a,c,i>800?1:0,STEP,vars,world,&ev,0);
			b.fwd=Vector(1,0,0);   //the client's mouse in between: not the move's
			AimPlayer(&b);
			ApplyMoveCmd(&b,c,i>800?1:0,STEP,vars,world,&ev,1);
		}
		GetMoveState(&a,&sa);
		GetMoveState(&b,&sb);
		CHECK(!memcmp(&sa.pos,&sb.pos,sizeof(sa.pos))&&!memcmp(&sa.vel,&sb.vel,sizeof(sa.vel)));
		CHECK(sa.thrustfuel==sb.thrustfuel&&sa.roll==sb.roll&&sa.runtime==sb.runtime);
		CHECK(sa.legframe==sb.legframe&&sa.hanggliding==sb.hanggliding&&sa.jump==sb.jump);
		CHECK(a.pos.x>=LANDGRIDSIZE&&a.pos.x<=LANDGRIDSIZE*2+1);   //kept in the world's copy
	}
	//prediction: the server agrees, nothing changes; it differs, its state and the
	//commands since give what a straight run from it gives, to the bit
	{
		static Prediction pr;
		PredictReset(&pr);
		Player cl=Standing(600,600),srv=Standing(600,600);
		MoveState at[200];
		MoveCmd cmds[200];
		for(int i=0;i<200;i++)
		{
			MoveCmd &c=cmds[i];
			memset(&c,0,sizeof(c));
			c.keys=(unsigned char)(MOVEKEY_UP|(i>50&&i<80?MOVEKEY_JETPACK:0)|(i==120?MOVEKEY_JUMP:0));
			c.yaw=(unsigned short)(i*100);
			c.flags=MOVECMD_ALIVE;
			MoveEvents ev;
			PredictTick(&pr,&cl,1000+i,c,0,STEP,vars,world,&ev);
			ApplyMoveCmd(&srv,c,0,STEP,vars,world,&ev,0);
			GetMoveState(&srv,&at[i]);
		}
		CHECK(PredictFind(&pr,1199)&&PredictFind(&pr,1072)&&!PredictFind(&pr,1071));
		MoveState now;
		GetMoveState(&cl,&now);
		PredictResult r=PredictReconcile(&pr,&cl,1150,at[150],0,STEP,vars,world);
		CHECK(r.kind==PREDICT_OK&&r.poserr==0&&r.replayed==0);
		CHECK(!memcmp(&cl.pos,&now.pos,sizeof(now.pos)));
		//older than one compared already, or no longer kept: ignored
		CHECK(PredictReconcile(&pr,&cl,1140,at[140],0,STEP,vars,world).kind==PREDICT_STALE);
		CHECK(PredictReconcile(&pr,&cl,1060,at[60],0,STEP,vars,world).kind==PREDICT_STALE);
		CHECK(PredictReconcile(&pr,&cl,1300,at[150],0,STEP,vars,world).kind==PREDICT_STALE);
		//the server had it half a unit aside at 1160 (a push, a rounding of another machine)
		MoveState pushed=at[160];
		pushed.pos.x+=.5f;
		Player ref=Standing(600,600);
		SetMoveState(&ref,pushed);
		MoveEvents ev;
		for(int i=161;i<200;i++) ApplyMoveCmd(&ref,cmds[i],0,STEP,vars,world,&ev,1);
		cl.up=0;   //a key let go since the last tick: still let go after
		r=PredictReconcile(&pr,&cl,1160,pushed,0,STEP,vars,world);
		CHECK(r.kind==PREDICT_CORRECTED&&r.replayed==39);
		CHECK(fabsf(r.poserr-.5f)<1e-3f);
		CHECK(!memcmp(&cl.pos,&ref.pos,sizeof(ref.pos))&&!memcmp(&cl.vel,&ref.vel,sizeof(ref.vel)));
		CHECK(fabsf(r.shift.x-(ref.pos.x-now.pos.x))<1e-4f);
		CHECK(cl.up==0);
		//and the ring holds the new states: the server agreeing later is fine
		MoveState later;
		GetMoveState(&ref,&later);
		CHECK(PredictReconcile(&pr,&cl,1199,later,0,STEP,vars,world).kind==PREDICT_OK);
		//the same place in the next copy of the world (x + LANDGRIDSIZE): as predicted
		PredictTick(&pr,&cl,1200,cmds[0],0,STEP,vars,world,&ev);
		MoveState wrapped=pr.ring[1200%PREDICTRING].after;
		wrapped.pos.x+=LANDGRIDSIZE;
		CHECK(PredictReconcile(&pr,&cl,1200,wrapped,0,STEP,vars,world).kind==PREDICT_OK);
	}
	//the server's speed penalty (it gave us something to carry before we knew): the
	//commands after its state are played again with it, and the next ones made with it
	{
		static Prediction pr;
		PredictReset(&pr);
		Player cl=Standing(600,600),srv=Standing(600,600);
		MoveCmd c;
		memset(&c,0,sizeof(c));
		c.keys=MOVEKEY_UP;
		c.flags=MOVECMD_ALIVE;
		MoveEvents ev;
		MoveState at10;
		for(int i=0;i<20;i++)
		{
			PredictTick(&pr,&cl,i,c,0,STEP,vars,world,&ev);
			ApplyMoveCmd(&srv,c,i<10?0:1,STEP,vars,world,&ev,0);   //carries from 10 on
			if(i==10) GetMoveState(&srv,&at10);
		}
		MoveState now;
		GetMoveState(&srv,&now);
		PredictResult r=PredictReconcile(&pr,&cl,10,at10,1,STEP,vars,world);
		CHECK(r.kind==PREDICT_CORRECTED&&r.replayed==9);
		CHECK(!memcmp(&cl.pos,&now.pos,sizeof(now.pos))&&!memcmp(&cl.vel,&now.vel,sizeof(now.vel)));
		for(int i=20;i<40;i++)
		{
			PredictTick(&pr,&cl,i,c,0,STEP,vars,world,&ev);   //we still think we carry nothing
			ApplyMoveCmd(&srv,c,1,STEP,vars,world,&ev,0);
		}
		CHECK(!memcmp(&cl.pos,&srv.pos,sizeof(srv.pos))&&!memcmp(&cl.vel,&srv.vel,sizeof(srv.vel)));
		GetMoveState(&srv,&now);
		CHECK(PredictReconcile(&pr,&cl,39,now,1,STEP,vars,world).kind==PREDICT_OK);
	}
	if(failures) printf("%d failures\n",failures);
	else printf("movement: all passed\n");
	return failures?1:0;
}
