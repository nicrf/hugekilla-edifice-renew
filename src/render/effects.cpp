//Visual effects of M13.5 (effects.h): a pool of particles drawn as billboards, and
//short lights laid on the ground. Times are in seconds of the game clock (curtime,
//so HK_FIXEDCLOCK captures repeat), positions in world units (a block is 2, the
//player 1.5 tall).
//
//Glows (fire, flash, sparks) add their light (RBLEND_ADD): their order doesn't
//matter, and the fog color is set to black for them, so they fade into the fog
//instead of turning the fog's color. Solid things (debris, smoke) blend by alpha
//with the scene's fog, sorted back to front together, so a stone flies in front
//of or behind a puff as it should. None writes depth: the particles don't hide
//each other, and the scene behind them still tests against what was there.
//
//What is under water is drawn before the water (DrawEffects(0)) when the camera is
//above it, so the surface, drawn over, tints and hides it as it does the lake bed;
//the rest after (DrawEffects(1)). From under water, everything is drawn after: the
//surface is then above it all, and drawn first it would cover nearer bubbles (the
//particles write no depth).

#include "effects.h"
#include "renderer.h"
#include "textures.h"
#include "redrawing.h"
#include "hugekilla.h"
#include "playing.h"
#include "land.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

//---- textures ----

//procedural, so no data file: slots past the registry's file textures
enum
{
	TEX_FX_GLOW=TEX_FX0,    //soft round spot: flash, fire core, sparks' heads
	TEX_FX_FIRE,            //a puff of flame, noisy edges
	TEX_FX_STREAK,          //a soft line, along u: sparks (antialiased by the texture)
	TEX_FX_CHUNK,           //a jagged stone: debris
	TEX_FX_SMOKE,           //a soft, lumpy puff: smoke, dust, mist
	TEX_FX_RING,            //a thin ring: ripples on the water
	TEX_FX_BUBBLE           //a ring with a highlight: air bubbles
};

//value noise on a 2D lattice that wraps every period cells (seamless sprites)
static float Hash(int x,int y,int seed)
{
	unsigned h=(unsigned)x*374761393u+(unsigned)y*668265263u+(unsigned)seed*2246822519u;
	h=(h^(h>>13))*1274126177u;
	h^=h>>16;
	return (h&0xffff)/65535.f;
}

static float Noise(float x,float y,int period,int seed)
{
	int x0=(int)floorf(x),y0=(int)floorf(y);
	float fx=x-x0,fy=y-y0;
	fx=fx*fx*(3-2*fx);
	fy=fy*fy*(3-2*fy);
	int x1=x0+1,y1=y0+1;
	x0=((x0%period)+period)%period;
	y0=((y0%period)+period)%period;
	x1=((x1%period)+period)%period;
	y1=((y1%period)+period)%period;
	float a=Hash(x0,y0,seed),b=Hash(x1,y0,seed),c=Hash(x0,y1,seed),d=Hash(x1,y1,seed);
	return (a+(b-a)*fx)+((c+(d-c)*fx)-(a+(b-a)*fx))*fy;
}

//four octaves, 0..1
static float Fbm(float x,float y,int seed)
{
	float sum=0,amp=.5f,norm=0;
	int period=4;
	for(int i=0;i<4;i++)
	{
		sum+=Noise(x*period,y*period,period,seed+i)*amp;
		norm+=amp;
		amp*=.5f;
		period*=2;
	}
	return sum/norm;
}

static float Clamp01(float v)
{
	return v<0?0:v>1?1:v;
}

#define FXTEXSIZE 128

