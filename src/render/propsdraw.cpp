//The rocks and trees that block (src/game/props.h), drawn for Classic: simple
//textured meshes made here (no files), so that a Classic player sees what he walks
//around and what stops his bullets. Ultra draws the same props itself, lit, swaying
//and with its far versions (ultra_veg.inl); when its vegetation is off, these are
//drawn there too (RMAT_MODEL).
//
//The shapes follow Ultra's: a conifer of stacked cones on a short trunk, a broadleaf
//tree of lumpy balls on a trunk, rocks of squashed lumpy spheres with a flat
//bottom; a lighter version of each far away. The light is baked in the vertex
//colors (sky above, a little sun), the textures are made at the first draw.

#include "renderer.h"
#include "textures.h"
#include "redrawing.h"
#include "props.h"
#include "land.h"
#include <math.h>
#include <string.h>
#include <vector>

#define TEX_BARK (TEX_PROPS0)
#define TEX_NEEDLES (TEX_PROPS0+1)
#define TEX_LEAVES (TEX_PROPS0+2)
#define TEX_ROCK (TEX_PROPS0+3)

#define PROPSRANGE 300.0f    //the fog ends at 400
#define PROPSFAR 70.0f       //the lighter meshes past this

struct P3
{
	float x,y,z;
};

static P3 p3(float x,float y,float z)
{
	P3 p={x,y,z};
	return p;
}

static P3 Normalized(P3 p)
{
	float l=sqrtf(p.x*p.x+p.y*p.y+p.z*p.z);
	if(l<1e-6f) return p3(0,1,0);
	return p3(p.x/l,p.y/l,p.z/l);
}

struct PropMesh
{
	std::vector<RVertex> verts;
	std::vector<RMeshPart> parts;
};

//conifer, tree, rock 0..2, each near and far
static PropMesh meshes[5][2];
static int made;

static unsigned Lcg(unsigned &s)
{
	s=s*1664525u+1013904223u;
	return s>>8;
}

static float Rand01(unsigned &s)
{
	return Lcg(s)/16777216.0f;
}

//---- textures ----

static float Hash2(int x,int y,int seed)
{
	unsigned h=(unsigned)x*374761393u+(unsigned)y*668265263u+(unsigned)seed*2246822519u;
	h=(h^(h>>13))*1274126177u;
	h^=h>>16;
	return (h&0xffff)/65535.f;
}

//value noise that wraps every period cells (the textures tile)
static float Noise2(float x,float y,int period,int seed)
{
	int x0=(int)floorf(x),y0=(int)floorf(y);
	float fx=x-x0,fy=y-y0;
	fx=fx*fx*(3-2*fx);
	fy=fy*fy*(3-2*fy);
	auto h=[&](int a,int b){ return Hash2((a%period+period)%period,(b%period+period)%period,seed); };
	return (h(x0,y0)*(1-fx)+h(x0+1,y0)*fx)*(1-fy)+(h(x0,y0+1)*(1-fx)+h(x0+1,y0+1)*fx)*fy;
}

#define PROPTEXSIZE 64

