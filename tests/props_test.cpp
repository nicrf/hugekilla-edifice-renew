// Unit test of the rocks and trees that block (src/game/props.h, docs/PROPS.md):
// the same land gives the same props (and the same hash, on every OS: the golden
// value below; CI runs this test on macOS, Linux and Windows), on dry land, out of
// the scenery's way, never overlapping; then the collision math: trunks and rocks
// against bullets, players and blocks, across the edge of the world.
#include "props.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

#define SIZE 512
#define WATER 23.0f

//a land of hills and lakes from integer hashes and exact float operations only (no
//sinf: the land must be the same everywhere for the golden hash to mean something)
static float Lattice(int x,int z,int period,unsigned seed)
{
	x=(x%period+period)%period;
	z=(z%period+period)%period;
	unsigned v=(unsigned)x*2654435761u^(unsigned)z*2246822519u^seed*3266489917u;
	v^=v>>15;
	v*=0x2c1b3c6du;
	v^=v>>12;
	return (v&0xffff)/65535.0f;
}

static float Value(int x,int z,int cell,unsigned seed)
{
	int cx=x/cell,cz=z/cell,period=SIZE/cell;
	float fx=(float)(x%cell)/cell,fz=(float)(z%cell)/cell;
	fx=fx*fx*(3-2*fx);
	fz=fz*fz*(3-2*fz);
	float a=Lattice(cx,cz,period,seed),b=Lattice(cx+1,cz,period,seed);
	float c=Lattice(cx,cz+1,period,seed),d=Lattice(cx+1,cz+1,period,seed);
	return (a*(1-fx)+b*fx)*(1-fz)+(c*(1-fx)+d*fx)*fz;
}

static std::vector<float> MakeLand(unsigned seed)
{
	std::vector<float> land((size_t)SIZE*SIZE);
	for(int x=0;x<SIZE;x++)
		for(int z=0;z<SIZE;z++)
			land[(size_t)x*SIZE+z]=WATER-8+Value(x,z,128,seed)*40+Value(x,z,32,seed+1)*14+Value(x,z,8,seed+2)*2;
	return land;
}

static float Dist2(const Prop &a,const Prop &b)
{
	float dx=PropWrap(a.x-b.x,SIZE),dz=PropWrap(a.z-b.z,SIZE);
	return dx*dx+dz*dz;
}

static void Placement()
{
	std::vector<float> land=MakeLand(7);
	float avoid[4]={100,100,300.5f,40};
	std::vector<Prop> a,b,c;
	PropsGenerate(land.data(),SIZE,WATER,12345,avoid,2,a);
	PropsGenerate(land.data(),SIZE,WATER,12345,avoid,2,b);
	CHECK(a.size()==b.size());
	CHECK(!a.empty()&&!memcmp(a.data(),b.data(),a.size()*sizeof(Prop)));
	CHECK(PropsHash(a)==PropsHash(b));
	PropsGenerate(land.data(),SIZE,WATER,54321,avoid,2,c);
	CHECK(PropsHash(c)!=PropsHash(a));
	int trees=0,rocks=0;
	for(size_t i=0;i<a.size();i++)
	{
		const Prop &p=a[i];
		CHECK(p.x>=0&&p.x<SIZE&&p.z>=0&&p.z<SIZE);
		CHECK(p.radius>0&&p.radius<4&&p.height>0&&p.height<12);
		if(p.kind==PROP_ROCK)
		{
			rocks++;
			CHECK(p.y+.2f*p.scale>=WATER-1);   //the land under it: shore at most
		}
		else
		{
			trees++;
			CHECK(p.y>=WATER+1.5f);            //dry land
		}
		for(int k=0;k<2;k++)
		{
			float dx=PropWrap(p.x-avoid[k*2],SIZE),dz=PropWrap(p.z-avoid[k*2+1],SIZE);
			CHECK(dx*dx+dz*dz>=PROPAVOID*PROPAVOID);
		}
		//nothing stands in a rock
		if(p.kind==PROP_ROCK)
			for(size_t k=0;k<i;k++)
			{
				float reach=p.radius+a[k].radius;
				CHECK(Dist2(p,a[k])>=reach*reach);
			}
	}
	printf("%d trees, %d rocks, hash %08x\n",trees,rocks,PropsHash(a));
	CHECK(trees>150&&trees<1500);
	CHECK(rocks>20&&rocks<800);
	//golden: the same props on every OS (a change of the placement on purpose changes
	//it: update it here, and play only with builds that agree, docs/PROPS.md)
	CHECK(PropsHash(a)==0x7915c82cu);
	//a sea: nothing
	std::vector<float> sea((size_t)SIZE*SIZE,WATER-5);
	PropsGenerate(sea.data(),SIZE,WATER,12345,0,0,c);
	CHECK(c.empty());
	//not a land
	PropsGenerate(0,SIZE,WATER,12345,0,0,c);
	CHECK(c.empty());
}