void FxLoadTextures()
{
	static unsigned char px[FXTEXSIZE*FXTEXSIZE*4];
	for(int tex=TEX_FX_GLOW;tex<=TEX_FX_BUBBLE;tex++)
	{
		for(int y=0;y<FXTEXSIZE;y++)
			for(int x=0;x<FXTEXSIZE;x++)
			{
				float u=(x+.5f)/FXTEXSIZE,v=(y+.5f)/FXTEXSIZE;
				float dx=u*2-1,dy=v*2-1;
				float r=sqrtf(dx*dx+dy*dy);
				float lum=1,alpha=0;
				switch(tex)
				{
				case TEX_FX_GLOW:
					alpha=Clamp01(1-r);
					alpha*=alpha;
					break;
				case TEX_FX_FIRE:
					{
						//a ragged ball: the noise eats into the edge, and darkens
						//the inside a little so it isn't a flat disc
						float n=Fbm(u,v,7);
						alpha=Clamp01((1-r)*1.6f-.25f+(n-.5f)*1.1f);
						alpha=alpha*alpha*(3-2*alpha);
						lum=.65f+.35f*Fbm(u+.37f,v+.11f,11);
					}
					break;
				case TEX_FX_STREAK:
					{
						//across (v): a gaussian; along (u): bright at the head (u=1),
						//fading to the tail, and rounded at both ends
						float across=expf(-dy*dy*6);
						float along=Clamp01(u*1.2f)*Clamp01((1-u)*12);
						alpha=across*along;
					}
					break;
				case TEX_FX_CHUNK:
					{
						//a hard, uneven outline (coarse noise on the radius), a
						//little shading across
						float n=Fbm(u*.5f,v*.5f,23);
						alpha=Clamp01(((1-r)-.35f+(n-.5f)*.9f)*12);
						lum=.75f+.25f*(dx-dy)*.5f+.2f*(Fbm(u,v,29)-.5f);
					}
					break;
				case TEX_FX_SMOKE:
					{
						//billows: a noisy outline over a denser core, shaded as
						//if lit from above (the noise sampled a little lower is
						//the shadow side of each lump)
						float n=Fbm(u,v,37);
						float edge=Clamp01(((1-r)+(n-.5f)*.9f-.18f)*2.6f);
						alpha=edge*edge*(3-2*edge)*(.6f+.4f*n);
						float below=Fbm(u,v-.04f,37);
						lum=Clamp01(.62f+.2f*dy+(n-below)*5+.25f*(n-.5f));
					}
					break;
				case TEX_FX_RING:
					{
						float d=(r-.78f)/.09f;
						alpha=expf(-d*d)+.15f*Clamp01(1-r)*Clamp01((r-.4f)*3);
					}
					break;
				case TEX_FX_BUBBLE:
					{
						//the rim, a faint inside, and the light caught at the top left
						float d=(r-.8f)/.12f;
						float hx=dx+.35f,hy=dy-.35f;
						alpha=expf(-d*d)+.2f*Clamp01(1-r)+expf(-(hx*hx+hy*hy)/.03f);
					}
					break;
				}
				unsigned char *p=&px[(y*FXTEXSIZE+x)*4];
				p[0]=p[1]=p[2]=(unsigned char)(lum*255+.5f);
				p[3]=(unsigned char)(Clamp01(alpha)*255+.5f);
			}
		TexUpload(tex,FXTEXSIZE,FXTEXSIZE,4,px,TEXF_MIPLINEAR);
	}
}

//---- particles ----

enum FxKind
{
	FX_FLASH,               //the blast's first light, a big glow for a few frames
	FX_FIRE,                //fireball puffs: expand, slow down, cool from white to red
	FX_SPARK,               //hot streaks thrown out, falling and bouncing
	FX_DEBRIS,              //dark chunks of earth, falling and bouncing
	FX_SMOKE,               //puffs that grow, rise, drift and thin out: smoke, dust, mist, silt
	FX_RING,                //a ripple flat on the water, widening
	FX_DROP,                //water thrown up by a splash, falling back
	FX_BUBBLE               //air rising under water, gone at the surface
};

struct FxParticle
{
	int kind;
	Vector pos,vel;
	float age,life;         //seconds; a negative age waits (smoke after the fire)
	float size0,size1;      //size at birth, at the end (half width, world units)
	float rot,spin;         //billboard rotation, radians and radians a second
	float r,g,b,a;
	Vector drift;           //smoke: the speed it settles to (rising air, wind)
};

struct FxLight
{
	Vector pos;
	float age,life,radius;
};

#define FXMAXPARTICLES 4096
#define FXMAXLIGHTS 32
static std::vector<FxParticle> parts;
static std::vector<FxLight> lights;