static void MakeTextures()
{
	static unsigned char px[PROPTEXSIZE*PROPTEXSIZE*3];
	for(int t=0;t<4;t++)
	{
		for(int y=0;y<PROPTEXSIZE;y++)
			for(int x=0;x<PROPTEXSIZE;x++)
			{
				float u=(float)x/PROPTEXSIZE,v=(float)y/PROPTEXSIZE;
				float n1=Noise2(u*8,v*8,8,t*3+1),n2=Noise2(u*32,v*32,32,t*3+2);
				float r,g,b;
				switch(t)
				{
				case 0:   //bark: dark furrows along the trunk
				{
					float f=Noise2(u*16,v*2,16,5)*.6f+n2*.4f;
					float k=.55f+.45f*f;
					r=.42f*k;
					g=.30f*k;
					b=.20f*k;
					break;
				}
				case 1:   //needles: dark green, fine
				{
					float k=.6f+.25f*n2+.15f*n1;
					r=.16f*k;
					g=.36f*k;
					b=.18f*k;
					break;
				}
				case 2:   //leaves: lighter, in clumps
				{
					float k=.55f+.3f*n1+.15f*n2;
					r=.26f*k;
					g=.48f*k;
					b=.16f*k;
					break;
				}
				default:   //rock: gray, a little lichen
				{
					float k=.6f+.25f*n1+.15f*n2;
					float moss=n1>.7f?(n1-.7f)*1.5f:0;
					r=(.52f-.15f*moss)*k;
					g=(.50f+.02f*moss)*k;
					b=(.47f-.15f*moss)*k;
					break;
				}
				}
				unsigned char *p=&px[(y*PROPTEXSIZE+x)*3];
				p[0]=(unsigned char)(r*255+.5f);
				p[1]=(unsigned char)(g*255+.5f);
				p[2]=(unsigned char)(b*255+.5f);
			}
		TexUpload(TEX_PROPS0+t,PROPTEXSIZE,PROPTEXSIZE,3,px,TEXF_MIPLINEAR);
	}
}

//---- meshes ----

static void Part(PropMesh &m,int tex)
{
	RMeshPart part={tex,(int)m.verts.size(),0};
	m.parts.push_back(part);
}

//a vertex, its light baked from its normal: the sky above, some sun
static void Vert(PropMesh &m,P3 p,P3 n,float u,float v,float ao)
{
	static const P3 sun=Normalized(p3(.3f,.8f,.5f));
	float d=n.x*sun.x+n.y*sun.y+n.z*sun.z;
	float shade=(.5f+.3f*(n.y*.5f+.5f)+.25f*(d>0?d:0))*ao;
	RVertex vx;
	memset(&vx,0,sizeof(vx));
	vx.x=p.x;
	vx.y=p.y;
	vx.z=p.z;
	vx.u=u;
	vx.v=v;
	vx.r=vx.g=vx.b=shade>1?1:shade;
	vx.a=1;
	vx.nx=n.x;
	vx.ny=n.y;
	vx.nz=n.z;
	m.verts.push_back(vx);
	m.parts.back().count++;
}

static void Trunk(PropMesh &m,float r0,float r1,float h,int sides)
{
	Part(m,TEX_BARK);
	for(int i=0;i<sides;i++)
	{
		float a0=6.2831853f*i/sides,a1=6.2831853f*(i+1)/sides;
		P3 n0=Normalized(p3(cosf(a0),.15f,sinf(a0))),n1=Normalized(p3(cosf(a1),.15f,sinf(a1)));
		P3 b0=p3(cosf(a0)*r0,0,sinf(a0)*r0),b1=p3(cosf(a1)*r0,0,sinf(a1)*r0);
		P3 t0=p3(cosf(a0)*r1,h,sinf(a0)*r1),t1=p3(cosf(a1)*r1,h,sinf(a1)*r1);
		float u0=(float)i/sides*2,u1=(float)(i+1)/sides*2;
		Vert(m,b0,n0,u0,0,.6f);
		Vert(m,t0,n0,u0,h*.5f,.9f);
		Vert(m,b1,n1,u1,0,.6f);
		Vert(m,b1,n1,u1,0,.6f);
		Vert(m,t0,n0,u0,h*.5f,.9f);
		Vert(m,t1,n1,u1,h*.5f,.9f);
	}
}

