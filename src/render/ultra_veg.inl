//Trees, bushes and rocks of the Ultra graphics, included by renderer_ultra.cpp.
//
//Purely visual: no collision, nothing on the network. Each client places them from
//the land alone (a hash of the cell they stand in, the land's height, slope and
//water), so every client sees the same ones; never on blocks (RUltraScene's
//blocked), nor near the flags and stations (RUltraScene's avoid). They can hide a
//player that a Classic player sees (BUILD.md).
//
//The meshes are made here (no files): conifers of stacked cones, broadleaf trees
//and bushes of lumpy blobs, rocks of displaced spheres; each with a lighter mesh
//for far away. Placed again every frame around the camera (a few thousand cells),
//drawn instanced, swaying in the grass's wind, into the shadow maps and the water's
//reflection too.

struct VegVertex
{
	float p[3],n[3],a[3];      //a: part (0 trunk, 1 leaves, 2 rock), occlusion, sway
};

struct VegInstance
{
	float pos[3],scale;
	float turn,tint,pad,phase;
};

enum
{
	VEG_CONIFER,VEG_CONIFERFAR,VEG_TREE,VEG_TREEFAR,VEG_BUSH,VEG_BUSHFAR,
	VEG_ROCK0,VEG_ROCK1,VEG_ROCK2,VEG_ROCK0FAR,VEG_ROCK1FAR,VEG_ROCK2FAR,
	VEG_NUMMESHES
};

static sg_shader shveg,shvegshadow;
static sg_pipeline vegpipe,vegshadowpipe;
static sg_buffer vegvb,veginst;
static int vegfirst[VEG_NUMMESHES],vegcount[VEG_NUMMESHES];
static std::vector<VegInstance> veglists[VEG_NUMMESHES];
static int veginstfirst[VEG_NUMMESHES];
static int vegtotal;
#define VEGMAXINST 16384

static unsigned VegHash(int x,int z,unsigned seed)
{
	unsigned v=(unsigned)x*73856093u^(unsigned)z*19349663u^seed*83492791u;
	v^=v>>13;
	v*=0x5bd1e995u;
	v^=v>>15;
	v*=0x27d4eb2du;
	v^=v>>16;
	return v;
}

static float VegRand(unsigned &s)
{
	s=s*1664525u+1013904223u;
	return (s>>8)/16777216.0f;
}

//a smooth noise on the unit sphere, for lumps
static float SphereNoise(V3 d,unsigned seed)
{
	float n=0;
	unsigned s=seed;
	for(int i=0;i<5;i++)
	{
		V3 a=Norm(v3(VegRand(s)*2-1,VegRand(s)*2-1,VegRand(s)*2-1));
		float f=2+VegRand(s)*3,ph=VegRand(s)*6.28f;
		n+=sinf(Dot(a,d)*f+ph)/(1+i*0.5f);
	}
	return n*0.35f;
}