//its own random numbers: rand() is the game's (AI, respawn positions)
static unsigned fxrand=12345;
static float Rnd()
{
	fxrand=fxrand*1664525u+1013904223u;
	return (fxrand>>8)/16777216.f;
}

static float Rnd(float lo,float hi)
{
	return lo+(hi-lo)*Rnd();
}

//a random direction, uniform on the sphere
static Vector RndDir()
{
	float z=Rnd(-1,1),a=Rnd(0,6.2831853f),s=sqrtf(1-z*z);
	return Vector(s*cosf(a),z,s*sinf(a));
}

static FxParticle *Spawn(int kind,const Vector &pos,const Vector &vel,float life,float size0,float size1)
{
	if((int)parts.size()>=FXMAXPARTICLES) return 0;   //a burst too many: dropped
	FxParticle p;
	memset(&p,0,sizeof(p));
	p.kind=kind;
	p.pos=pos;
	p.vel=vel;
	p.life=life;
	p.size0=size0;
	p.size1=size1;
	p.rot=Rnd(0,6.2831853f);
	p.r=p.g=p.b=p.a=1;
	parts.push_back(p);
	return &parts.back();
}

//a puff of smoke: delay seconds before it shows, grey level, opacity
static void Smoke(const Vector &pos,const Vector &vel,float delay,float life,float size0,float size1,float grey,float alpha)
{
	FxParticle *p=Spawn(FX_SMOKE,pos,vel,life,size0,size1);
	if(!p) return;
	p->age=-delay;
	p->spin=Rnd(-.4f,.4f);
	p->r=p->g=p->b=grey;
	p->a=alpha;
	p->drift=Vector(.35f,.45f,.15f);
}

//dust, mist or silt: a puff of its own color that settles to drift
static void Puff(const Vector &pos,const Vector &vel,float life,float size0,float size1,float r,float g,float b,float alpha,const Vector &drift)
{
	FxParticle *p=Spawn(FX_SMOKE,pos,vel,life,size0,size1);
	if(!p) return;
	p->spin=Rnd(-.6f,.6f);
	p->r=r;
	p->g=g;
	p->b=b;
	p->a=alpha;
	p->drift=drift;
}

//the water's height at x, as DrawWater moves its waves (redrawing.cpp)
static float Surface(float x)
{
	return WaterSurfaceHeight(x);
}

void FxBubbles(const Vector &pos,int count,float spread)
{
	for(int i=0;i<count;i++)
	{
		Vector at=pos+RndDir()*Rnd(0,spread);
		if(at.y>Surface(at.x)-.05f) continue;
		FxParticle *p=Spawn(FX_BUBBLE,at,Vector(Rnd(-.2f,.2f),Rnd(.8f,1.8f),Rnd(-.2f,.2f)),Rnd(2,4),Rnd(.025f,.07f),0);
		if(p) p->spin=Rnd(4,9);   //the wobble's speed
	}
}

void FxBulletWake(const Vector &from,const Vector &to)
{
	if(Rnd()<.5f) FxBubbles(from+(to-from)*Rnd(),1,.03f);
}

void FxWaterSplash(const Vector &at,float strength)
{
	float y=Surface(at.x);
	Vector pos(at.x,y,at.z);
	float s=sqrtf(strength);
	int rings=strength>2?3:1;
	for(int i=0;i<rings;i++)
	{
		FxParticle *p=Spawn(FX_RING,pos,Vector(0,0,0),Rnd(.8f,1.2f)*s,.08f*s,Rnd(.9f,1.2f)*s);
		if(p) p->age=-i*.18f;
	}
	int drops=(int)(7*strength);
	for(int i=0;i<drops;i++)
	{
		float a=Rnd(0,6.2831853f),out=Rnd(.2f,1.3f)*s;
		Spawn(FX_DROP,pos+Vector(0,.05f,0),Vector(cosf(a)*out,Rnd(2.5f,5)*s,sinf(a)*out),2,Rnd(.015f,.03f)*s,0);
	}
	//spray: a little puff for a bullet, a column thrown up by a grenade
	int mists=strength>2?10:1;
	float up=strength>2?2.2f:1;
	for(int i=0;i<mists;i++)
		Puff(pos+Vector(Rnd(-.2f,.2f)*s,.1f,Rnd(-.2f,.2f)*s),Vector(0,Rnd(.5f,1.2f)*s*up,0),Rnd(.5f,.9f)*up,.08f*s,Rnd(.4f,.6f)*s,
			.85f,.9f,.95f,.45f,Vector(.2f,.25f,.1f));
}

