//The game's rocks and trees (props.h, docs/PROPS.md): built with the land, then
//asked by the physics (playing.cpp), the build preview (structures.cpp), the server's
//bullets (multiplay.cpp, through TickObjects) and the drawing (propsdraw.cpp, Ultra).
//
//A prop is there unless a block stands in it (a saved map, or a block a modified
//client placed) or a station or flag lies near it: both depend only on what every
//machine already shares (blocks, server objects), so the server and the clients
//agree on which props are there.

#include "props.h"
#include "land.h"
#include "playing.h"
#include "gameplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static std::vector<Prop> props;
static std::vector<unsigned char> there;
static PropGrid grid;
static unsigned landhash;
static int enabled=1;
static DWORD lastrefresh;

#define PROPSMAXNEAR 64
//kept free around a station (its base is built there) and a dropped flag
#define STATIONCLEAR 6.0f
#define OBJECTCLEAR 3.0f

static int PropsLog()
{
	static int on=getenv("HK_PROPS_LOG")!=0;
	return on;
}

void PropsBuild(unsigned seed)
{
	//the map's scenery models keep their room
	std::vector<float> avoid;
	for(int i=0;i<GetNumSceneries();i++)
	{
		Scenery *s=GetScenery(i);
		avoid.push_back(s->pos.x-floorf(s->pos.x/LANDGRIDSIZE)*LANDGRIDSIZE);
		avoid.push_back(s->pos.z-floorf(s->pos.z/LANDGRIDSIZE)*LANDGRIDSIZE);
	}
	PropsGenerate(&land[0][0],LANDGRIDSIZE,WATERHEIGHT,seed,avoid.data(),(int)avoid.size()/2,props);
	grid.Build(props,LANDGRIDSIZE);
	there.assign(props.size(),1);
	landhash=PropsHash(props);
	lastrefresh=0;
	char text[80];
	snprintf(text,sizeof(text),"rocks and trees: %d (hash %08x)",(int)props.size(),landhash);
	ConsoleDisplayText(text);
	//debug aid (tests/net_props.py): the props on stderr
	if(PropsLog())
	{
		fprintf(stderr,"props: %d hash %08x\n",(int)props.size(),landhash);
		for(size_t i=0;i<props.size();i++)
		{
			const Prop &p=props[i];
			fprintf(stderr,"prop %d %d %.3f %.3f %.3f %.3f %.3f\n",(int)i,p.kind,p.x,p.y,p.z,p.radius,p.height);
		}
	}
}

void PropsSetEnabled(int on)
{
	enabled=on!=0;
}

int PropsEnabled()
{
	return enabled;
}

int PropsCount()
{
	return (int)props.size();
}

unsigned PropsLandHash()
{
	return landhash;
}

const std::vector<Prop> &PropsList()
{
	return props;
}

int PropThere(int i)
{
	return enabled&&i>=0&&i<(int)there.size()&&there[i];
}

static float Wrap(float d)
{
	return PropWrap(d,LANDGRIDSIZE);
}

void PropsRefresh()
{
	if(props.empty()) return;
	//5 times a second is enough: blocks and objects don't move every frame
	if(lastrefresh&&curtime-lastrefresh<200&&curtime>=lastrefresh) return;
	lastrefresh=curtime;
	for(size_t i=0;i<props.size();i++)
	{
		const Prop &p=props[i];
		int free=1;
		int x0=(int)floorf((p.x-p.radius)/BLOCKSIZE),x1=(int)floorf((p.x+p.radius)/BLOCKSIZE);
		int z0=(int)floorf((p.z-p.radius)/BLOCKSIZE),z1=(int)floorf((p.z+p.radius)/BLOCKSIZE);
		int y0=(int)floorf((p.y-.5f)/BLOCKSIZE),y1=(int)floorf((p.y+p.height)/BLOCKSIZE);
		if(y0<-1) y0=-1;
		for(int bx=x0;bx<=x1&&free;bx++)
			for(int bz=z0;bz<=z1&&free;bz++)
				for(int by=y0;by<=y1&&free;by++)
					if(GetBlock(bx,by,bz)->type&&PropBoxOverlap(p,bx*BLOCKSIZE-p.x,by*BLOCKSIZE,bz*BLOCKSIZE-p.z,
						(bx+1)*BLOCKSIZE-p.x,(by+1)*BLOCKSIZE,(bz+1)*BLOCKSIZE-p.z))
						free=0;
		there[i]=(unsigned char)free;
	}
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		const ServerObject &o=sobject[i];
		if(!o.type||o.attached!=-1) continue;   //carried: it goes where its carrier can
		float clear=o.type==WATERPUMP?STATIONCLEAR:OBJECTCLEAR;
		int near[PROPSMAXNEAR];
		int n=grid.Near(o.pos.x,o.pos.z,clear,near,PROPSMAXNEAR);
		for(int k=0;k<n;k++)
		{
			const Prop &p=props[near[k]];
			float dx=Wrap(o.pos.x-p.x),dz=Wrap(o.pos.z-p.z);
			float reach=clear+p.radius;
			if(dx*dx+dz*dz<reach*reach) there[near[k]]=0;
		}
	}
}

