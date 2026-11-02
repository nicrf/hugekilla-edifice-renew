//The props' placement and collision math (props.h): pure functions of the land, no
//game state, so that tests/props_test.cpp checks them and every machine computes
//the same props. Placement is the one the Ultra vegetation (#90) had for its trees
//and boulders, over the whole land at once instead of around the camera.

#include "props.h"
#include <math.h>
#include <string.h>

//the same props from the same land on every compiler: no a*b+c fused into one
//rounding here (clang and GCC may do it on arm64)
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

static unsigned PropHashCell(int x,int z,unsigned seed)
{
	unsigned v=(unsigned)x*73856093u^(unsigned)z*19349663u^seed*83492791u;
	v^=v>>13;
	v*=0x5bd1e995u;
	v^=v>>15;
	v*=0x27d4eb2du;
	v^=v>>16;
	return v;
}

//exact in float: 24 bits of s over 2^24
static float PropRand(unsigned &s)
{
	s=s*1664525u+1013904223u;
	return (s>>8)/16777216.0f;
}

static float LandAt(const float *land,int size,int x,int z)
{
	x&=size-1;
	z&=size-1;
	return land[(size_t)x*size+z];
}

//a periodic value noise over the land (repeats with it): where the forests are
static float LandNoise(int x,int z,int cell,int size,unsigned seed)
{
	int cells=size/cell;
	int cx=x/cell,cz=z/cell;
	float fx=(float)(x%cell)/cell,fz=(float)(z%cell)/cell;
	auto h=[&](int a,int b)
	{
		unsigned v=(unsigned)(((a%cells+cells)%cells)*73856093u)^(unsigned)(((b%cells+cells)%cells)*19349663u)^seed;
		v^=v>>13;
		v*=0x5bd1e995u;
		v^=v>>15;
		return (v&0xffff)/65535.0f;
	};
	fx=fx*fx*(3-2*fx);
	fz=fz*fz*(3-2*fz);
	float a=h(cx,cz),b=h(cx+1,cz),c=h(cx,cz+1),d=h(cx+1,cz+1);
	return (a*(1-fx)+b*fx)*(1-fz)+(c*(1-fx)+d*fx)*fz;
}

//the land's height at x, z (bilinear) and its squared slope (gradient over 4 units)
static float Sample(const float *land,int size,float x,float z,float *g2)
{
	int ix=(int)floorf(x),iz=(int)floorf(z);
	float fx=x-ix,fz=z-iz;
	float h=(LandAt(land,size,ix,iz)*(1-fx)+LandAt(land,size,ix+1,iz)*fx)*(1-fz)+
		(LandAt(land,size,ix,iz+1)*(1-fx)+LandAt(land,size,ix+1,iz+1)*fx)*fz;
	float dx=(LandAt(land,size,ix+2,iz)-LandAt(land,size,ix-2,iz))/4;
	float dz=(LandAt(land,size,ix,iz+2)-LandAt(land,size,ix,iz-2))/4;
	*g2=dx*dx+dz*dz;
	return h;
}

static int Near(float x,float z,float px,float pz,float dist,int size)
{
	float dx=PropWrap(x-px,size),dz=PropWrap(z-pz,size);
	return dx*dx+dz*dz<dist*dist;
}

//the three rock shapes (radii of the Ultra mesh, ultra_veg.inl)
static const float rockrx[3]={1,.8f,1.2f},rockry[3]={.6f,.75f,.45f},rockrz[3]={.85f,.9f,.7f};