static void Icosphere(int subdiv,std::vector<V3> &tris)
{
	const float t=1.618034f;
	V3 v[12]={v3(-1,t,0),v3(1,t,0),v3(-1,-t,0),v3(1,-t,0),v3(0,-1,t),v3(0,1,t),v3(0,-1,-t),v3(0,1,-t),
		v3(t,0,-1),v3(t,0,1),v3(-t,0,-1),v3(-t,0,1)};
	static const int f[20][3]={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
		{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
	tris.clear();
	for(int i=0;i<20;i++)
		for(int k=0;k<3;k++) tris.push_back(Norm(v[f[i][k]]));
	for(int s=0;s<subdiv;s++)
	{
		std::vector<V3> next;
		for(size_t i=0;i<tris.size();i+=3)
		{
			V3 a=tris[i],b=tris[i+1],c=tris[i+2];
			V3 ab=Norm(Scale(v3(a.x+b.x,a.y+b.y,a.z+b.z),0.5f)),bc=Norm(Scale(v3(b.x+c.x,b.y+c.y,b.z+c.z),0.5f)),
				ca=Norm(Scale(v3(c.x+a.x,c.y+a.y,c.z+a.z),0.5f));
			V3 t4[12]={a,ab,ca, ab,b,bc, ca,bc,c, ab,bc,ca};
			next.insert(next.end(),t4,t4+12);
		}
		tris.swap(next);
	}
}

static void VegPush(std::vector<VegVertex> &out,V3 p,V3 n,float part,float ao,float sway)
{
	VegVertex v;
	v.p[0]=p.x;
	v.p[1]=p.y;
	v.p[2]=p.z;
	v.n[0]=n.x;
	v.n[1]=n.y;
	v.n[2]=n.z;
	v.a[0]=part;
	v.a[1]=ao;
	v.a[2]=sway;
	out.push_back(v);
}

//a lumpy ball (leaves) or a rock: the sphere pushed by noise, radii per axis
static void Blob(std::vector<VegVertex> &out,V3 c,V3 r,int subdiv,unsigned seed,float part,float amp,float height,float flat)
{
	std::vector<V3> tris;
	Icosphere(subdiv,tris);
	for(size_t i=0;i<tris.size();i+=3)
	{
		V3 p[3];
		for(int k=0;k<3;k++)
		{
			V3 d=tris[i+k];
			float s=1+SphereNoise(d,seed)*amp;
			p[k]=v3(c.x+d.x*r.x*s,c.y+d.y*r.y*s,c.z+d.z*r.z*s);
			if(flat>0&&p[k].y<c.y-r.y*flat) p[k].y=c.y-r.y*flat;   //a rock's flat bottom
		}
		V3 fn=Norm(Cross(Sub(p[1],p[0]),Sub(p[2],p[0])));
		if(Dot(fn,tris[i])<0) fn=Scale(fn,-1);
		for(int k=0;k<3;k++)
		{
			V3 d=tris[i+k];
			//rocks keep their facets a little, leaves are soft
			V3 n=Norm(part>1.5f?v3(d.x*0.5f+fn.x*0.5f,d.y*0.5f+fn.y*0.5f,d.z*0.5f+fn.z*0.5f):d);
			float ao=part>1.5f?0.6f+0.4f*Clamp(d.y*0.5f+0.6f,0,1):0.45f+0.55f*Clamp(d.y*0.5f+0.5f,0,1);
			VegPush(out,p[k],n,part,ao,height>0?Clamp(p[k].y/height,0,1):0);
		}
	}
}

static void Cylinder(std::vector<VegVertex> &out,float r0,float r1,float h,int sides,float height)
{
	for(int i=0;i<sides;i++)
	{
		float a0=6.2831853f*i/sides,a1=6.2831853f*(i+1)/sides;
		V3 n0=v3(cosf(a0),0.15f,sinf(a0)),n1=v3(cosf(a1),0.15f,sinf(a1));
		V3 b0=v3(cosf(a0)*r0,0,sinf(a0)*r0),b1=v3(cosf(a1)*r0,0,sinf(a1)*r0);
		V3 t0=v3(cosf(a0)*r1,h,sinf(a0)*r1),t1=v3(cosf(a1)*r1,h,sinf(a1)*r1);
		VegPush(out,b0,Norm(n0),0,0.5f,0);
		VegPush(out,t0,Norm(n0),0,1,h/height);
		VegPush(out,b1,Norm(n1),0,0.5f,0);
		VegPush(out,b1,Norm(n1),0,0.5f,0);
		VegPush(out,t0,Norm(n0),0,1,h/height);
		VegPush(out,t1,Norm(n1),0,1,h/height);
	}
}

//a conifer's layer: a jagged cone from radius r at y0 to its tip at y1
static void Cone(std::vector<VegVertex> &out,float y0,float y1,float r,int sides,unsigned seed,float height)
{
	unsigned s=seed;
	std::vector<float> rr(sides);
	for(int i=0;i<sides;i++) rr[i]=r*(0.8f+0.4f*VegRand(s));
	for(int i=0;i<sides;i++)
	{
		int j=(i+1)%sides;
		float a0=6.2831853f*i/sides,a1=6.2831853f*j/sides;
		V3 b0=v3(cosf(a0)*rr[i],y0-0.3f*VegRand(s),sinf(a0)*rr[i]),b1=v3(cosf(a1)*rr[j],y0,sinf(a1)*rr[j]);
		V3 tip=v3(0,y1,0);
		float slope=r/(y1-y0);
		V3 n0=Norm(v3(cosf(a0),slope,sinf(a0))),n1=Norm(v3(cosf(a1),slope,sinf(a1)));
		VegPush(out,b0,n0,1,0.55f,b0.y/height);
		VegPush(out,tip,v3(0,1,0),1,1,1);
		VegPush(out,b1,n1,1,0.55f,b1.y/height);
		//underside, darker
		VegPush(out,b0,v3(0,-1,0),1,0.3f,b0.y/height);
		VegPush(out,b1,v3(0,-1,0),1,0.3f,b1.y/height);
		VegPush(out,v3(0,y0+0.4f,0),v3(0,-1,0),1,0.2f,y0/height);
	}
}

static void MakeVegMeshes()
{
	std::vector<VegVertex> v;
	for(int m=0;m<VEG_NUMMESHES;m++)
	{
		vegfirst[m]=(int)v.size();
		int far=m==VEG_CONIFERFAR||m==VEG_TREEFAR||m==VEG_BUSHFAR||m>=VEG_ROCK0FAR;
		switch(m)
		{
		case VEG_CONIFER:
		case VEG_CONIFERFAR:
		{
			float h=9;
			Cylinder(v,0.22f,0.12f,2.0f,far?5:8,h);
			int layers=far?3:5,sides=far?7:14;
			for(int i=0;i<layers;i++)
			{
				float y0=1.4f+i*(h-2.2f)/layers,y1=y0+(h-y0)*0.75f+0.6f;
				Cone(v,y0,y1>h?h:y1,2.4f*(1-0.17f*i*(5.0f/layers)),sides,77+i,h);
			}
			break;
		}
		case VEG_TREE:
		case VEG_TREEFAR:
		{
			float h=7.5f;
			Cylinder(v,0.3f,0.16f,4.2f,far?5:8,h);
			unsigned s=99;
			int blobs=far?4:7;
			for(int i=0;i<blobs;i++)
			{
				float a=VegRand(s)*6.28f,d=i?0.9f+VegRand(s)*1.1f:0;
				V3 c=v3(cosf(a)*d,4.6f+VegRand(s)*2.0f-(i?0.4f:0),sinf(a)*d);
				float r=1.4f+VegRand(s)*0.9f;
				Blob(v,c,v3(r,r*0.85f,r),far?0:2,200+i,1,0.6f,h,0);
			}
			break;
		}
		case VEG_BUSH:
		case VEG_BUSHFAR:
		{
			unsigned s=55;
			for(int i=0;i<(far?2:4);i++)
			{
				float a=VegRand(s)*6.28f,d=i?0.5f+VegRand(s)*0.4f:0;
				float r=0.6f+VegRand(s)*0.35f;
				Blob(v,v3(cosf(a)*d,r*0.75f,sinf(a)*d),v3(r,r*0.8f,r),far?1:2,300+i,1,0.3f,1.6f,0);
			}
			break;
		}
		default:
		{
			int k=(m-VEG_ROCK0)%3;
			static const float rx[3]={1,0.8f,1.2f},ry[3]={0.6f,0.75f,0.45f},rz[3]={0.85f,0.9f,0.7f};
			Blob(v,v3(0,0,0),v3(rx[k],ry[k],rz[k]),far?1:3,400+k,2,0.4f,0,0.35f);
			break;
		}
		}
		vegcount[m]=(int)v.size()-vegfirst[m];
	}
	sg_buffer_desc bd;
	memset(&bd,0,sizeof(bd));
	bd.data.ptr=v.data();
	bd.data.size=v.size()*sizeof(VegVertex);
	bd.label="ultra vegetation meshes";
	vegvb=sg_make_buffer(&bd);
	memset(&bd,0,sizeof(bd));
	bd.size=VEGMAXINST*sizeof(VegInstance);
	bd.usage.vertex_buffer=true;
	bd.usage.write_transient=true;
	bd.label="ultra vegetation instances";
	veginst=sg_make_buffer(&bd);
}

static void VegInit()
{
	sg_backend b=sg_query_backend();
	shveg=sg_make_shader(veg_shader_desc(b));
	shvegshadow=sg_make_shader(vegshadow_shader_desc(b));
	for(int shadow=0;shadow<2;shadow++)
	{
		sg_pipeline_desc d;
		memset(&d,0,sizeof(d));
		d.shader=shadow?shvegshadow:shveg;
		d.layout.buffers[0].stride=sizeof(VegVertex);
		d.layout.buffers[1].stride=sizeof(VegInstance);
		d.layout.buffers[1].step_func=SG_VERTEXSTEP_PER_INSTANCE;
		d.layout.attrs[0].format=SG_VERTEXFORMAT_FLOAT3;
		d.layout.attrs[0].offset=offsetof(VegVertex,p);
		d.layout.attrs[1].format=SG_VERTEXFORMAT_FLOAT3;
		d.layout.attrs[1].offset=offsetof(VegVertex,n);
		d.layout.attrs[2].format=SG_VERTEXFORMAT_FLOAT3;
		d.layout.attrs[2].offset=offsetof(VegVertex,a);
		d.layout.attrs[3].format=SG_VERTEXFORMAT_FLOAT4;
		d.layout.attrs[3].buffer_index=1;
		d.layout.attrs[3].offset=0;
		d.layout.attrs[4].format=SG_VERTEXFORMAT_FLOAT4;
		d.layout.attrs[4].buffer_index=1;
		d.layout.attrs[4].offset=16;
		d.depth.pixel_format=SG_PIXELFORMAT_DEPTH;
		d.depth.compare=shadow?SG_COMPAREFUNC_LESS_EQUAL:SG_COMPAREFUNC_LESS;
		d.depth.write_enabled=true;
		if(shadow)
		{
			d.depth.bias=1.0f;
			d.depth.bias_slope_scale=1.5f;
			d.colors[0].pixel_format=SG_PIXELFORMAT_NONE;
		}
		else d.colors[0].pixel_format=SG_PIXELFORMAT_RGBA16F;
		d.cull_mode=SG_CULLMODE_NONE;
		d.label=shadow?"ultra vegetation shadow":"ultra vegetation";
		(shadow?vegshadowpipe:vegpipe)=sg_make_pipeline(&d);
	}
	MakeVegMeshes();
}

//the land's height and normal at x, z (it repeats every landsize)
static float VegLand(float x,float z,V3 *n)
{
	const float *land=scene.land;
	int size=scene.landsize;
	int ix=(int)floorf(x),iz=(int)floorf(z);
	float fx=x-ix,fz=z-iz;
	float h=(LandAt(land,size,ix,iz)*(1-fx)+LandAt(land,size,ix+1,iz)*fx)*(1-fz)+
		(LandAt(land,size,ix,iz+1)*(1-fx)+LandAt(land,size,ix+1,iz+1)*fx)*fz;
	if(n)
	{
		float dx=(LandAt(land,size,ix+2,iz)-LandAt(land,size,ix-2,iz))/4;
		float dz=(LandAt(land,size,ix,iz+2)-LandAt(land,size,ix,iz-2))/4;
		*n=Norm(v3(-dx,1,-dz));
	}
	return h;
}

static int VegAvoid(float x,float y,float z,float radius)
{
	for(int i=0;i<scene.numavoid;i++)
	{
		float dx=x-scene.avoid[i][0],dz=z-scene.avoid[i][2];
		if(dx*dx+dz*dz<radius*radius) return 1;
	}
	if(scene.blocked&&(scene.blocked(x,y+0.5f,z)||scene.blocked(x,y+2.5f,z))) return 1;
	return 0;
}

static void VegAdd(int mesh,float x,float y,float z,float scale,unsigned &s)
{
	if(vegtotal>=VEGMAXINST) return;
	VegInstance in;
	in.pos[0]=x;
	in.pos[1]=y;
	in.pos[2]=z;
	in.scale=scale;
	in.turn=VegRand(s)*6.2831853f;
	in.tint=VegRand(s);
	in.pad=0;
	in.phase=VegRand(s)*6.2831853f;
	veglists[mesh].push_back(in);
	vegtotal++;
}

//what stands around the camera this frame
static void VegPlace()
{
	for(int m=0;m<VEG_NUMMESHES;m++) veglists[m].clear();
	vegtotal=0;
	int q=rultraprefs.vegetation;
	if(q<=0||!scene.land||scene.landsize<=0) return;
	int size=scene.landsize;
	float w=scene.waterheight;
	float ex=scene.eye[0],ez=scene.eye[2];
	unsigned seed=landhash;
	//trees and bushes: an 8-unit cell each at most, thicker in forests
	const int tc=8;
	int tcells=size/tc;
	float trange=q>=2?260.0f:170.0f;
	int r=(int)(trange/tc);
	int cx0=(int)floorf(ex/tc),cz0=(int)floorf(ez/tc);
	for(int dz=-r;dz<=r;dz++)
		for(int dx=-r;dx<=r;dx++)
		{
			int cx=cx0+dx,cz=cz0+dz;
			int hx=((cx%tcells)+tcells)%tcells,hz=((cz%tcells)+tcells)%tcells;
			unsigned s=VegHash(hx,hz,seed);
			float forest=LandNoise(hx*tc,hz*tc,64,size,11);
			float p=forest>0.56f?0.75f:0.07f;
			if(VegRand(s)>p) continue;
			float x=(cx+0.15f+0.7f*VegRand(s))*tc,z=(cz+0.15f+0.7f*VegRand(s))*tc;
			float ddx=x-ex,ddz=z-ez,d2=ddx*ddx+ddz*ddz;
			if(d2>trange*trange) continue;
			V3 n;
			float h=VegLand(x,z,&n);
			if(h<w+1.5f||1-n.y>0.28f||h>w+60) continue;
			if(VegAvoid(x,h,z,14)) continue;
			float k=VegRand(s);
			int far=d2>75*75;
			int mesh;
			float scale;
			if(k<0.4f||h>w+30)
			{
				mesh=far?VEG_CONIFERFAR:VEG_CONIFER;
				scale=0.8f+VegRand(s)*0.6f;
			}
			else if(k<0.75f)
			{
				mesh=far?VEG_TREEFAR:VEG_TREE;
				scale=0.75f+VegRand(s)*0.5f;
			}
			else
			{
				mesh=far?VEG_BUSHFAR:VEG_BUSH;
				scale=0.7f+VegRand(s)*0.8f;
			}
			VegAdd(mesh,x,h-0.15f,z,scale,s);
			//a bush or two at a tree's foot
			if(mesh!=VEG_BUSH&&mesh!=VEG_BUSHFAR&&VegRand(s)<0.4f&&!far)
			{
				float a=VegRand(s)*6.28f,bx=x+cosf(a)*2.2f,bz=z+sinf(a)*2.2f;
				float bh=VegLand(bx,bz,0);
				if(bh>w+1&&!VegAvoid(bx,bh,bz,12)) VegAdd(VEG_BUSH,bx,bh-0.1f,bz,0.6f+VegRand(s)*0.5f,s);
			}
		}
	//rocks: a 4-unit cell each at most, more on slopes and shores, some in clusters
	const int rc=4;
	int rcells=size/rc;
	float rrange=q>=2?150.0f:90.0f;
	r=(int)(rrange/rc);
	cx0=(int)floorf(ex/rc);
	cz0=(int)floorf(ez/rc);
	for(int dz=-r;dz<=r;dz++)
		for(int dx=-r;dx<=r;dx++)
		{
			int cx=cx0+dx,cz=cz0+dz;
			int hx=((cx%rcells)+rcells)%rcells,hz=((cz%rcells)+rcells)%rcells;
			unsigned s=VegHash(hx,hz,seed^0x9e3779b9u);
			float x=(cx+VegRand(s))*rc,z=(cz+VegRand(s))*rc;
			float ddx=x-ex,ddz=z-ez,d2=ddx*ddx+ddz*ddz;
			if(d2>rrange*rrange) continue;
			V3 n;
			float h=VegLand(x,z,&n);
			float slope=1-n.y;
			float shore=(h>w-1.5f&&h<w+2)?0.12f:0;
			float p=0.012f+Clamp(slope-0.15f,0,0.5f)*0.5f+shore;
			if(VegRand(s)>p||h<w-3) continue;
			if(VegAvoid(x,h,z,12)) continue;
			int far=d2>55*55;
			int count=VegRand(s)<0.3f?3:1;
			for(int i=0;i<count;i++)
			{
				float rx=x+(i?(VegRand(s)-0.5f)*3:0),rz=z+(i?(VegRand(s)-0.5f)*3:0);
				float rh=i?VegLand(rx,rz,0):h;
				//mostly stones, now and then a boulder
				float big=VegRand(s);
				float scale=(i?0.25f:0.35f)+big*big*big*(i?0.6f:2.4f);
				int k=(int)(VegRand(s)*3)%3;
				VegAdd((far?VEG_ROCK0FAR:VEG_ROCK0)+k,rx,rh-0.2f*scale,rz,scale,s);
			}
		}
	//one buffer, the meshes' instances one after the other
	static std::vector<VegInstance> all;
	all.clear();
	for(int m=0;m<VEG_NUMMESHES;m++)
	{
		veginstfirst[m]=(int)all.size();
		all.insert(all.end(),veglists[m].begin(),veglists[m].end());
	}
	if(!all.empty())
	{
		sg_write_buffer_desc wd;
		memset(&wd,0,sizeof(wd));
		wd.src.data.ptr=all.data();
		wd.src.data.size=all.size()*sizeof(VegInstance);
		wd.dst.buffer=veginst;
		sg_write_buffer_transient(&wd);
	}
}

static void VegDraw(const Mat4 &viewproj,int shadow,int clip)
{
	if(!vegtotal) return;
	sg_apply_pipeline(shadow?vegshadowpipe:vegpipe);
	veg_vs_params_t vs;
	memset(&vs,0,sizeof(vs));
	vs.viewproj=viewproj;
	Set4(vs.cam,scene.eye[0],scene.eye[1],scene.eye[2],scene.time);
	float wa=0.6f+0.3f*sinf(scene.time*0.05f);   //the grass's wind
	Set4(vs.wind,cosf(wa),sinf(wa),0.35f+overcast*0.35f,0);
	int applied=0;
	for(int m=0;m<VEG_NUMMESHES;m++)
	{
		int n=(int)veglists[m].size();
		if(!n) continue;
		sg_bindings b;
		memset(&b,0,sizeof(b));
		b.vertex_buffers[0]=vegvb;
		b.vertex_buffers[1]=veginst;
		b.vertex_buffer_offsets[1]=veginstfirst[m]*(int)sizeof(VegInstance);
		if(!shadow)
		{
			b.views[VIEW_skylut]=skylut;
			b.views[VIEW_hdalb]=hdalb;
			b.views[VIEW_hdnrm]=hdnrm;
			b.views[VIEW_hdorm]=hdorm;
			b.views[VIEW_shadowmap]=shadowatlas.tex;
			b.samplers[SMP_lutsmp]=lutsampler;
			b.samplers[SMP_hdsmp]=hdsampler;
			b.samplers[SMP_shadowsmp]=shadowsampler;
		}
		sg_apply_bindings(&b);
		if(!applied)
		{
			sg_range r={&vs,sizeof(vs)};
			sg_apply_uniforms(UB_veg_vs_params,&r);
			if(!shadow)
			{
				veg_fs_params_t fs;
				Mat4 inv;
				RSokolInverse(scene.view,&inv);
				Set4(fs.view,-inv.m[8],-inv.m[9],-inv.m[10],clip?1.0f:0.0f);
				r.ptr=&fs;
				r.size=sizeof(fs);
				sg_apply_uniforms(UB_veg_fs_params,&r);
				r.ptr=&sp;
				r.size=sizeof(sp);
				sg_apply_uniforms(UB_scene_params,&r);
			}
			applied=1;
		}
		sg_draw(vegfirst[m],vegcount[m],n);
	}
}