void FxBulletImpact(const Vector &hit,const Vector &dir,int onblock)
{
	//the game finds the bullet already inside the ground or the wall: back out
	//along its way, so the puff isn't hidden in there
	Vector at=hit-dir*.25f;
	if(at.y<WATERHEIGHT)
	{
		//the lake bed: a cloud of silt that hangs, and the air the bullet brought
		Puff(at,Vector(0,.3f,0),Rnd(1.2f,1.8f),.08f,Rnd(.4f,.6f),.32f,.33f,.28f,.6f,Vector(0,.05f,0));
		FxBubbles(at,4,.15f);
		return;
	}
	//dust lighter than the ground it comes from (dry, in the light), or grey from a
	//wall: a quick burst and a slower cloud; and a few grains
	float r=.68f,g=.6f,b=.47f;
	if(onblock) r=g=b=.62f;
	Puff(at,Vector(Rnd(-.3f,.3f),Rnd(1.2f,1.8f),Rnd(-.3f,.3f))-dir*.6f,Rnd(.35f,.55f),.05f,Rnd(.35f,.5f),r,g,b,.9f,Vector(.1f,.3f,.05f));
	Puff(at,Vector(Rnd(-.2f,.2f),Rnd(.4f,.7f),Rnd(-.2f,.2f)),Rnd(1,1.5f),.1f,Rnd(.7f,1),r*.85f,g*.85f,b*.85f,.5f,Vector(.1f,.15f,.05f));
	for(int i=0;i<5;i++)
	{
		Vector d=RndDir()-dir;
		d.y=fabsf(d.y)+.5f;
		FxParticle *p=Spawn(FX_DEBRIS,at,Normalize(d)*Rnd(1.5f,3.5f),Rnd(.5f,.9f),Rnd(.03f,.05f),0);
		if(!p) continue;
		p->spin=Rnd(-10,10);
		p->r=r*.45f;
		p->g=g*.45f;
		p->b=b*.45f;
	}
	if(onblock)
		for(int i=0;i<3;i++)   //metal and stone spark
		{
			Vector d=RndDir();
			d.y=fabsf(d.y);
			Spawn(FX_SPARK,at,Normalize(d)*Rnd(4,8),Rnd(.12f,.25f),.025f,0);
		}
}