void PropsGenerate(const float *land,int size,float water,unsigned seed,const float *avoid,int numavoid,
	std::vector<Prop> &out)
{
	out.clear();
	if(!land||size<16||(size&(size-1))) return;
	auto avoided=[&](float x,float z)
	{
		for(int i=0;i<numavoid;i++)
			if(Near(x,z,avoid[i*2],avoid[i*2+1],PROPAVOID,size)) return 1;
		return 0;
	};
	//trees: one per 8-unit cell at most, thick in the forests, few elsewhere
	const int tc=8;
	int tcells=size/tc;
	for(int cz=0;cz<tcells;cz++)
		for(int cx=0;cx<tcells;cx++)
		{
			unsigned s=PropHashCell(cx,cz,seed);
			float forest=LandNoise(cx*tc,cz*tc,64,size,seed*7u+11u);
			float p=forest>.58f?.55f:.04f;
			if(PropRand(s)>p) continue;
			float x=(cx+.15f+.7f*PropRand(s))*tc,z=(cz+.15f+.7f*PropRand(s))*tc;
			float g2;
			float h=Sample(land,size,x,z,&g2);
			//dry land, not too steep, below the snowy peaks
			if(h<water+1.5f||g2>.6f||h>water+60) continue;
			if(avoided(x,z)) continue;
			Prop pr;
			memset(&pr,0,sizeof(pr));
			pr.x=x;
			pr.y=h;
			pr.z=z;
			pr.seed=s;
			if(PropRand(s)<.5f||h>water+30)
			{
				pr.kind=PROP_CONIFER;
				pr.scale=.8f+PropRand(s)*.6f;
				pr.radius=.35f*pr.scale;      //the trunk, with the branches close to it
				pr.height=7.5f*pr.scale;
			}
			else
			{
				pr.kind=PROP_TREE;
				pr.scale=.75f+PropRand(s)*.5f;
				pr.radius=.38f*pr.scale;
				pr.height=6*pr.scale;
			}
			out.push_back(pr);
		}
	size_t numtrees=out.size();
	//boulders: one per 8-unit cell at most, more on slopes and shores, never in a
	//tree or another rock (the small stones stay Ultra decorations)
	const int rc=8;
	int rcells=size/rc;
	for(int cz=0;cz<rcells;cz++)
		for(int cx=0;cx<rcells;cx++)
		{
			unsigned s=PropHashCell(cx,cz,seed^0x9e3779b9u);
			float x=(cx+PropRand(s))*rc,z=(cz+PropRand(s))*rc;
			float g2;
			float h=Sample(land,size,x,z,&g2);
			float slope=1-1/sqrtf(1+g2);
			float shore=(h>water-1.5f&&h<water+2)?.05f:0;
			float p=.012f+(slope>.15f?(slope<.65f?slope-.15f:.5f):0)*.25f+shore;
			if(PropRand(s)>p||h<water-1) continue;
			if(avoided(x,z)) continue;
			Prop pr;
			memset(&pr,0,sizeof(pr));
			float big=PropRand(s);
			pr.kind=PROP_ROCK;
			pr.variant=(unsigned char)((int)(PropRand(s)*3)%3);
			pr.scale=.9f+big*big*1.6f;
			pr.x=x;
			pr.y=h-.2f*pr.scale;     //sunk a little, as Ultra drew them
			pr.z=z;
			pr.seed=s;
			int v=pr.variant;
			pr.radius=pr.scale*(rockrx[v]>rockrz[v]?rockrx[v]:rockrz[v])*.95f;
			pr.height=pr.scale*rockry[v];
			int clear=1;
			for(size_t i=0;i<out.size()&&clear;i++)
				if(Near(x,z,out[i].x,out[i].z,pr.radius+out[i].radius+(i<numtrees?1:.5f),size)) clear=0;
			if(clear) out.push_back(pr);
		}
}

unsigned PropsHash(const std::vector<Prop> &props)
{
	unsigned h=2166136261u;
	auto mix=[&](unsigned v)
	{
		for(int i=0;i<4;i++)
		{
			h=(h^(v&0xff))*16777619u;
			v>>=8;
		}
	};
	mix((unsigned)props.size());
	for(const Prop &p:props)
	{
		mix(p.kind|p.variant<<8);
		mix(p.seed);
		mix((unsigned)(int)floorf(p.x*16));
		mix((unsigned)(int)floorf(p.z*16));
	}
	return h;
}

float PropTopAt(const Prop &p,float d2)
{
	float r2=p.radius*p.radius;
	if(d2>=r2) return p.y-1;
	if(p.kind!=PROP_ROCK) return p.y+p.height;
	return p.y+p.height*sqrtf(1-d2/r2);
}