//a jagged cone from radius r at y0 to its tip at y1, and its darker underside
static void Cone(PropMesh &m,float y0,float y1,float r,int sides,unsigned seed)
{
	unsigned s=seed;
	std::vector<float> rr(sides);
	for(int i=0;i<sides;i++) rr[i]=r*(.8f+.4f*Rand01(s));
	float slope=r/(y1-y0);
	for(int i=0;i<sides;i++)
	{
		int j=(i+1)%sides;
		float a0=6.2831853f*i/sides,a1=6.2831853f*j/sides;
		P3 b0=p3(cosf(a0)*rr[i],y0,sinf(a0)*rr[i]),b1=p3(cosf(a1)*rr[j],y0,sinf(a1)*rr[j]),tip=p3(0,y1,0);
		P3 n0=Normalized(p3(cosf(a0),slope,sinf(a0))),n1=Normalized(p3(cosf(a1),slope,sinf(a1)));
		float u0=(float)i/sides*3,u1=(float)(i+1)/sides*3;
		Vert(m,b0,n0,u0,0,.75f);
		Vert(m,tip,p3(0,1,0),(u0+u1)*.5f,1.5f,1);
		Vert(m,b1,n1,u1,0,.75f);
		Vert(m,b0,p3(0,-1,0),u0,0,.5f);
		Vert(m,b1,p3(0,-1,0),u1,0,.5f);
		Vert(m,p3(0,y0+.4f,0),p3(0,-1,0),(u0+u1)*.5f,.3f,.4f);
	}
}