void FxExplosion(const Vector &at)
{
	if(fxprefs.impacts&&at.y<WATERHEIGHT-.3f)
	{
		//under water: no fire, a dull flash, a burst of bubbles, and the water
		//thrown up above if it isn't too deep
		FxParticle *f=Spawn(FX_FLASH,at,Vector(0,0,0),.2f,2.5f,4);
		if(f)
		{
			f->r=.45f;
			f->g=.6f;
			f->b=.7f;
		}
		FxBubbles(at,70,1.4f);
		float depth=WATERHEIGHT-at.y;
		if(depth<6) FxWaterSplash(at,1+4*(1-depth/6));
		return;
	}
	Vector pos=at+Vector(0,.4f,0);   //grenades lie on the ground
	FxParticle *flash=Spawn(FX_FLASH,pos+Vector(0,.6f,0),Vector(0,0,0),.14f,7,8);
	if(flash)
	{
		flash->g=.9f;
		flash->b=.7f;
	}
	for(int i=0;i<14;i++)
	{
		Vector d=RndDir();
		d.y=fabsf(d.y)*.8f+.2f;      //the ground stops the lower half
		FxParticle *p=Spawn(FX_FIRE,pos+d*Rnd(0,.6f),d*Rnd(1.5f,4.5f),Rnd(.45f,.85f),Rnd(.5f,.9f),Rnd(2,3.4f));
		if(p) p->spin=Rnd(-1.5f,1.5f);
	}
	for(int i=0;i<40;i++)
	{
		Vector d=RndDir();
		d.y=fabsf(d.y)+.15f;
		Spawn(FX_SPARK,pos,Normalize(d)*Rnd(7,17),Rnd(.35f,.9f),Rnd(.04f,.07f),0);
	}
	for(int i=0;i<16;i++)
	{
		Vector d=RndDir();
		d.y=fabsf(d.y)+.4f;
		FxParticle *p=Spawn(FX_DEBRIS,pos,Normalize(d)*Rnd(3,8),Rnd(1.2f,2.2f),Rnd(.07f,.15f),0);
		if(!p) continue;
		p->spin=Rnd(-8,8);
		float shade=Rnd(.12f,.28f);
		p->r=shade*1.15f;
		p->g=shade;
		p->b=shade*.8f;
	}
	if((int)lights.size()<FXMAXLIGHTS)
	{
		FxLight l={pos,0,.4f,8};
		lights.push_back(l);
	}
	//the smoke that stays: sooty puffs taking over from the fire as it dies down
	if(fxprefs.smoke)
		for(int i=0;i<10;i++)
		{
			Vector d=RndDir();
			d.y=fabsf(d.y)*.5f;
			Smoke(pos+d*Rnd(.2f,1.6f),d*Rnd(.3f,1.2f)+Vector(0,Rnd(.5f,1.1f),0),Rnd(.15f,.5f),Rnd(3.5f,6),
				Rnd(.7f,1.1f),Rnd(2.4f,3.6f),Rnd(.42f,.58f),Rnd(.55f,.75f));
		}
}

//the trail of grenades in flight, from where the game moved them (object[], type 4),
//a puff every 30 ms of each: drawn side, so it follows the frame rate, not the ticks
static void GrenadeTrails(float dt)
{
	static float since[MAXOBJS];
	for(int i=0;i<MAXOBJS;i++)
	{
		const Object &o=object[i];
		if(o.type!=4||o.wasfroze||Magnitude(o.vel)<.02f)
		{
			since[i]=0;
			continue;
		}
		since[i]+=dt;
		while(since[i]>=.03f)
		{
			since[i]-=.03f;
			//sinking in a lake: air, not smoke
			if(o.pos.y<WATERHEIGHT)
			{
				if(fxprefs.impacts) FxBubbles(o.pos,1,.05f);
				continue;
			}
			Smoke(o.pos+RndDir()*.05f,RndDir()*.15f+Vector(0,.25f,0),0,Rnd(1.1f,1.7f),.15f,Rnd(.6f,.9f),Rnd(.7f,.82f),.5f);
		}
	}
}

//moves everything by dt seconds and drops what is over
static void Update(float dt)
{
	for(size_t i=0;i<parts.size();)
	{
		FxParticle &p=parts[i];
		p.age+=dt;
		if(p.age<0)
		{
			i++;
			continue;
		}
		if(p.age>=p.life)
		{
			p=parts.back();
			parts.pop_back();
			continue;
		}
		p.rot+=p.spin*dt;
		switch(p.kind)
		{
		case FX_FIRE:
			p.vel*=expf(-3.5f*dt);   //the air brakes the puffs, the heat lifts them
			p.vel.y+=1.5f*dt;
			break;
		case FX_SMOKE:
			p.vel*=expf(-1.2f*dt);   //slows down to the rise of warm air and a light wind
			p.vel+=(p.drift-p.vel)*(1-expf(-.6f*dt));
			break;
		case FX_DROP:
			p.vel.y-=12*dt;
			p.pos+=p.vel*dt;
			//back into the water, or on the shore
			if((p.vel.y<0&&p.pos.y<Surface(p.pos.x))||p.pos.y<GetHeight(p.pos))
			{
				p=parts.back();
				parts.pop_back();
				continue;
			}
			i++;
			continue;
		case FX_BUBBLE:
			p.pos+=p.vel*dt;
			if(p.pos.y>Surface(p.pos.x)-.03f)
			{
				p=parts.back();
				parts.pop_back();
				continue;
			}
			i++;
			continue;
		case FX_SPARK:
		case FX_DEBRIS:
			{
				p.vel.y-=(p.kind==FX_SPARK?9.f:14.f)*dt;
				Vector next=p.pos+p.vel*dt;
				if(next.y<GetHeight(next))
				{
					p.vel.y=fabsf(p.vel.y)*.3f;
					p.vel.x*=.5f;
					p.vel.z*=.5f;
					p.spin*=.5f;
					next=p.pos;
				}
				p.pos=next;
				i++;
				continue;
			}
		}
		p.pos+=p.vel*dt;
		i++;
	}
	for(size_t i=0;i<lights.size();)
	{
		lights[i].age+=dt;
		if(lights[i].age>=lights[i].life)
		{
			lights[i]=lights.back();
			lights.pop_back();
			continue;
		}
		i++;
	}
}