int PropSegmentHit(const Prop &p,const float a[3],const float b[3],float *t)
{
	//where the segment is inside the circle, seen from above: t0..t1
	float dx=b[0]-a[0],dz=b[2]-a[2];
	float r=p.radius;
	float A=dx*dx+dz*dz,B=2*(a[0]*dx+a[2]*dz),C=a[0]*a[0]+a[2]*a[2]-r*r;
	float t0,t1;
	if(A<1e-12f)
	{
		if(C>0) return 0;
		t0=0;
		t1=1;
	}
	else
	{
		float disc=B*B-4*A*C;
		if(disc<0) return 0;
		float sq=sqrtf(disc);
		t0=(-B-sq)/(2*A);
		t1=(-B+sq)/(2*A);
		if(t1<0||t0>1) return 0;
		if(t0<0) t0=0;
		if(t1>1) t1=1;
	}
	if(p.kind!=PROP_ROCK)
	{
		//a trunk: the part of t0..t1 between its foot (a little under the land) and top
		float lo=-1,hi=p.height;   //relative to p.y
		float ay=a[1]-p.y,dy=b[1]-a[1];
		float s0=t0,s1=t1;
		if(fabsf(dy)<1e-9f)
		{
			if(ay<lo||ay>hi) return 0;
		}
		else
		{
			float u0=(lo-ay)/dy,u1=(hi-ay)/dy;
			if(u0>u1)
			{
				float k=u0;
				u0=u1;
				u1=k;
			}
			if(u0>s0) s0=u0;
			if(u1<s1) s1=u1;
			if(s0>s1) return 0;
		}
		*t=s0;
		return 1;
	}
	//a rock: under its dome somewhere in t0..t1 (a step of at most .1 unit)
	float len=sqrtf(A)*(t1-t0);
	int steps=(int)(len/.1f)+1;
	if(steps>64) steps=64;
	for(int i=0;i<=steps;i++)
	{
		float u=t0+(t1-t0)*i/steps;
		float x=a[0]+dx*u,y=a[1]+(b[1]-a[1])*u,z=a[2]+dz*u;
		if(y<PropTopAt(p,x*x+z*z)&&y>p.y-p.height)
		{
			*t=u;
			return 1;
		}
	}
	return 0;
}

int PropCircleOverlap(const Prop &p,float dx,float dz,float r,float y0,float y1,float *nx,float *nz,float *depth)
{
	if(y1<p.y-.5f||y0>p.y+p.height) return 0;
	float reach=p.radius+r;
	float d2=dx*dx+dz*dz;
	if(d2>=reach*reach) return 0;
	float d=sqrtf(d2);
	if(d<1e-4f)
	{
		*nx=1;
		*nz=0;
	}
	else
	{
		*nx=dx/d;
		*nz=dz/d;
	}
	*depth=reach-d;
	return 1;
}

int PropBoxOverlap(const Prop &p,float x0,float y0,float z0,float x1,float y1,float z1)
{
	if(y1<=p.y-.5f||y0>=p.y+p.height) return 0;
	//the box's point nearest the axis
	float cx=x0>0?x0:(x1<0?x1:0),cz=z0>0?z0:(z1<0?z1:0);
	return cx*cx+cz*cz<p.radius*p.radius;
}

void PropGrid::Build(const std::vector<Prop> &props,int worldsize)
{
	size=worldsize;
	cell=16;
	cells=size/16;
	if(cells<1) cells=1;
	lists.assign((size_t)cells*cells,std::vector<int>());
	for(size_t i=0;i<props.size();i++)
	{
		const Prop &p=props[i];
		int x0=(int)floorf((p.x-p.radius)/cell),x1=(int)floorf((p.x+p.radius)/cell);
		int z0=(int)floorf((p.z-p.radius)/cell),z1=(int)floorf((p.z+p.radius)/cell);
		for(int z=z0;z<=z1;z++)
			for(int x=x0;x<=x1;x++)
				lists[(size_t)((x%cells+cells)%cells)*cells+(z%cells+cells)%cells].push_back((int)i);
	}
}

int PropGrid::Near(float x,float z,float r,int *out,int max) const
{
	if(lists.empty()) return 0;
	int n=0;
	int x0=(int)floorf((x-r)/cell),x1=(int)floorf((x+r)/cell);
	int z0=(int)floorf((z-r)/cell),z1=(int)floorf((z+r)/cell);
	for(int cz=z0;cz<=z1;cz++)
		for(int cx=x0;cx<=x1;cx++)
		{
			const std::vector<int> &l=lists[(size_t)((cx%cells+cells)%cells)*cells+(cz%cells+cells)%cells];
			for(int i:l)
			{
				int seen=0;
				for(int k=0;k<n&&!seen;k++) seen=out[k]==i;
				if(!seen&&n<max) out[n++]=i;
			}
		}
	return n;
}