static void Icosphere(int subdiv,std::vector<P3> &tris)
{
	const float t=1.618034f;
	P3 v[12]={p3(-1,t,0),p3(1,t,0),p3(-1,-t,0),p3(1,-t,0),p3(0,-1,t),p3(0,1,t),p3(0,-1,-t),p3(0,1,-t),
		p3(t,0,-1),p3(t,0,1),p3(-t,0,-1),p3(-t,0,1)};
	static const int f[20][3]={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
		{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
	tris.clear();
	for(int i=0;i<20;i++)
		for(int k=0;k<3;k++) tris.push_back(Normalized(v[f[i][k]]));
	for(int s=0;s<subdiv;s++)
	{
		std::vector<P3> next;
		for(size_t i=0;i<tris.size();i+=3)
		{
			P3 a=tris[i],b=tris[i+1],c=tris[i+2];
			P3 ab=Normalized(p3(a.x+b.x,a.y+b.y,a.z+b.z)),bc=Normalized(p3(b.x+c.x,b.y+c.y,b.z+c.z)),
				ca=Normalized(p3(c.x+a.x,c.y+a.y,c.z+a.z));
			P3 t4[12]={a,ab,ca, ab,b,bc, ca,bc,c, ab,bc,ca};
			next.insert(next.end(),t4,t4+12);
		}
		tris.swap(next);
	}
}

//a smooth noise on the unit sphere, for lumps
static float SphereNoise(P3 d,unsigned seed)
{
	float n=0;
	unsigned s=seed;
	for(int i=0;i<5;i++)
	{
		P3 a=Normalized(p3(Rand01(s)*2-1,Rand01(s)*2-1,Rand01(s)*2-1));
		float f=2+Rand01(s)*3,ph=Rand01(s)*6.28f;
		n+=sinf((a.x*d.x+a.y*d.y+a.z*d.z)*f+ph)/(1+i*.5f);
	}
	return n*.35f;
}

//a lumpy ball (a crown) or a rock (flat: its flat bottom, as a fraction of r.y)
static void Blob(PropMesh &m,P3 c,P3 r,int subdiv,unsigned seed,float amp,float flat,int tex)
{
	Part(m,tex);
	std::vector<P3> tris;
	Icosphere(subdiv,tris);
	for(size_t i=0;i<tris.size();i+=3)
	{
		P3 p[3];
		for(int k=0;k<3;k++)
		{
			P3 d=tris[i+k];
			float s=1+SphereNoise(d,seed)*amp;
			p[k]=p3(c.x+d.x*r.x*s,c.y+d.y*r.y*s,c.z+d.z*r.z*s);
			if(flat>0&&p[k].y<c.y-r.y*flat) p[k].y=c.y-r.y*flat;
		}
		for(int k=0;k<3;k++)
		{
			P3 d=tris[i+k];
			float ao=.55f+.45f*(d.y*.5f+.5f);
			Vert(m,p[k],d,(p[k].x+p[k].z)*.35f,p[k].y*.35f,ao);
		}
	}
}

static void MakeMeshes()
{
	static const float rx[3]={1,.8f,1.2f},ry[3]={.6f,.75f,.45f},rz[3]={.85f,.9f,.7f};
	for(int far=0;far<2;far++)
	{
		//conifer, 9 units tall (Ultra's)
		PropMesh &c=meshes[0][far];
		float h=9;
		Trunk(c,.22f,.12f,2.4f,far?4:6);
		Part(c,TEX_NEEDLES);
		int layers=far?2:4;
		for(int i=0;i<layers;i++)
		{
			float y0=1.4f+i*(h-2.2f)/layers,y1=y0+(h-y0)*.75f+.6f;
			Cone(c,y0,y1>h?h:y1,2.4f*(1-.17f*i*(5.0f/layers)),far?6:9,77+i);
		}
		//broadleaf, 7.5 units
		PropMesh &t=meshes[1][far];
		Trunk(t,.3f,.16f,4.4f,far?4:6);
		unsigned s=99;
		int blobs=far?2:4;
		for(int i=0;i<blobs;i++)
		{
			float a=Rand01(s)*6.28f,d=i?.9f+Rand01(s)*1.1f:0;
			P3 bc=p3(cosf(a)*d,4.6f+Rand01(s)*2.0f-(i?.4f:0),sinf(a)*d);
			float r=(far?1.9f:1.5f)+Rand01(s)*.9f;
			Blob(t,bc,p3(r,r*.85f,r),far?0:1,200+i,.6f,0,TEX_LEAVES);
		}
		//rocks
		for(int k=0;k<3;k++)
			Blob(meshes[2+k][far],p3(0,0,0),p3(rx[k],ry[k],rz[k]),far?0:2,400+k,.4f,.35f,TEX_ROCK);
	}
}

//---- drawing ----

void DrawProps(const Vector &eye,const Vector &dir)
{
	const std::vector<Prop> &props=PropsList();
	if(props.empty()||!PropsEnabled()) return;
	if(!made)
	{
		MakeTextures();
		MakeMeshes();
		made=1;
	}
	RPushAttrib(RATTR_ENABLE|RATTR_TEXTURE|RATTR_CURRENT|RATTR_POLYGON);
	REnable(RCAP_TEXTURE|RCAP_DEPTHTEST);
	RDisable(RCAP_BLEND|RCAP_CULL);
	RSetMaterial(RMAT_MODEL);
	float fx=dir.x,fz=dir.z;
	for(size_t i=0;i<props.size();i++)
	{
		if(!PropThere((int)i)) continue;
		const Prop &p=props[i];
		//the copy of the prop nearest the camera (the world repeats)
		float dx=PropWrap(p.x-eye.x,LANDGRIDSIZE),dz=PropWrap(p.z-eye.z,LANDGRIDSIZE);
		float d2=dx*dx+dz*dz;
		if(d2>PROPSRANGE*PROPSRANGE) continue;
		if(dx*fx+dz*fz<-15&&d2>30*30) continue;   //well behind the camera
		int far=d2>PROPSFAR*PROPSFAR;
		int mesh=p.kind==PROP_ROCK?2+p.variant%3:(p.kind==PROP_TREE?1:0);
		const PropMesh &m=meshes[mesh][far];
		float y=p.kind==PROP_ROCK?p.y:p.y-.15f;
		float turn=PropDetail(p,PROPTURN)*360;
		Mat4 model=Mat4Mul(Mat4Mul(Mat4Translate(eye.x+dx,y,eye.z+dz),Mat4Rotate(turn,0,1,0)),
			Mat4Scale(p.scale,p.scale,p.scale));
		RDrawMesh(model,m.verts.data(),m.parts.data(),(int)m.parts.size());
	}
	RSetMaterial(RMAT_NONE);
	RPopAttrib();
}