//---- drawing ----

//the camera's axes and position, from the modelview (a rotation and a translation)
static Vector camright,camup2,camfwd,cameye;

static void CameraFromModelView()
{
	const float *m=RModelView().m;
	camright=Vector(m[0],m[4],m[8]);
	camup2=Vector(m[1],m[5],m[9]);
	camfwd=Vector(-m[2],-m[6],-m[10]);
	cameye=Vector(-(m[0]*m[12]+m[1]*m[13]+m[2]*m[14]),
		-(m[4]*m[12]+m[5]*m[13]+m[6]*m[14]),
		-(m[8]*m[12]+m[9]*m[13]+m[10]*m[14]));
}

static inline void Vertex(const Vector &v)
{
	RVertex3f(v.x,v.y,v.z);
}

static void Billboard(const Vector &c,float size,float rot,float r,float g,float b,float a)
{
	float cs=cosf(rot)*size,sn=sinf(rot)*size;
	Vector x=camright*cs+camup2*sn,y=camup2*cs-camright*sn;
	RColor4f(r,g,b,a);
	RTexCoord2f(0,0);
	Vertex(c-x-y);
	RTexCoord2f(1,0);
	Vertex(c+x-y);
	RTexCoord2f(1,1);
	Vertex(c+x+y);
	RTexCoord2f(0,1);
	Vertex(c-x+y);
}

static float Ease(float t)
{
	return 1-(1-t)*(1-t);
}

//white hot, then orange, then a dull red that fades
static void FireColor(float t,float *r,float *g,float *b,float *a)
{
	if(t<.15f)
	{
		float k=t/.15f;
		*r=1;
		*g=.95f-.25f*k;
		*b=.75f-.5f*k;
	}
	else if(t<.5f)
	{
		float k=(t-.15f)/.35f;
		*r=1;
		*g=.7f-.35f*k;
		*b=.25f-.17f*k;
	}
	else
	{
		float k=(t-.5f)/.5f;
		*r=1-.55f*k;
		*g=.35f-.25f*k;
		*b=.08f-.05f*k;
	}
	*a=t<.45f?1:1-(t-.45f)/.55f;
}

//a light on the ground under an explosion: the land's own shape, lit by addition
static void DrawLight(const FxLight &l)
{
	float t=l.age/l.life;
	float bright=(1-t)*(1-t);
	const int n=12;
	float step=l.radius*2/n;
	Vector c=l.pos;
	MakeClose(&c);
	for(int i=0;i<n;i++)
	{
		RBegin(RP_TRISTRIP);
		for(int j=0;j<=n;j++)
			for(int k=0;k<2;k++)
			{
				float x=c.x-l.radius+(i+k)*step,z=c.z-l.radius+j*step;
				float dx=(x-c.x)/l.radius,dz=(z-c.z)/l.radius;
				float fall=Clamp01(1-(dx*dx+dz*dz));
				RColor4f(1,.6f,.25f,bright*fall*fall*.8f);
				RVertex3f(x,GetLandSmooth(x,z)+.06f,z);
			}
		REnd();
	}
}