float PropsHeight(const Vector &pos)
{
	float top=-1e9f;
	if(!enabled) return top;
	int near[PROPSMAXNEAR];
	int n=grid.Near(pos.x,pos.z,0,near,PROPSMAXNEAR);
	for(int k=0;k<n;k++)
	{
		const Prop &p=props[near[k]];
		if(p.kind!=PROP_ROCK||!there[near[k]]) continue;
		float dx=Wrap(pos.x-p.x),dz=Wrap(pos.z-p.z);
		float h=PropTopAt(p,dx*dx+dz*dz);
		if(h>top) top=h;
	}
	return top;
}

int PropsPushPlayer(Vector &pos,Vector &vel,float radius,float feet,float head)
{
	if(!enabled) return 0;
	int near[PROPSMAXNEAR];
	int n=grid.Near(pos.x,pos.z,radius,near,PROPSMAXNEAR);
	int touched=0;
	for(int k=0;k<n;k++)
	{
		const Prop &p=props[near[k]];
		//the rocks are ground (PropsHeight): walked onto, or a wall when too steep
		if(p.kind==PROP_ROCK||!there[near[k]]) continue;
		float nx,nz,depth;
		if(!PropCircleOverlap(p,Wrap(pos.x-p.x),Wrap(pos.z-p.z),radius,pos.y-feet,pos.y+head,&nx,&nz,&depth)) continue;
		pos.x+=nx*depth;
		pos.z+=nz*depth;
		float into=vel.x*nx+vel.z*nz;
		if(into<0)
		{
			vel.x-=into*nx;
			vel.z-=into*nz;
		}
		touched=1;
	}
	return touched;
}

int PropsSegmentHit(const Vector &a,const Vector &b,Vector *hit)
{
	if(!enabled) return 0;
	float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z;
	float half=sqrtf(dx*dx+dz*dz)*.5f;
	int near[PROPSMAXNEAR];
	int n=grid.Near(a.x+dx*.5f,a.z+dz*.5f,half,near,PROPSMAXNEAR);
	float best=2;
	for(int k=0;k<n;k++)
	{
		const Prop &p=props[near[k]];
		if(!there[near[k]]) continue;
		float ra[3]={Wrap(a.x-p.x),a.y,Wrap(a.z-p.z)};
		float rb[3]={ra[0]+dx,b.y,ra[2]+dz};
		float t;
		if(PropSegmentHit(p,ra,rb,&t)&&t<best) best=t;
	}
	if(best>1) return 0;
	*hit=Vector(a.x+dx*best,a.y+dy*best,a.z+dz*best);
	return 1;
}

int PropsBounce(Vector &pos,const Vector &prev,Vector &vel)
{
	if(!enabled) return 0;
	int near[PROPSMAXNEAR];
	int n=grid.Near(pos.x,pos.z,0,near,PROPSMAXNEAR);
	for(int k=0;k<n;k++)
	{
		const Prop &p=props[near[k]];
		if(p.kind==PROP_ROCK||!there[near[k]]) continue;   //rocks: the ground (GetHeight)
		float dx=Wrap(pos.x-p.x),dz=Wrap(pos.z-p.z);
		if(dx*dx+dz*dz>=p.radius*p.radius||pos.y<p.y-.5f||pos.y>p.y+p.height) continue;
		//off the trunk where it came from, slower
		float px=Wrap(prev.x-p.x),pz=Wrap(prev.z-p.z);
		float d=sqrtf(px*px+pz*pz);
		float nx=d>1e-4f?px/d:1,nz=d>1e-4f?pz/d:0;
		float into=vel.x*nx+vel.z*nz;
		if(into<0)
		{
			vel.x-=2*into*nx;
			vel.z-=2*into*nz;
		}
		vel*=.6f;
		pos=prev;
		return 1;
	}
	return 0;
}

int PropsBoxBlocked(float x0,float y0,float z0,float x1,float y1,float z1)
{
	if(!enabled) return 0;
	float cx=(x0+x1)*.5f,cz=(z0+z1)*.5f;
	float r=sqrtf((x1-x0)*(x1-x0)+(z1-z0)*(z1-z0))*.5f;
	int near[PROPSMAXNEAR];
	int n=grid.Near(cx,cz,r,near,PROPSMAXNEAR);
	for(int k=0;k<n;k++)
	{
		const Prop &p=props[near[k]];
		if(!there[near[k]]) continue;
		float ox=Wrap(x0-p.x),oz=Wrap(z0-p.z);
		if(PropBoxOverlap(p,ox,y0,oz,ox+(x1-x0),y1,oz+(z1-z0))) return 1;
	}
	return 0;
}

int PropsSpawnFree(float x,float z)
{
	if(!enabled) return 1;
	int near[PROPSMAXNEAR];
	int n=grid.Near(x,z,4,near,PROPSMAXNEAR);
	for(int k=0;k<n;k++)
	{
		const Prop &p=props[near[k]];
		if(!there[near[k]]) continue;
		float dx=Wrap(x-p.x),dz=Wrap(z-p.z);
		float reach=p.radius+1.5f;
		if(dx*dx+dz*dz<reach*reach) return 0;
	}
	return 1;
}