static Prop Trunk()
{
	Prop p;
	memset(&p,0,sizeof(p));
	p.kind=PROP_CONIFER;
	p.x=10;
	p.y=30;
	p.z=20;
	p.radius=.5f;
	p.height=8;
	p.scale=1;
	return p;
}

static Prop Rock()
{
	Prop p=Trunk();
	p.kind=PROP_ROCK;
	p.radius=2;
	p.height=1;
	return p;
}

static void Collision()
{
	Prop t=Trunk();
	float hit;
	//a bullet across the trunk at chest height: stops on its bark
	float a[3]={-3,32,0},b[3]={3,32,0};
	CHECK(PropSegmentHit(t,a,b,&hit)&&fabsf(hit-2.5f/6)<1e-4f);
	//beside it, above its top, too short to reach it
	float a2[3]={-3,32,.6f},b2[3]={3,32,.6f};
	CHECK(!PropSegmentHit(t,a2,b2,&hit));
	float a3[3]={-3,38.5f,0},b3[3]={3,38.5f,0};
	CHECK(!PropSegmentHit(t,a3,b3,&hit));
	float a4[3]={-3,32,0},b4[3]={-1,32,0};
	CHECK(!PropSegmentHit(t,a4,b4,&hit));
	//from inside: at once; and diving into its top
	float a5[3]={.1f,32,0},b5[3]={3,32,0};
	CHECK(PropSegmentHit(t,a5,b5,&hit)&&hit==0);
	float a6[3]={0,40,-.1f},b6[3]={0,36,.1f};
	CHECK(PropSegmentHit(t,a6,b6,&hit)&&fabsf(hit-.5f)<1e-3f);
	//a rock: under its dome, not over it
	Prop r=Rock();
	CHECK(fabsf(PropTopAt(r,0)-31)<1e-5f);
	CHECK(PropTopAt(r,1)<31&&PropTopAt(r,1)>30.5f);
	CHECK(PropTopAt(r,4.1f)<30);
	float r1[3]={-4,30.5f,0},r2[3]={4,30.5f,0};
	CHECK(PropSegmentHit(r,r1,r2,&hit)&&hit>.25f&&hit<.5f);
	float r3[3]={-4,31.2f,0},r4[3]={4,31.2f,0};
	CHECK(!PropSegmentHit(r,r3,r4,&hit));
	//a player against the trunk: out along the line from its axis
	float nx,nz,depth;
	CHECK(PropCircleOverlap(t,.8f,0,.5f,29,31.9f,&nx,&nz,&depth)&&nx==1&&nz==0&&fabsf(depth-.2f)<1e-5f);
	CHECK(!PropCircleOverlap(t,1.1f,0,.5f,29,31.9f,&nx,&nz,&depth));
	CHECK(!PropCircleOverlap(t,.8f,0,.5f,38.6f,40,&nx,&nz,&depth));   //over it (jetpack)
	CHECK(PropCircleOverlap(t,0,0,.5f,29,31,&nx,&nz,&depth)&&depth>.99f);   //right on the axis
	//blocks: a 2-unit box next to the trunk, through it, above it
	CHECK(!PropBoxOverlap(t,.6f,30,-1,2.6f,32,1));
	CHECK(PropBoxOverlap(t,.4f,30,-1,2.4f,32,1));
	CHECK(PropBoxOverlap(t,-1,30,-1,1,32,1));
	CHECK(!PropBoxOverlap(t,-1,38,-1,1,40,1));
	CHECK(!PropBoxOverlap(t,.4f,30,.4f,2.4f,32,2.4f));   //the corner past its radius
	//the world repeats
	CHECK(fabsf(PropWrap(511.5f-.5f,SIZE)-(-1))<1e-5f);
	CHECK(fabsf(PropWrap(.5f-511.5f,SIZE)-1)<1e-5f);
	CHECK(fabsf(PropWrap(1024+3,SIZE)-3)<1e-5f);
	std::vector<Prop> props;
	Prop edge=Trunk();
	edge.x=511.5f;
	edge.z=.3f;
	props.push_back(Trunk());
	props.push_back(edge);
	PropGrid grid;
	grid.Build(props,SIZE);
	int near[8];
	int n=grid.Near(512+.5f,512+.2f,1,near,8);   //across the edge, in the next copy of the world
	CHECK(n==1&&near[0]==1);
	n=grid.Near(10,20,1,near,8);
	CHECK(n==1&&near[0]==0);
	n=grid.Near(200,200,1,near,8);
	CHECK(n==0);
}

int main()
{
	Placement();
	Collision();
	if(failures)
	{
		printf("%d failures\n",failures);
		return 1;
	}
	printf("props: all tests passed\n");
	return 0;
}