//a streak from p back along its velocity (length: seconds of travel), across the
//view: sparks, water drops
static void Streak(const FxParticle &p,float length,float r,float g,float b,float a)
{
	Vector tail=p.pos-p.vel*length;
	Vector side=CrossProduct(p.vel,p.pos-cameye);
	float len=Magnitude(side);
	if(len<1e-6f) return;
	side*=p.size0/len;
	RColor4f(r,g,b,a);
	RTexCoord2f(1,0);
	Vertex(p.pos-side);
	RTexCoord2f(1,1);
	Vertex(p.pos+side);
	RTexCoord2f(0,1);
	Vertex(tail+side);
	RTexCoord2f(0,0);
	Vertex(tail-side);
}

void FxMuzzleGlow(const Vector &pos,float size,float bright,float rot)
{
	CameraFromModelView();
	RPushAttrib(RATTR_ENABLE|RATTR_COLOR|RATTR_FOG|RATTR_POLYGON|RATTR_TEXTURE|RATTR_CURRENT);
	RDisable(RCAP_CULL|RCAP_FOG);
	REnable(RCAP_BLEND|RCAP_DEPTHTEST|RCAP_TEXTURE);
	RBlendFunc(RBLEND_ADD);
	RDepthMask(0);
	//a ragged orange burst, and a white-hot core
	TexBind(TEX_FX_FIRE);
	RBegin(RP_QUADS);
	Billboard(pos,size,rot,1,.6f,.25f,.8f*bright);
	REnd();
	TexBind(TEX_FX_GLOW);
	RBegin(RP_QUADS);
	Billboard(pos,size*.45f,0,1,.95f,.8f,bright);
	REnd();
	RDepthMask(1);
	RPopAttrib();
}

struct SortItem
{
	float depth;
	int index;
	bool operator<(const SortItem &o) const { return depth>o.depth; }   //far first
};

//which of the two passes draws p (see the top of the file)
static int belowcamera;       //the camera is under water: everything after the water
static int InLayer(const FxParticle &p,int layer)
{
	if(p.age<0) return 0;
	if(belowcamera) return layer==1;
	return (p.pos.y<WATERHEIGHT-.25f)==(layer==0);
}

void DrawEffects(int layer)
{
	//once a frame, even when Draw runs twice (stereo)
	static DWORD lasttime;
	static int started;
	if(!started||curtime!=lasttime)
	{
		float dt=started?(float)(int)(curtime-lasttime)/1000:0;
		if(dt<0) dt=0;
		if(dt>.1f) dt=.1f;          //a pause (menu, loading) doesn't jump everything
		started=1;
		lasttime=curtime;
		if(fxprefs.smoke) GrenadeTrails(dt);
		Update(dt);
	}
	if(parts.empty()&&lights.empty()) return;
	CameraFromModelView();
	belowcamera=GetLocalPlayer()->pos.y<WATERHEIGHT;   //as the game's underwater fog
	if(layer==0||belowcamera)
		for(size_t i=0;i<parts.size();i++) MakeClose(&parts[i].pos);

	RPushAttrib(RATTR_ENABLE|RATTR_COLOR|RATTR_FOG|RATTR_POLYGON|RATTR_TEXTURE|RATTR_CURRENT);
	RDisable(RCAP_CULL);
	REnable(RCAP_BLEND|RCAP_DEPTHTEST);
	RDepthMask(0);

	//glows
	static const float black[4]={0,0,0,0};
	RFogColor(black);
	RBlendFunc(RBLEND_ADD);
	if(!lights.empty()&&layer==1)
	{
		RDisable(RCAP_TEXTURE);
		for(size_t i=0;i<lights.size();i++) DrawLight(lights[i]);
	}
	REnable(RCAP_TEXTURE);
	TexBind(TEX_FX_FIRE);
	RBegin(RP_QUADS);
	for(size_t i=0;i<parts.size();i++)
	{
		const FxParticle &p=parts[i];
		if(p.kind!=FX_FIRE||!InLayer(p,layer)) continue;
		float t=p.age/p.life,r,g,b,a;
		FireColor(t,&r,&g,&b,&a);
		//.8: the flame's texture shows instead of a white blob where puffs add up
		Billboard(p.pos,p.size0+(p.size1-p.size0)*Ease(t),p.rot,r,g,b,a*.8f);
	}
	REnd();
	TexBind(TEX_FX_GLOW);
	RBegin(RP_QUADS);
	for(size_t i=0;i<parts.size();i++)
	{
		const FxParticle &p=parts[i];
		if(p.kind!=FX_FLASH||!InLayer(p,layer)) continue;
		float t=p.age/p.life;
		Billboard(p.pos,p.size0+(p.size1-p.size0)*t,0,p.r,p.g,p.b,(1-t)*(1-t));
	}
	REnd();
	//sparks: a streak from the spark back along its velocity, across the view
	TexBind(TEX_FX_STREAK);
	RBegin(RP_QUADS);
	for(size_t i=0;i<parts.size();i++)
	{
		const FxParticle &p=parts[i];
		if(p.kind!=FX_SPARK||!InLayer(p,layer)) continue;
		float fade=1-p.age/p.life;
		Streak(p,.045f,1,.75f+.2f*fade,.3f+.4f*fade,fade);
	}
	REnd();
	RPopAttrib();

	//solid things, by alpha in the scene's fog, far ones first
	static std::vector<SortItem> order;
	order.clear();
	for(size_t i=0;i<parts.size();i++)
	{
		int kind=parts[i].kind;
		if((kind==FX_DEBRIS||kind==FX_SMOKE||kind==FX_RING||kind==FX_DROP||kind==FX_BUBBLE)&&InLayer(parts[i],layer))
		{
			SortItem s={DotProduct(parts[i].pos-cameye,camfwd),(int)i};
			order.push_back(s);
		}
	}
	if(order.empty())
	{
		RDepthMask(1);
		return;
	}
	std::sort(order.begin(),order.end());
	RPushAttrib(RATTR_ENABLE|RATTR_COLOR|RATTR_POLYGON|RATTR_CURRENT|RATTR_TEXTURE);
	RDisable(RCAP_CULL);
	REnable(RCAP_BLEND|RCAP_DEPTHTEST|RCAP_TEXTURE);
	RBlendFunc(RBLEND_ALPHA);
	int bound=-1;
	for(size_t k=0;k<order.size();k++)
	{
		const FxParticle &p=parts[order[k].index];
		float t=p.age/p.life;
		static const int textures[]={0,0,0,TEX_FX_CHUNK,TEX_FX_SMOKE,TEX_FX_RING,TEX_FX_STREAK,TEX_FX_BUBBLE};
		int tex=textures[p.kind];
		if(tex!=bound)
		{
			if(bound>=0) REnd();
			TexBind(tex);
			RBegin(RP_QUADS);
			bound=tex;
		}
		switch(p.kind)
		{
		case FX_SMOKE:
			{
				//fades in quickly, then thins out as it spreads
				float a=p.a*Clamp01(t*8)*(1-t)*(1-t*.5f);
				Billboard(p.pos,p.size0+(p.size1-p.size0)*Ease(t),p.rot,p.r,p.g,p.b,a);
			}
			break;
		case FX_RING:
			{
				//flat on the water, where the waves are now
				float size=p.size0+(p.size1-p.size0)*Ease(t);
				float y=Surface(p.pos.x)+.03f;
				RColor4f(.85f,.9f,.95f,.75f*(1-t)*(1-t));
				RTexCoord2f(0,0);
				RVertex3f(p.pos.x-size,y,p.pos.z-size);
				RTexCoord2f(1,0);
				RVertex3f(p.pos.x+size,y,p.pos.z-size);
				RTexCoord2f(1,1);
				RVertex3f(p.pos.x+size,y,p.pos.z+size);
				RTexCoord2f(0,1);
				RVertex3f(p.pos.x-size,y,p.pos.z+size);
			}
			break;
		case FX_DROP:
			Streak(p,.05f,.8f,.87f,.95f,.8f);
			break;
		case FX_BUBBLE:
			{
				//a wobble sideways as it rises
				Vector at=p.pos+camright*(sinf(p.age*p.spin)*.04f);
				Billboard(at,p.size0,0,.85f,.92f,1,.7f*Clamp01(t*10));
			}
			break;
		default:
			Billboard(p.pos,p.size0,p.rot,p.r,p.g,p.b,t<.8f?1:(1-t)*5);
			break;
		}
	}
	REnd();
	RPopAttrib();
	RDepthMask(1);
}
