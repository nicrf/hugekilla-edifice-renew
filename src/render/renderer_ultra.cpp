//The Ultra graphics (renderer.h), drawn by the sokol_gfx backend: renderer_sokol.cpp
//records the frame as for Classic, with marks where the 3D scene begins and ends and
//where the water and the grass go; between them this file draws the scene its way,
//then post-processes it into the frame, under the HUD.
//
//A frame:
//  1. the land's textures (heights, normals, splat weights) when the land changed,
//     and the sky's (a LUT computed on the CPU) when the sun or the map changed;
//  2. the shadow maps: the sun's view of the land, blocks and models, cascades in
//     a 2x2 atlas;
//  3. the water's planar reflection: the same draws, mirrored, at half size;
//  4. the scene in HDR (RGBA16F): the sky, then the game's draws, lit (shaders/
//     ultra.glsl, "lit") or classic ("hdrdraw"); at the water's mark a copy of
//     the color and depth so far, which the water refracts;
//  5. bloom (downsample and upsample chains), god rays (a radial blur of the sky
//     towards the sun), then exposure and ACES tonemapping into an RGBA8 image,
//     and FXAA from it into the frame.
//
//WebGL2 has no float render targets everywhere: the web build stays Classic.

#include "rbackend.h"
#include "rsokol.h"
#include "textures.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#if defined(SOKOL_GLES3) || defined(SOKOL_WGPU) || defined(SOKOL_DUMMY_BACKEND)

int RUltraBackendSupported()
{
	return 0;
}
void RUltraInit() {}
void RUltraShutdown() {}
void RUltraSetScene(const RUltraScene &) {}
void RUltraDrawScene(const RSokolFrame &,const RSokolCmd *,int) {}

#else

#include "ultra.glsl.h"

//---- small math ----

struct V3
{
	float x,y,z;
};
static V3 v3(float x,float y,float z)
{
	V3 r={x,y,z};
	return r;
}
static V3 Sub(V3 a,V3 b)
{
	return v3(a.x-b.x,a.y-b.y,a.z-b.z);
}
static V3 Scale(V3 a,float s)
{
	return v3(a.x*s,a.y*s,a.z*s);
}
static float Dot(V3 a,V3 b)
{
	return a.x*b.x+a.y*b.y+a.z*b.z;
}
static V3 Cross(V3 a,V3 b)
{
	return v3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
}
static V3 Norm(V3 a)
{
	float l=sqrtf(Dot(a,a));
	return l>0?Scale(a,1/l):a;
}
static float Clamp(float v,float lo,float hi)
{
	return v<lo?lo:v>hi?hi:v;
}
static float Smooth(float e0,float e1,float x)
{
	float t=Clamp((x-e0)/(e1-e0),0,1);
	return t*t*(3-2*t);
}
static void Xform(const Mat4 &m,const float in[4],float out[4])
{
	for(int i=0;i<4;i++)
		out[i]=m.m[i]*in[0]+m.m[4+i]*in[1]+m.m[8+i]*in[2]+m.m[12+i]*in[3];
}
static void Set4(float *d,float a,float b,float c,float e)
{
	d[0]=a;
	d[1]=b;
	d[2]=c;
	d[3]=e;
}

//the Ultra assets' own random numbers: rand() is the game's (its world, its sky)
static unsigned urandseed=1;
static int URand()
{
	urandseed=urandseed*1103515245u+12345u;
	return (int)((urandseed>>16)&0x7fff);
}

//IEEE half from float (the sky LUT and nothing hot)
static unsigned short Half(float f)
{
	unsigned int x;
	memcpy(&x,&f,4);
	unsigned int sign=(x>>16)&0x8000;
	int e=(int)((x>>23)&0xff)-127+15;
	unsigned int m=x&0x7fffff;
	if(e<=0) return (unsigned short)sign;
	if(e>=31) return (unsigned short)(sign|0x7c00);
	return (unsigned short)(sign|(e<<10)|(m>>13));
}

//---- resources ----

static sg_shader shhdr,shlit,shshadow,shsky,shwater,shgrass,shcopy,shdown,shup,shrays,shcomposite,shfxaa;
#define NUMKEYS (2*6*2*2*3*2)     //renderer_sokol.cpp's classic pipeline keys
static sg_pipeline hdrpipes[NUMKEYS],litpipes[NUMKEYS];
static sg_pipeline shadowpipe,skypipe,waterpipe,grasspipe,copypipe,downpipe,uppipe,rayspipe,compositepipe,fxaapipe;
static sg_sampler linclamp,pointclamp,linrepeat,hdsampler,shadowsampler,lutsampler;
static int inited,failed;

struct Target
{
	sg_image image;
	sg_view att,tex;
	int w,h;
};

static void MakeTarget(Target &t,int w,int h,sg_pixel_format fmt,int depth,const char *label)
{
	sg_image_desc id;
	memset(&id,0,sizeof(id));
	id.width=w;
	id.height=h;
	id.pixel_format=fmt;
	id.sample_count=1;
	if(depth) id.usage.depth_stencil_attachment=true;
	else id.usage.color_attachment=true;
	id.label=label;
	t.image=sg_make_image(&id);
	sg_view_desc vd;
	memset(&vd,0,sizeof(vd));
	if(depth) vd.depth_stencil_attachment.image=t.image;
	else vd.color_attachment.image=t.image;
	t.att=sg_make_view(&vd);
	memset(&vd,0,sizeof(vd));
	vd.texture.image=t.image;
	t.tex=sg_make_view(&vd);
	t.w=w;
	t.h=h;
}

static void FreeTarget(Target &t)
{
	if(t.tex.id) sg_destroy_view(t.tex);
	if(t.att.id) sg_destroy_view(t.att);
	if(t.image.id) sg_destroy_image(t.image);
	memset(&t,0,sizeof(t));
}

#define NUMBLOOM 6
static Target hdr,hdrdepth,scenecopy,lindepth,refl,refldepth,bloom[NUMBLOOM],rays,ldr,shadowatlas,clouds;
static sg_shader shclouds;
static sg_pipeline cloudspipe;
static sg_image noise3dimg;
static sg_view noise3d;
static sg_sampler noisesampler;
static int targetw,targeth,shadowsize;

//textures
static sg_image skylutimg,heightimg,landnrmimg,landsplatimg,hdalbimg,hdnrmimg,hdormimg,wnormalimg;
static sg_view skylut,heightview,landnrm,landsplat,hdalb,hdnrm,hdorm,wnormal;
static int hdblocklayer[32];      //TEX_BLOCK0+i: its HD layer, -1 none
#define HDLAYERS 17               //grass rock sand snow dirt foam wall0..9 grass2
#define HDBLOCK0 6

//static geometry
static sg_buffer watergrid,wateridx,bladevb,bladeib,instnear,instfar;
static int waterindices;
#define BLADEINDICES 15
#define GRASSTILE 20.0f
#define GRASSFARTILE 60.0f
#define GRASSPERSIDE 200

static RUltraScene scene;
static unsigned landhash;
static int landvalid;
static float skykey[8];           //what the sky LUT was computed for
static float suncolor[3],skyzenith[3],skyhorizon[3],skyground[3],haze,classicscale,overcast,darkness;

static void VegInit();

static void Log(const char *what)
{
	fprintf(stderr,"Ultra: %s\n",what);
}

static sg_view TexView(sg_image img)
{
	sg_view_desc vd;
	memset(&vd,0,sizeof(vd));
	vd.texture.image=img;
	return sg_make_view(&vd);
}

//---- pipelines ----

static sg_pipeline_desc Fullscreen(sg_shader sh,sg_pixel_format fmt,const char *label)
{
	sg_pipeline_desc d;
	memset(&d,0,sizeof(d));
	d.shader=sh;
	d.depth.pixel_format=SG_PIXELFORMAT_NONE;
	d.colors[0].pixel_format=fmt;
	d.label=label;
	return d;
}

static sg_pipeline HdrPipe(int key)
{
	if(hdrpipes[key].id) return hdrpipes[key];
	sg_pipeline_desc d=RSokolPipeDesc(key);
	d.shader=shhdr;
	d.colors[0].pixel_format=SG_PIXELFORMAT_RGBA16F;
	d.label="ultra classic";
	hdrpipes[key]=sg_make_pipeline(&d);
	return hdrpipes[key];
}

static sg_pipeline LitPipe(int key)
{
	if(litpipes[key].id) return litpipes[key];
	sg_pipeline_desc d=RSokolPipeDesc(key);
	d.shader=shlit;
	d.layout.attrs[ATTR_lit_normal0].format=SG_VERTEXFORMAT_FLOAT3;
	d.layout.attrs[ATTR_lit_normal0].offset=offsetof(RVertex,nx);
	d.colors[0].pixel_format=SG_PIXELFORMAT_RGBA16F;
	d.label="ultra lit";
	litpipes[key]=sg_make_pipeline(&d);
	return litpipes[key];
}

//classic keys (renderer_sokol.cpp): ((((prim*6+blend)*2+depthtest)*2+depthwrite)*3+cull)*2+colorwrite
static int KeyPrim(int key)
{
	return key/(2*3*2*2*6);
}
static int KeyBlend(int key)
{
	return key/(2*3*2*2)%6;
}
static int KeyFlipCull(int key)
{
	int cull=key/2%3;
	if(!cull) return key;
	return key+((cull==1?2:1)-cull)*2;
}

static sg_sampler Sampler(sg_filter f,sg_filter mip,sg_wrap wrap,int aniso)
{
	sg_sampler_desc sd;
	memset(&sd,0,sizeof(sd));
	sd.min_filter=sd.mag_filter=f;
	sd.mipmap_filter=mip;
	sd.wrap_u=sd.wrap_v=wrap;
	sd.max_anisotropy=aniso;
	return sg_make_sampler(&sd);
}

int RUltraBackendSupported()
{
	if(failed||!sg_isvalid()) return 0;
	sg_pixelformat_info f=sg_query_pixelformat(SG_PIXELFORMAT_RGBA16F);
	sg_pixelformat_info r=sg_query_pixelformat(SG_PIXELFORMAT_R32F);
	return f.render&&f.filter&&f.blend&&r.render;
}

void RUltraInit()
{
	if(inited) return;
	inited=1;
	sg_backend b=sg_query_backend();
	shhdr=sg_make_shader(hdrdraw_shader_desc(b));
	shlit=sg_make_shader(lit_shader_desc(b));
	shshadow=sg_make_shader(shadow_shader_desc(b));
	shsky=sg_make_shader(sky_shader_desc(b));
	shwater=sg_make_shader(water_shader_desc(b));
	shgrass=sg_make_shader(grass_shader_desc(b));
	shcopy=sg_make_shader(copy_shader_desc(b));
	shdown=sg_make_shader(down_shader_desc(b));
	shup=sg_make_shader(up_shader_desc(b));
	shrays=sg_make_shader(rays_shader_desc(b));
	shcomposite=sg_make_shader(composite_shader_desc(b));
	shfxaa=sg_make_shader(fxaa_shader_desc(b));
	if(sg_query_shader_state(shlit)!=SG_RESOURCESTATE_VALID||sg_query_shader_state(shwater)!=SG_RESOURCESTATE_VALID||
		sg_query_shader_state(shgrass)!=SG_RESOURCESTATE_VALID||sg_query_shader_state(shcomposite)!=SG_RESOURCESTATE_VALID)
	{
		Log("the shaders didn't compile: Classic graphics");
		failed=1;
		return;
	}

	sg_pipeline_desc d;
	//depth only, both faces (thin walls), sloped bias against acne
	memset(&d,0,sizeof(d));
	d.shader=shshadow;
	d.layout.buffers[0].stride=sizeof(RVertex);
	d.layout.attrs[ATTR_shadow_position].format=SG_VERTEXFORMAT_FLOAT3;
	d.layout.attrs[ATTR_shadow_position].offset=offsetof(RVertex,x);
	d.depth.pixel_format=SG_PIXELFORMAT_DEPTH;
	d.depth.compare=SG_COMPAREFUNC_LESS_EQUAL;
	d.depth.write_enabled=true;
	d.depth.bias=1.0f;
	d.depth.bias_slope_scale=1.5f;
	d.colors[0].pixel_format=SG_PIXELFORMAT_NONE;
	d.cull_mode=SG_CULLMODE_NONE;
	d.label="ultra shadow";
	shadowpipe=sg_make_pipeline(&d);

	memset(&d,0,sizeof(d));
	d.shader=shsky;
	d.depth.pixel_format=SG_PIXELFORMAT_DEPTH;
	d.depth.compare=SG_COMPAREFUNC_ALWAYS;
	d.colors[0].pixel_format=SG_PIXELFORMAT_RGBA16F;
	d.label="ultra sky";
	skypipe=sg_make_pipeline(&d);

	memset(&d,0,sizeof(d));
	d.shader=shwater;
	d.layout.attrs[ATTR_water_position].format=SG_VERTEXFORMAT_FLOAT2;
	d.index_type=SG_INDEXTYPE_UINT32;
	d.depth.pixel_format=SG_PIXELFORMAT_DEPTH;
	d.depth.compare=SG_COMPAREFUNC_LESS;
	d.depth.write_enabled=true;
	d.colors[0].pixel_format=SG_PIXELFORMAT_RGBA16F;
	d.cull_mode=SG_CULLMODE_NONE;
	d.label="ultra water";
	waterpipe=sg_make_pipeline(&d);

	memset(&d,0,sizeof(d));
	d.shader=shgrass;
	d.layout.buffers[1].step_func=SG_VERTEXSTEP_PER_INSTANCE;
	d.layout.attrs[ATTR_grass_shape].format=SG_VERTEXFORMAT_FLOAT2;
	d.layout.attrs[ATTR_grass_shape].buffer_index=0;
	d.layout.attrs[ATTR_grass_inst].format=SG_VERTEXFORMAT_FLOAT4;
	d.layout.attrs[ATTR_grass_inst].buffer_index=1;
	d.index_type=SG_INDEXTYPE_UINT16;
	d.depth.pixel_format=SG_PIXELFORMAT_DEPTH;
	d.depth.compare=SG_COMPAREFUNC_LESS;
	d.depth.write_enabled=true;
	d.colors[0].pixel_format=SG_PIXELFORMAT_RGBA16F;
	d.cull_mode=SG_CULLMODE_NONE;
	d.label="ultra grass";
	grasspipe=sg_make_pipeline(&d);

	d=Fullscreen(shcopy,SG_PIXELFORMAT_RGBA16F,"ultra copy");
	d.color_count=2;
	d.colors[1].pixel_format=SG_PIXELFORMAT_R32F;
	copypipe=sg_make_pipeline(&d);
	d=Fullscreen(shdown,SG_PIXELFORMAT_RGBA16F,"ultra bloom down");
	downpipe=sg_make_pipeline(&d);
	d=Fullscreen(shup,SG_PIXELFORMAT_RGBA16F,"ultra bloom up");
	d.colors[0].blend.enabled=true;
	d.colors[0].blend.src_factor_rgb=d.colors[0].blend.src_factor_alpha=SG_BLENDFACTOR_ONE;
	d.colors[0].blend.dst_factor_rgb=d.colors[0].blend.dst_factor_alpha=SG_BLENDFACTOR_ONE;
	uppipe=sg_make_pipeline(&d);
	d=Fullscreen(shrays,SG_PIXELFORMAT_RGBA16F,"ultra god rays");
	rayspipe=sg_make_pipeline(&d);
	d=Fullscreen(shcomposite,SG_PIXELFORMAT_RGBA8,"ultra composite");
	compositepipe=sg_make_pipeline(&d);
	//into the frame, which has a depth buffer (cleared for the HUD)
	shclouds=sg_make_shader(clouds_shader_desc(b));
	d=Fullscreen(shclouds,SG_PIXELFORMAT_RGBA16F,"ultra clouds");
	cloudspipe=sg_make_pipeline(&d);
	d=Fullscreen(shfxaa,SG_PIXELFORMAT_RGBA8,"ultra fxaa");
	d.depth.pixel_format=SG_PIXELFORMAT_DEPTH;
	d.depth.compare=SG_COMPAREFUNC_ALWAYS;
	fxaapipe=sg_make_pipeline(&d);

	linclamp=Sampler(SG_FILTER_LINEAR,SG_FILTER_NEAREST,SG_WRAP_CLAMP_TO_EDGE,1);
	pointclamp=Sampler(SG_FILTER_NEAREST,SG_FILTER_NEAREST,SG_WRAP_CLAMP_TO_EDGE,1);
	linrepeat=Sampler(SG_FILTER_LINEAR,SG_FILTER_LINEAR,SG_WRAP_REPEAT,4);
	{
		sg_sampler_desc nd;
		memset(&nd,0,sizeof(nd));
		nd.min_filter=nd.mag_filter=SG_FILTER_LINEAR;
		nd.wrap_u=nd.wrap_v=nd.wrap_w=SG_WRAP_REPEAT;
		noisesampler=sg_make_sampler(&nd);
	}
	hdsampler=Sampler(SG_FILTER_LINEAR,SG_FILTER_LINEAR,SG_WRAP_REPEAT,8);
	sg_sampler_desc sd;
	memset(&sd,0,sizeof(sd));
	sd.min_filter=sd.mag_filter=SG_FILTER_LINEAR;
	sd.wrap_u=sd.wrap_v=SG_WRAP_CLAMP_TO_EDGE;
	sd.compare=SG_COMPAREFUNC_LESS_EQUAL;
	shadowsampler=sg_make_sampler(&sd);
	memset(&sd,0,sizeof(sd));
	sd.min_filter=sd.mag_filter=SG_FILTER_LINEAR;
	sd.wrap_u=SG_WRAP_REPEAT;
	sd.wrap_v=SG_WRAP_CLAMP_TO_EDGE;
	lutsampler=sg_make_sampler(&sd);
	for(int i=0;i<32;i++) hdblocklayer[i]=-1;
	VegInit();
}

static void FreeTargets()
{
	FreeTarget(hdr);
	FreeTarget(hdrdepth);
	FreeTarget(scenecopy);
	FreeTarget(lindepth);
	FreeTarget(refl);
	FreeTarget(refldepth);
	for(int i=0;i<NUMBLOOM;i++) FreeTarget(bloom[i]);
	FreeTarget(rays);
	FreeTarget(ldr);
	FreeTarget(clouds);
	targetw=targeth=0;
}

void RUltraShutdown()
{
	if(!inited) return;
	FreeTargets();
	FreeTarget(shadowatlas);
}

static void EnsureTargets(int w,int h)
{
	if(w==targetw&&h==targeth) return;
	FreeTargets();
	MakeTarget(hdr,w,h,SG_PIXELFORMAT_RGBA16F,0,"ultra hdr");
	MakeTarget(hdrdepth,w,h,SG_PIXELFORMAT_DEPTH,1,"ultra depth");
	MakeTarget(scenecopy,w,h,SG_PIXELFORMAT_RGBA16F,0,"ultra scene copy");
	MakeTarget(lindepth,w,h,SG_PIXELFORMAT_R32F,0,"ultra eye depth");
	int rw=w/2>1?w/2:1,rh=h/2>1?h/2:1;
	MakeTarget(refl,rw,rh,SG_PIXELFORMAT_RGBA16F,0,"ultra reflection");
	MakeTarget(refldepth,rw,rh,SG_PIXELFORMAT_DEPTH,1,"ultra reflection depth");
	int bw=w,bh=h;
	for(int i=0;i<NUMBLOOM;i++)
	{
		bw=bw/2>1?bw/2:1;
		bh=bh/2>1?bh/2:1;
		MakeTarget(bloom[i],bw,bh,SG_PIXELFORMAT_RGBA16F,0,"ultra bloom");
	}
	MakeTarget(rays,rw,rh,SG_PIXELFORMAT_RGBA16F,0,"ultra god rays");
	MakeTarget(ldr,w,h,SG_PIXELFORMAT_RGBA8,0,"ultra ldr");
	MakeTarget(clouds,w/4>1?w/4:1,h/4>1?h/4:1,SG_PIXELFORMAT_RGBA16F,0,"ultra clouds");
	targetw=w;
	targeth=h;
}

//---- HD textures (data/hd, tools/hd_textures.py) ----

static const char *hdnames[HDLAYERS]={"grass","rock","sand","snow","dirt","foam",
	"wall0","wall1","wall2","wall3","wall4","wall5","wall6","wall7","wall8","wall9","grass2"};

//one layer at size x size, RGBA, mip levels appended to levels[] (box filter)
static int LoadLayer(const char *file,int size,const unsigned char fallback[4],std::vector<std::vector<unsigned char>> &levels)
{
	int w=0,h=0;
	unsigned char *px=LoadImagePixels(file,&w,&h,4);
	std::vector<unsigned char> img((size_t)size*size*4);
	int ok=px&&w>0&&h>0;
	for(int y=0;y<size;y++)
		for(int x=0;x<size;x++)
		{
			unsigned char *o=&img[((size_t)y*size+x)*4];
			if(ok)
			{
				//nearest resize (the files are 1024: a power of two divides exactly)
				const unsigned char *i=px+((size_t)(y*h/size)*w+(x*w/size))*4;
				memcpy(o,i,4);
			}
			else memcpy(o,fallback,4);
		}
	if(px) FreeImagePixels(px);
	int s=size,level=0;
	for(;;)
	{
		levels[level].insert(levels[level].end(),img.begin(),img.end());
		if(s==1) break;
		int n=s/2;
		std::vector<unsigned char> next((size_t)n*n*4);
		for(int y=0;y<n;y++)
			for(int x=0;x<n;x++)
				for(int k=0;k<4;k++)
				{
					const unsigned char *t=&img[((size_t)(2*y)*s+2*x)*4+k];
					next[((size_t)y*n+x)*4+k]=(unsigned char)((t[0]+t[4]+t[(size_t)s*4]+t[(size_t)s*4+4]+2)/4);
				}
		img.swap(next);
		s=n;
		level++;
	}
	return ok;
}

static sg_image MakeArray(int size,const char *suffix,const unsigned char fallback[4],int *found)
{
	int nummips=0;
	for(int s=size;s>=1;s/=2) nummips++;
	std::vector<std::vector<unsigned char>> levels(nummips);
	for(int i=0;i<HDLAYERS;i++)
	{
		char file[64];
		snprintf(file,sizeof(file),"hd/%s%s.jpg",hdnames[i],suffix);
		int ok=LoadLayer(file,size,fallback,levels);
		if(found) found[i]=ok;
	}
	sg_image_desc id;
	memset(&id,0,sizeof(id));
	id.type=SG_IMAGETYPE_ARRAY;
	id.width=id.height=size;
	id.num_slices=HDLAYERS;
	id.num_mipmaps=nummips;
	id.pixel_format=SG_PIXELFORMAT_RGBA8;
	for(int m=0;m<nummips;m++)
	{
		id.data.mip_levels[m].ptr=levels[m].data();
		id.data.mip_levels[m].size=levels[m].size();
	}
	id.label="ultra hd";
	return sg_make_image(&id);
}

//a tileable normal map of small waves: sines of whole periods, so it repeats
static void MakeWaterNormals()
{
	const int n=256;
	std::vector<float> hgt((size_t)n*n,0);
	urandseed=1234;
	for(int k=0;k<48;k++)
	{
		int fx=URand()%15-7,fy=URand()%15-7;
		if(!fx&&!fy) fx=1;
		float f=sqrtf((float)(fx*fx+fy*fy));
		float amp=1/powf(f,1.4f);
		float ph=(float)URand()/32767.0f*6.2831853f;
		for(int y=0;y<n;y++)
			for(int x=0;x<n;x++)
				hgt[(size_t)y*n+x]+=amp*sinf(6.2831853f*(fx*x+fy*y)/n+ph);
	}
	std::vector<unsigned char> px((size_t)n*n*4);
	for(int y=0;y<n;y++)
		for(int x=0;x<n;x++)
		{
			float dx=hgt[(size_t)y*n+(x+1)%n]-hgt[(size_t)y*n+(x+n-1)%n];
			float dy=hgt[(size_t)((y+1)%n)*n+x]-hgt[(size_t)((y+n-1)%n)*n+x];
			V3 nn=Norm(v3(-dx*2.5f,-dy*2.5f,1));
			unsigned char *o=&px[((size_t)y*n+x)*4];
			o[0]=(unsigned char)(nn.x*127.5f+127.5f);
			o[1]=(unsigned char)(nn.y*127.5f+127.5f);
			o[2]=(unsigned char)(nn.z*127.5f+127.5f);
			o[3]=255;
		}
	//mips: box
	std::vector<std::vector<unsigned char>> levels;
	levels.push_back(px);
	int s=n;
	while(s>1)
	{
		int h=s/2;
		std::vector<unsigned char> next((size_t)h*h*4);
		const std::vector<unsigned char> &c=levels.back();
		for(int y=0;y<h;y++)
			for(int x=0;x<h;x++)
				for(int k=0;k<4;k++)
					next[((size_t)y*h+x)*4+k]=(unsigned char)((c[((size_t)(2*y)*s+2*x)*4+k]+c[((size_t)(2*y)*s+2*x+1)*4+k]+
						c[((size_t)(2*y+1)*s+2*x)*4+k]+c[((size_t)(2*y+1)*s+2*x+1)*4+k]+2)/4);
		levels.push_back(next);
		s=h;
	}
	sg_image_desc id;
	memset(&id,0,sizeof(id));
	id.width=id.height=n;
	id.num_mipmaps=(int)levels.size();
	id.pixel_format=SG_PIXELFORMAT_RGBA8;
	for(size_t m=0;m<levels.size();m++)
	{
		id.data.mip_levels[m].ptr=levels[m].data();
		id.data.mip_levels[m].size=levels[m].size();
	}
	id.label="ultra water normals";
	wnormalimg=sg_make_image(&id);
	wnormal=TexView(wnormalimg);
}

//a tiling 3D noise for the clouds: billowy (inverted cell distances) over a value
//noise, three octaves, in one channel
static void MakeNoise3D()
{
	const int n=64;
	std::vector<unsigned char> px((size_t)n*n*n);
	auto hash=[](int x,int y,int z,int p)
	{
		x=((x%p)+p)%p;
		y=((y%p)+p)%p;
		z=((z%p)+p)%p;
		unsigned v=(unsigned)x*73856093u^(unsigned)y*19349663u^(unsigned)z*83492791u;
		v^=v>>13;
		v*=0x5bd1e995u;
		v^=v>>15;
		return v;
	};
	for(int z=0;z<n;z++)
		for(int y=0;y<n;y++)
			for(int x=0;x<n;x++)
			{
				float sum=0,amp=1,norm=0;
				for(int o=0;o<3;o++)
				{
					int cells=4<<o;
					float fx=(float)x*cells/n,fy=(float)y*cells/n,fz=(float)z*cells/n;
					int cx=(int)fx,cy=(int)fy,cz=(int)fz;
					float best=9;
					for(int k=-1;k<=1;k++)
						for(int j=-1;j<=1;j++)
							for(int i=-1;i<=1;i++)
							{
								unsigned h=hash(cx+i,cy+j,cz+k,cells);
								float px_=cx+i+(h&255)/255.0f,py_=cy+j+((h>>8)&255)/255.0f,pz_=cz+k+((h>>16)&255)/255.0f;
								float d=(px_-fx)*(px_-fx)+(py_-fy)*(py_-fy)+(pz_-fz)*(pz_-fz);
								if(d<best) best=d;
							}
					sum+=amp*(1-Clamp(sqrtf(best),0,1));
					norm+=amp;
					amp*=0.5f;
				}
				px[((size_t)z*n+y)*n+x]=(unsigned char)(Clamp(sum/norm,0,1)*255);
			}
	sg_image_desc id;
	memset(&id,0,sizeof(id));
	id.type=SG_IMAGETYPE_3D;
	id.width=id.height=n;
	id.num_slices=n;
	id.pixel_format=SG_PIXELFORMAT_R8;
	id.data.mip_levels[0].ptr=px.data();
	id.data.mip_levels[0].size=px.size();
	id.label="ultra cloud noise";
	noise3dimg=sg_make_image(&id);
	noise3d=TexView(noise3dimg);
}

//the water's grid: rings around the camera, denser near it, out to where the land ends
static void MakeWaterGrid()
{
	const int rings=160,segs=256;
	std::vector<float> v;
	std::vector<unsigned int> idx;
	v.push_back(0);
	v.push_back(0);
	for(int r=1;r<=rings;r++)
	{
		float rad=1200.0f*powf((float)r/rings,2.4f);
		for(int s=0;s<segs;s++)
		{
			float a=6.2831853f*s/segs;
			v.push_back(cosf(a)*rad);
			v.push_back(sinf(a)*rad);
		}
	}
	for(int s=0;s<segs;s++)
	{
		idx.push_back(0);
		idx.push_back(1+(s+1)%segs);
		idx.push_back(1+s);
	}
	for(int r=1;r<rings;r++)
		for(int s=0;s<segs;s++)
		{
			unsigned a=1+(r-1)*segs+s,b=1+(r-1)*segs+(s+1)%segs;
			unsigned c=1+r*segs+s,e=1+r*segs+(s+1)%segs;
			idx.push_back(a);
			idx.push_back(b);
			idx.push_back(c);
			idx.push_back(c);
			idx.push_back(b);
			idx.push_back(e);
		}
	sg_buffer_desc bd;
	memset(&bd,0,sizeof(bd));
	bd.data.ptr=v.data();
	bd.data.size=v.size()*sizeof(float);
	bd.label="ultra water grid";
	watergrid=sg_make_buffer(&bd);
	memset(&bd,0,sizeof(bd));
	bd.usage.index_buffer=true;
	bd.data.ptr=idx.data();
	bd.data.size=idx.size()*sizeof(unsigned);
	bd.label="ultra water indices";
	wateridx=sg_make_buffer(&bd);
	waterindices=(int)idx.size();
}

//a blade (3 segments and a tip) and the instances of the two rings of grass: a
//jittered grid over a tile that repeats around the camera
static void MakeGrass()
{
	static const float blade[14]={-1,0, 1,0, -1,.33f, 1,.33f, -1,.66f, 1,.66f, 0,1};
	static const unsigned short ind[BLADEINDICES]={0,1,2, 2,1,3, 2,3,4, 4,3,5, 4,5,6};
	sg_buffer_desc bd;
	memset(&bd,0,sizeof(bd));
	bd.data.ptr=blade;
	bd.data.size=sizeof(blade);
	bd.label="ultra blade";
	bladevb=sg_make_buffer(&bd);
	memset(&bd,0,sizeof(bd));
	bd.usage.index_buffer=true;
	bd.data.ptr=ind;
	bd.data.size=sizeof(ind);
	bd.label="ultra blade indices";
	bladeib=sg_make_buffer(&bd);
	for(int ring=0;ring<2;ring++)
	{
		float tile=ring?GRASSFARTILE:GRASSTILE;
		float step=tile/GRASSPERSIDE;
		std::vector<float> inst;
		urandseed=ring?777:333;
		for(int y=0;y<GRASSPERSIDE;y++)
			for(int x=0;x<GRASSPERSIDE;x++)
			{
				inst.push_back((x+(float)URand()/32767.0f)*step);
				inst.push_back((y+(float)URand()/32767.0f)*step);
				inst.push_back((float)URand()/32767.0f*6.2831853f);
				inst.push_back((float)URand()/32767.0f);
			}
		memset(&bd,0,sizeof(bd));
		bd.data.ptr=inst.data();
		bd.data.size=inst.size()*sizeof(float);
		bd.label="ultra grass instances";
		(ring?instfar:instnear)=sg_make_buffer(&bd);
	}
}

static void LoadAssets()
{
	static int loaded;
	if(loaded) return;
	loaded=1;
	static const unsigned char gray[4]={128,128,128,255},flat[4]={128,128,255,255},orm[4]={255,200,128,255};
	int found[HDLAYERS];
	hdalbimg=MakeArray(1024,"",gray,found);
	hdnrmimg=MakeArray(512,"_n",flat,0);
	hdormimg=MakeArray(512,"_r",orm,0);
	hdalb=TexView(hdalbimg);
	hdnrm=TexView(hdnrmimg);
	hdorm=TexView(hdormimg);
	int n=0;
	for(int i=0;i<HDLAYERS;i++) n+=found[i];
	for(int i=0;i<10;i++) hdblocklayer[i]=found[HDBLOCK0+i]?HDBLOCK0+i:-1;
	char msg[64];
	snprintf(msg,sizeof(msg),"%d of %d HD textures (data/hd)",n,HDLAYERS);
	Log(msg);
	MakeWaterNormals();
	MakeNoise3D();
	MakeWaterGrid();
	MakeGrass();
}

//---- the land ----

static float LandAt(const float *land,int size,int x,int z)
{
	x&=size-1;
	z&=size-1;
	return land[(size_t)x*size+z];
}

//a periodic value noise over the land (repeats with it)
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

static void UpdateLand()
{
	if(!scene.land||scene.landsize<=0||(scene.landsize&(scene.landsize-1))) return;
	int size=scene.landsize;
	const float *land=scene.land;
	unsigned hash=2166136261u;
	const unsigned *bits=(const unsigned *)land;
	for(size_t i=0;i<(size_t)size*size;i++) hash=(hash^bits[i])*16777619u;
	hash^=(unsigned)(scene.waterheight*1000);
	if(landvalid&&hash==landhash) return;
	landhash=hash;
	landvalid=1;
	float hmax=-1e9f;
	for(size_t i=0;i<(size_t)size*size;i++) if(land[i]>hmax) hmax=land[i];
	float w=scene.waterheight;
	float snowline=w+(hmax-w)*0.78f;
	if(snowline<w+45) snowline=w+45;   //low lands have no snow
	std::vector<float> heights((size_t)size*size);
	std::vector<unsigned char> nrm((size_t)size*size*4),splat((size_t)size*size*4);
	for(int z=0;z<size;z++)
		for(int x=0;x<size;x++)
		{
			size_t o=(size_t)z*size+x;
			float h=LandAt(land,size,x,z);
			heights[o]=h;
			//the near rings draw every 2 units: the normal over 2
			float dx=(LandAt(land,size,x+2,z)-LandAt(land,size,x-2,z))/4;
			float dz=(LandAt(land,size,x,z+2)-LandAt(land,size,x,z-2))/4;
			V3 n=Norm(v3(-dx,1,-dz));
			nrm[o*4]=(unsigned char)(n.x*127.5f+127.5f);
			nrm[o*4+1]=(unsigned char)(n.y*127.5f+127.5f);
			nrm[o*4+2]=(unsigned char)(n.z*127.5f+127.5f);
			nrm[o*4+3]=255;
			float slope=1-n.y;
			float nz1=LandNoise(x,z,16,size,1),nz2=LandNoise(x,z,64,size,2),nz3=LandNoise(x,z,8,size,3);
			float sand=1-Smooth(w+0.6f,w+2.0f,h+(nz1-0.5f)*1.6f);
			float rock=Smooth(0.30f,0.48f,slope+(nz3-0.5f)*0.1f);
			float snow=Smooth(snowline-4,snowline+4,h+(nz2-0.5f)*10)*(1-Smooth(0.45f,0.6f,slope));
			float dirt=Smooth(0.66f,0.86f,nz2*0.6f+nz1*0.4f)*0.7f;
			//layered: snow over rock over sand over grass and dirt
			float rem=1;
			float ws=snow*rem;
			rem-=ws;
			float wr=rock*rem;
			rem-=wr;
			float wsa=sand*rem;
			rem-=wsa;
			float wg=rem*(1-dirt);
			splat[o*4]=(unsigned char)(Clamp(wg,0,1)*255+.5f);
			splat[o*4+1]=(unsigned char)(Clamp(wr,0,1)*255+.5f);
			splat[o*4+2]=(unsigned char)(Clamp(wsa,0,1)*255+.5f);
			splat[o*4+3]=(unsigned char)(Clamp(ws,0,1)*255+.5f);
		}
	//replaced: the frame that used the old ones is already committed
	if(heightimg.id)
	{
		sg_destroy_view(heightview);
		sg_destroy_view(landnrm);
		sg_destroy_view(landsplat);
		sg_destroy_image(heightimg);
		sg_destroy_image(landnrmimg);
		sg_destroy_image(landsplatimg);
	}
	sg_image_desc id;
	memset(&id,0,sizeof(id));
	id.width=id.height=size;
	id.pixel_format=SG_PIXELFORMAT_R32F;
	id.data.mip_levels[0].ptr=heights.data();
	id.data.mip_levels[0].size=heights.size()*sizeof(float);
	id.label="ultra heights";
	heightimg=sg_make_image(&id);
	heightview=TexView(heightimg);
	id.pixel_format=SG_PIXELFORMAT_RGBA8;
	id.data.mip_levels[0].ptr=nrm.data();
	id.data.mip_levels[0].size=nrm.size();
	id.label="ultra land normals";
	landnrmimg=sg_make_image(&id);
	landnrm=TexView(landnrmimg);
	id.data.mip_levels[0].ptr=splat.data();
	id.data.mip_levels[0].size=splat.size();
	id.label="ultra land splat";
	landsplatimg=sg_make_image(&id);
	landsplat=TexView(landsplatimg);
}

//---- the sky ----

//single scattering in a spherical atmosphere (Rayleigh and Mie, exponential
//densities), marched from the eye towards d; returns the light scattered to the eye
static void Scatter(V3 d,V3 sun,float mie,float out[3])
{
	const float R=6360e3f,top=6420e3f,hr=8e3f,hm=1.2e3f;
	const float br[3]={5.8e-6f,13.5e-6f,33.1e-6f};
	float bm=21e-6f*mie;
	V3 o=v3(0,R+300,0);
	//to the top of the atmosphere
	float b=Dot(o,d),c=Dot(o,o)-top*top;
	float len=-b+sqrtf(b*b-c);
	const int steps=16,lsteps=6;
	float seg=len/steps;
	float odr=0,odm=0,sr[3]={0,0,0},sm[3]={0,0,0};
	float mu=Dot(d,sun);
	float pr=3/(16*3.14159265f)*(1+mu*mu);
	float g=0.6f;
	float pm=3/(8*3.14159265f)*((1-g*g)*(1+mu*mu))/((2+g*g)*powf(1+g*g-2*g*mu,1.5f));
	for(int i=0;i<steps;i++)
	{
		V3 p=v3(o.x+d.x*seg*(i+.5f),o.y+d.y*seg*(i+.5f),o.z+d.z*seg*(i+.5f));
		float h=sqrtf(Dot(p,p))-R;
		float dr=expf(-h/hr)*seg,dm=expf(-h/hm)*seg;
		odr+=dr;
		odm+=dm;
		//towards the sun
		float lb=Dot(p,sun),lc=Dot(p,p)-top*top;
		float llen=-lb+sqrtf(lb*lb-lc);
		float lseg=llen/lsteps,lodr=0,lodm=0;
		int lit=1;
		for(int j=0;j<lsteps;j++)
		{
			V3 q=v3(p.x+sun.x*lseg*(j+.5f),p.y+sun.y*lseg*(j+.5f),p.z+sun.z*lseg*(j+.5f));
			float lh=sqrtf(Dot(q,q))-R;
			if(lh<0)
			{
				lit=0;
				break;
			}
			lodr+=expf(-lh/hr)*lseg;
			lodm+=expf(-lh/hm)*lseg;
		}
		if(!lit) continue;
		for(int k=0;k<3;k++)
		{
			float tau=br[k]*(odr+lodr)+bm*1.1f*(odm+lodm);
			float att=expf(-tau);
			sr[k]+=dr*att;
			sm[k]+=dm*att;
		}
	}
	for(int k=0;k<3;k++) out[k]=(sr[k]*br[k]*pr+sm[k]*bm*pm)*20;
}

//the sun's light at the ground: what the atmosphere lets through
static void SunTransmittance(V3 sun,float mie,float out[3])
{
	const float R=6360e3f,top=6420e3f,hr=8e3f,hm=1.2e3f;
	const float br[3]={5.8e-6f,13.5e-6f,33.1e-6f};
	V3 o=v3(0,R+300,0);
	V3 s=sun;
	if(s.y<0.02f) s=Norm(v3(s.x,0.02f,s.z));
	float b=Dot(o,s),c=Dot(o,o)-top*top;
	float len=-b+sqrtf(b*b-c);
	float odr=0,odm=0;
	const int steps=24;
	float seg=len/steps;
	for(int i=0;i<steps;i++)
	{
		V3 p=v3(o.x+s.x*seg*(i+.5f),o.y+s.y*seg*(i+.5f),o.z+s.z*seg*(i+.5f));
		float h=sqrtf(Dot(p,p))-R;
		odr+=expf(-h/hr)*seg;
		odm+=expf(-h/hm)*seg;
	}
	for(int k=0;k<3;k++) out[k]=expf(-(br[k]*odr+21e-6f*mie*1.1f*odm));
}

static float SrgbToLinear(float c)
{
	return c<=0.04045f?c/12.92f:powf((c+0.055f)/1.055f,2.4f);
}

#define LUTW 128
#define LUTH 64

static void UpdateSky()
{
	float key[8]={scene.sundir[0],scene.sundir[1],scene.sundir[2],scene.fogcolor[0],scene.fogcolor[1],scene.fogcolor[2],
		(float)scene.underwater,1};
	if(skylutimg.id&&!memcmp(key,skykey,sizeof(key))) return;
	memcpy(skykey,key,sizeof(key));
	V3 sun=Norm(v3(scene.sundir[0],scene.sundir[1],scene.sundir[2]));
	//the map's mood from its fog color: a blue one is a clear day; grey, sepia or
	//dark ones are overcast, hazy, stormy
	float fr=scene.fogcolor[0],fg=scene.fogcolor[1],fb=scene.fogcolor[2];
	float lum=0.299f*fr+0.587f*fg+0.114f*fb;
	float blue=(fb-fr)/(fb>0.05f?fb:0.05f);
	overcast=1-Clamp(blue/0.35f,0,1);
	darkness=Clamp(lum/0.62f,0.06f,1.15f);
	haze=0.3f+overcast*2.0f;
	float fl[3]={SrgbToLinear(fr),SrgbToLinear(fg),SrgbToLinear(fb)};
	std::vector<unsigned short> px((size_t)LUTW*LUTH*4);
	double sum[3]={0,0,0},hsum[3]={0,0,0};
	int nsum=0,nhsum=0;
	for(int y=0;y<LUTH;y++)
	{
		float v=(y+0.5f)/LUTH;
		float t=(v-0.5f)*2;
		float el=(t<0?-1:1)*t*t*1.5707963f;
		for(int x=0;x<LUTW;x++)
		{
			float az=((x+0.5f)/LUTW-0.5f)*6.2831853f;
			float e=el<0.0f?0.0f:el;
			V3 d=v3(cosf(e)*sinf(az),sinf(e),cosf(e)*cosf(az));
			float c[3];
			Scatter(d,sun,haze,c);
			//below the horizon: the haze over the land, darker downwards
			if(el<0) for(int k=0;k<3;k++) c[k]*=1-0.5f*Clamp(-el/0.6f,0,1);
			//overcast: towards the map's own fog color, flatter
			float flat=0.9f+0.35f*Clamp(el,0,1);
			for(int k=0;k<3;k++)
			{
				float o=fl[k]*flat*1.6f;
				c[k]=(c[k]*(1-overcast*0.85f)+o*overcast*0.85f)*darkness;
			}
			unsigned short *p=&px[((size_t)y*LUTW+x)*4];
			for(int k=0;k<3;k++) p[k]=Half(c[k]);
			p[3]=Half(1);
			if(el>0.9f)
			{
				for(int k=0;k<3;k++) sum[k]+=c[k];
				nsum++;
			}
			if(el>0.03f&&el<0.2f)
			{
				for(int k=0;k<3;k++) hsum[k]+=c[k];
				nhsum++;
			}
		}
	}
	float tr[3];
	SunTransmittance(sun,haze,tr);
	float above=Smooth(-0.05f,0.08f,sun.y);
	for(int k=0;k<3;k++)
	{
		suncolor[k]=tr[k]*6.0f*(1-0.8f*overcast)*darkness*above;
		skyzenith[k]=(float)(sum[k]/(nsum?nsum:1));
		skyhorizon[k]=(float)(hsum[k]/(nhsum?nhsum:1));
		//light coming back from the ground: its albedo under the sun and the sky
		skyground[k]=suncolor[k]*Clamp(sun.y,0,1)*0.05f+skyhorizon[k]*0.25f;
	}
	if(skylutimg.id)
	{
		sg_destroy_view(skylut);
		sg_destroy_image(skylutimg);
	}
	sg_image_desc id;
	memset(&id,0,sizeof(id));
	id.width=LUTW;
	id.height=LUTH;
	id.pixel_format=SG_PIXELFORMAT_RGBA16F;
	id.data.mip_levels[0].ptr=px.data();
	id.data.mip_levels[0].size=px.size()*sizeof(unsigned short);
	id.label="ultra sky";
	skylutimg=sg_make_image(&id);
	skylut=TexView(skylutimg);
}

//---- shadows ----

struct Cascades
{
	int count;
	Mat4 vp[4];           //world to the API's clip, for drawing
	Mat4 lookup[4];       //world to the atlas (uv, depth)
	float ends[4];
	float texel;
};

static Mat4 LookAt(V3 eye,V3 at,V3 up)
{
	V3 f=Norm(Sub(at,eye));
	V3 s=Norm(Cross(f,up));
	V3 u=Cross(s,f);
	Mat4 m=Mat4Identity();
	m.m[0]=s.x;
	m.m[4]=s.y;
	m.m[8]=s.z;
	m.m[1]=u.x;
	m.m[5]=u.y;
	m.m[9]=u.z;
	m.m[2]=-f.x;
	m.m[6]=-f.y;
	m.m[10]=-f.z;
	m.m[12]=-Dot(s,eye);
	m.m[13]=-Dot(u,eye);
	m.m[14]=Dot(f,eye);
	return m;
}

static Mat4 Ortho(float l,float r,float b,float t,float n,float f)
{
	Mat4 m=Mat4Identity();
	m.m[0]=2/(r-l);
	m.m[5]=2/(t-b);
	m.m[10]=-2/(f-n);
	m.m[12]=-(r+l)/(r-l);
	m.m[13]=-(t+b)/(t-b);
	m.m[14]=-(f+n)/(f-n);
	return m;
}

//fits each cascade's sphere around its slice of the view (stable under turning),
//its center snapped to the shadow map's texels (stable under moving)
static void FitCascades(Cascades &cs,const Mat4 &proj,int count,int tile,const RSokolFrame &f)
{
	static const float ends4[4]={9,28,75,190},ends2[2]={22,110};
	cs.count=count;
	float tanv=1/proj.m[5],tanh=1/proj.m[0];
	Mat4 invview;
	RSokolInverse(scene.view,&invview);
	V3 eye=v3(scene.eye[0],scene.eye[1],scene.eye[2]);
	V3 fwd=Norm(v3(-invview.m[8],-invview.m[9],-invview.m[10]));
	V3 sun=Norm(v3(scene.sundir[0],scene.sundir[1],scene.sundir[2]));
	V3 up=fabsf(sun.y)>0.99f?v3(0,0,1):v3(0,1,0);
	float nearz=0.15f;
	for(int i=0;i<count;i++)
	{
		float farz=count==4?ends4[i]:ends2[i];
		cs.ends[i]=farz;
		//the slice's bounding sphere: on the view axis, through its far corners
		float k=tanv*tanv+tanh*tanh;
		float cz=0.5f*(nearz+farz)*(1+k);
		if(cz>farz) cz=farz;
		float r=sqrtf((farz-cz)*(farz-cz)+farz*farz*k);
		V3 center=v3(eye.x+fwd.x*cz,eye.y+fwd.y*cz,eye.z+fwd.z*cz);
		//snap the center in the light's view to whole texels
		Mat4 lv=LookAt(v3(0,0,0),Scale(sun,-1),up);
		float c4[4]={center.x,center.y,center.z,1},lc[4];
		Xform(lv,c4,lc);
		float texel=2*r/tile;
		lc[0]=floorf(lc[0]/texel)*texel;
		lc[1]=floorf(lc[1]/texel)*texel;
		//casters behind the slice (hills, buildings) up to 300 units towards the sun
		Mat4 lp=Ortho(lc[0]-r,lc[0]+r,lc[1]-r,lc[1]+r,-lc[2]-r-300,-lc[2]+r);
		Mat4 vp=Mat4Mul(RSokolApiProjection(lp),lv);
		cs.vp[i]=vp;
		//clip to the atlas: x, y to the tile (y up as sokol's viewports), z to the depth texture's
		int tx=i%2,ty=i/2;
		Mat4 bias=Mat4Identity();
		bias.m[0]=0.25f;
		bias.m[12]=0.25f+0.5f*tx;
		float ys=0.25f,yo=0.25f+0.5f*ty;
		if(f.origintopleft)
		{
			ys=-ys;
			yo=1-yo;
		}
		bias.m[5]=ys;
		bias.m[13]=yo;
		if(!f.ztozero)
		{
			bias.m[10]=0.5f;
			bias.m[14]=0.5f;
		}
		cs.lookup[i]=Mat4Mul(bias,vp);
		if(i==0) cs.texel=1.0f/(tile*2);
	}
}

//---- drawing ----

static sg_pass Pass(const Target *color,int ncolors,const Target *depth,int clear,const char *label)
{
	sg_pass p;
	memset(&p,0,sizeof(p));
	for(int i=0;i<ncolors;i++)
	{
		p.attachments.colors[i]=color[i].att;
		p.action.colors[i].load_action=clear?SG_LOADACTION_CLEAR:SG_LOADACTION_LOAD;
		p.action.colors[i].store_action=SG_STOREACTION_STORE;
		p.action.colors[i].clear_value.r=p.action.colors[i].clear_value.g=p.action.colors[i].clear_value.b=0;
		p.action.colors[i].clear_value.a=0;
	}
	if(depth)
	{
		p.attachments.depth_stencil=depth->att;
		p.action.depth.load_action=clear?SG_LOADACTION_CLEAR:SG_LOADACTION_LOAD;
		p.action.depth.store_action=SG_STOREACTION_STORE;
		p.action.depth.clear_value=1;
	}
	p.label=label;
	return p;
}

static int SameMat(const Mat4 &a,const Mat4 &b)
{
	return !memcmp(&a,&b,sizeof(a));
}

static int IsLit(const RSokolCmd &c)
{
	return c.mark<0&&c.material!=RMAT_NONE&&KeyPrim(c.pipe)==0;
}

static scene_params_t sp;
static int spshadows;

//the draws of a frame, with what changed since the last one applied
struct Drawer
{
	const RSokolFrame *f;
	sg_pipeline pipe;
	int uniforms,material,texslot;
	uint32_t view;
	int vp[4],sc[4];
	int reflect;          //mirrored (the reflection pass)
	Mat4 mirror;          //eye to the mirrored eye
	int w,h;              //the target's size

	void Reset()
	{
		pipe.id=SG_INVALID_ID;
		uniforms=material=texslot=-1;
		view=0;
		for(int i=0;i<4;i++) vp[i]=sc[i]=-1;
	}

	void Viewport(const RSokolCmd &c)
	{
		int v[4]={c.viewport[0],c.viewport[1],c.viewport[2],c.viewport[3]};
		if(!v[2]||reflect)
		{
			v[0]=v[1]=0;
			v[2]=w;
			v[3]=h;
		}
		if(memcmp(v,vp,sizeof(v)))
		{
			sg_apply_viewport(v[0],v[1],v[2],v[3],false);
			memcpy(vp,v,sizeof(v));
		}
		const int *s=c.scissor;
		int full[4]={0,0,1<<15,1<<15};
		if(reflect) s=full;
		if(memcmp(s,sc,sizeof(sc)))
		{
			sg_apply_scissor_rect(s[0],s[1],s[2],s[3],false);
			memcpy(sc,s,sizeof(sc));
		}
	}

	void Lit(const RSokolCmd &c)
	{
		int key=reflect?KeyFlipCull(c.pipe):c.pipe;
		sg_pipeline p=LitPipe(key);
		int newpipe=p.id!=pipe.id;
		if(newpipe)
		{
			sg_apply_pipeline(p);
			pipe=p;
			uniforms=material=texslot=-1;
			view=0;
		}
		Viewport(c);
		if(newpipe||c.view.id!=view)
		{
			material=-1;      //untextured or not: the material's uniforms say
			sg_bindings b;
			memset(&b,0,sizeof(b));
			b.vertex_buffers[0]=f->vbuf;
			b.views[VIEW_tex]=c.view;
			b.views[VIEW_skylut]=skylut;
			b.views[VIEW_landnrm]=landnrm;
			b.views[VIEW_landsplat]=landsplat;
			b.views[VIEW_hdalb]=hdalb;
			b.views[VIEW_hdnrm]=hdnrm;
			b.views[VIEW_hdorm]=hdorm;
			b.views[VIEW_shadowmap]=shadowatlas.tex;
			b.samplers[SMP_smp]=f->samplers[c.sampler];
			b.samplers[SMP_lutsmp]=lutsampler;
			b.samplers[SMP_landsmp]=linrepeat;
			b.samplers[SMP_hdsmp]=hdsampler;
			b.samplers[SMP_shadowsmp]=shadowsampler;
			sg_apply_bindings(&b);
			view=c.view.id;
		}
		if(newpipe)
		{
			sg_range r={&sp,sizeof(sp)};
			sg_apply_uniforms(UB_scene_params,&r);
		}
		if(newpipe||c.uniforms!=uniforms)
		{
			const RSokolUniforms &u=f->uniforms[c.uniforms];
			lit_vs_params_t vs;
			memset(&vs,0,sizeof(vs));
			vs.proj=u.proj;
			vs.modelview=reflect?Mat4Mul(mirror,u.modelview):u.modelview;
			vs.model=u.model;
			Set4(vs.wind,scene.time,0.012f,c.material==RMAT_FOLIAGE?1.0f:0.0f,0);
			sg_range r={&vs,sizeof(vs)};
			sg_apply_uniforms(UB_lit_vs_params,&r);
			uniforms=c.uniforms;
			material=-1;
		}
		if(c.material!=material||c.texslot!=texslot)
		{
			lit_fs_params_t fs;
			memset(&fs,0,sizeof(fs));
			float kind=c.material==RMAT_TERRAIN?0.0f:c.material==RMAT_BLOCK?1.0f:2.0f;
			float layer=-1;
			if(c.material==RMAT_BLOCK&&c.texslot>=TEX_BLOCK0&&c.texslot<TEX_BLOCK0+32)
				layer=(float)hdblocklayer[c.texslot-TEX_BLOCK0];
			if(c.material!=RMAT_TERRAIN&&c.view.id==f->white.id) layer=-2;   //no texture
			Set4(fs.material,kind,c.material==RMAT_BLOCK?0.75f:0.55f,0,layer);
			Set4(fs.land,(float)scene.landsize,reflect?1.0f:0.0f,0,0);
			sg_range r={&fs,sizeof(fs)};
			sg_apply_uniforms(UB_lit_fs_params,&r);
			material=c.material;
			texslot=c.texslot;
		}
		sg_draw(c.first,c.count,1);
	}

	void Classic(const RSokolCmd &c)
	{
		sg_pipeline p=HdrPipe(c.pipe);
		int newpipe=p.id!=pipe.id;
		if(newpipe)
		{
			sg_apply_pipeline(p);
			pipe=p;
			uniforms=material=texslot=-1;
			view=0;
		}
		Viewport(c);
		if(newpipe||c.view.id!=view)
		{
			sg_bindings b;
			memset(&b,0,sizeof(b));
			b.vertex_buffers[0]=f->vbuf;
			b.views[VIEW_tex]=c.view;
			b.views[VIEW_skylut]=skylut;
			b.samplers[SMP_smp]=f->samplers[c.sampler];
			b.samplers[SMP_lutsmp]=lutsampler;
			sg_apply_bindings(&b);
			view=c.view.id;
		}
		if(newpipe)
		{
			sg_range r={&sp,sizeof(sp)};
			sg_apply_uniforms(UB_scene_params,&r);
		}
		if(newpipe||c.uniforms!=uniforms)
		{
			const RSokolUniforms &u=f->uniforms[c.uniforms];
			sg_range vs={&u.proj,sizeof(hdrdraw_vs_params_t)},fs={u.fogcolor,sizeof(hdrdraw_fs_params_t)};
			sg_apply_uniforms(UB_hdrdraw_vs_params,&vs);
			sg_apply_uniforms(UB_hdrdraw_fs_params,&fs);
			uniforms=c.uniforms;
		}
		sg_draw(c.first,c.count,1);
	}
};

static void DrawSky(const Mat4 &proj,const Mat4 &view)
{
	//clip (far plane) to a world direction: the inverse of proj times the view's rotation
	Mat4 rot=view;
	rot.m[12]=rot.m[13]=rot.m[14]=0;
	Mat4 vp=Mat4Mul(proj,rot),inv;
	if(!RSokolInverse(vp,&inv)) return;
	sg_apply_pipeline(skypipe);
	sg_bindings b;
	memset(&b,0,sizeof(b));
	b.views[VIEW_skylut]=skylut;
	b.samplers[SMP_lutsmp]=lutsampler;
	b.views[VIEW_cloudtex]=clouds.tex;
	b.samplers[SMP_cloudsmp]=linclamp;
	sg_apply_bindings(&b);
	sky_vs_params_t vs;
	vs.ndctodir=inv;
	sg_range r={&vs,sizeof(vs)};
	sg_apply_uniforms(UB_sky_vs_params,&r);
	sky_fs_params_t fs;
	Set4(fs.screen,1.0f/targetw,1.0f/targeth,rultraprefs.clouds?1.0f:0.0f,0);
	r.ptr=&fs;
	r.size=sizeof(fs);
	sg_apply_uniforms(UB_sky_fs_params,&r);
	sg_range s={&sp,sizeof(sp)};
	sg_apply_uniforms(UB_scene_params,&s);
	sg_draw(0,3,1);
}

static void CopyScene(const RSokolFrame &f,const Mat4 &proj)
{
	Target outs[2]={scenecopy,lindepth};
	sg_pass p=Pass(outs,2,0,0,"ultra copy");
	p.action.colors[0].load_action=p.action.colors[1].load_action=SG_LOADACTION_DONTCARE;
	sg_begin_pass(&p);
	sg_apply_pipeline(copypipe);
	sg_bindings b;
	memset(&b,0,sizeof(b));
	b.views[VIEW_srccolor]=hdr.tex;
	b.views[VIEW_depth]=hdrdepth.tex;
	b.samplers[SMP_pointsmp]=pointclamp;
	sg_apply_bindings(&b);
	copy_params_t cp;
	memset(&cp,0,sizeof(cp));
	Set4(cp.screen,1.0f/targetw,1.0f/targeth,f.origintopleft?1.0f:0.0f,f.ztozero?0.0f:1.0f);
	RSokolInverse(proj,&cp.invproj);
	sg_range r={&cp,sizeof(cp)};
	sg_apply_uniforms(UB_copy_params,&r);
	sg_draw(0,3,1);
	sg_end_pass();
}

static void DrawWater(const RSokolFrame &f,const RSokolUniforms &u,int planar)
{
	sg_apply_pipeline(waterpipe);
	sg_apply_viewport(0,0,targetw,targeth,false);
	sg_apply_scissor_rect(0,0,targetw,targeth,false);
	sg_bindings b;
	memset(&b,0,sizeof(b));
	b.vertex_buffers[0]=watergrid;
	b.index_buffer=wateridx;
	b.views[VIEW_scenecolor]=scenecopy.tex;
	b.views[VIEW_scenedepth]=lindepth.tex;
	b.views[VIEW_refl]=refl.tex;
	b.views[VIEW_skylut]=skylut;
	b.views[VIEW_wnormals]=wnormal;
	b.views[VIEW_hdalb]=hdalb;
	b.views[VIEW_shadowmap]=shadowatlas.tex;
	b.samplers[SMP_clampsmp]=linclamp;
	b.samplers[SMP_pointsmp]=pointclamp;
	b.samplers[SMP_lutsmp]=lutsampler;
	b.samplers[SMP_repsmp]=linrepeat;
	b.samplers[SMP_shadowsmp]=shadowsampler;
	sg_apply_bindings(&b);
	water_vs_params_t vs;
	memset(&vs,0,sizeof(vs));
	vs.viewproj=Mat4Mul(u.proj,u.modelview);
	Set4(vs.cam,scene.eye[0],scene.waterheight,scene.eye[2],scene.time);
	//waves: fade with distance (no aliasing), calmer under a clear sky
	Set4(vs.waves,0.9f+overcast*0.6f,40,140,0);
	sg_range r={&vs,sizeof(vs)};
	sg_apply_uniforms(UB_water_vs_params,&r);
	water_fs_params_t fs;
	memset(&fs,0,sizeof(fs));
	Set4(fs.screen,1.0f/targetw,1.0f/targeth,planar?1.0f:0.0f,0);
	Mat4 inv;
	RSokolInverse(scene.view,&inv);
	Set4(fs.view,-inv.m[8],-inv.m[9],-inv.m[10],0);
	r.ptr=&fs;
	r.size=sizeof(fs);
	sg_apply_uniforms(UB_water_fs_params,&r);
	r.ptr=&sp;
	r.size=sizeof(sp);
	sg_apply_uniforms(UB_scene_params,&r);
	sg_draw(0,waterindices,1);
}

static void DrawGrass(const RSokolFrame &f,const RSokolUniforms &u)
{
	(void)f;
	int q=rultraprefs.grass;
	if(q<=0||!heightimg.id) return;
	sg_apply_pipeline(grasspipe);
	sg_apply_viewport(0,0,targetw,targeth,false);
	sg_apply_scissor_rect(0,0,targetw,targeth,false);
	grass_vs_params_t vs;
	memset(&vs,0,sizeof(vs));
	vs.viewproj=Mat4Mul(u.proj,u.modelview);
	float t=scene.time;
	//the wind turns slowly, and blows harder under an overcast sky
	float wa=0.6f+0.3f*sinf(t*0.05f);
	float strength=0.35f+overcast*0.35f;
	grass_fs_params_t fs;
	memset(&fs,0,sizeof(fs));
	Set4(fs.tint,1,1,1,0);
	Mat4 inv;
	RSokolInverse(scene.view,&inv);
	Set4(fs.view,-inv.m[8],-inv.m[9],-inv.m[10],0);
	for(int ring=0;ring<2;ring++)
	{
		float outer=ring?(q>=2?42.0f:26.0f):(q>=2?13.0f:9.0f);
		float inner=ring?(q>=2?13.0f:9.0f):0;
		sg_bindings b;
		memset(&b,0,sizeof(b));
		b.vertex_buffers[0]=bladevb;
		b.vertex_buffers[1]=ring?instfar:instnear;
		b.index_buffer=bladeib;
		b.views[VIEW_heights]=heightview;
		b.views[VIEW_vsplat]=landsplat;
		b.views[VIEW_skylut]=skylut;
		b.views[VIEW_hdalb]=hdalb;
		b.views[VIEW_shadowmap]=shadowatlas.tex;
		b.samplers[SMP_hsmp]=pointclamp;
		b.samplers[SMP_vsmp]=linrepeat;
		b.samplers[SMP_lutsmp]=lutsampler;
		b.samplers[SMP_hdsmp]=hdsampler;
		b.samplers[SMP_shadowsmp]=shadowsampler;
		sg_apply_bindings(&b);
		Set4(vs.cam,scene.eye[0],scene.eye[1],scene.eye[2],ring?GRASSFARTILE:GRASSTILE);
		Set4(vs.params,t,outer,inner,ring?0.42f:0.27f);
		Set4(vs.wind,cosf(wa),sinf(wa),strength,(float)scene.landsize);
		Set4(vs.land,scene.waterheight,0,0,0);
		sg_range r={&vs,sizeof(vs)};
		sg_apply_uniforms(UB_grass_vs_params,&r);
		r.ptr=&fs;
		r.size=sizeof(fs);
		sg_apply_uniforms(UB_grass_fs_params,&r);
		r.ptr=&sp;
		r.size=sizeof(sp);
		sg_apply_uniforms(UB_scene_params,&r);
		sg_draw(0,BLADEINDICES,GRASSPERSIDE*GRASSPERSIDE);
	}
}

static void Fullpass(const Target &dst,sg_pipeline pipe,const char *label,int load)
{
	sg_pass p=Pass(&dst,1,0,0,label);
	p.action.colors[0].load_action=load?SG_LOADACTION_LOAD:SG_LOADACTION_DONTCARE;
	sg_begin_pass(&p);
	sg_apply_pipeline(pipe);
}

#include "ultra_veg.inl"

void RUltraSetScene(const RUltraScene &s)
{
	scene=s;
	//debug aid: HK_ULTRA_SUN=<height> lowers or raises the sun (0.1: sunset), Ultra only
	static const char *sunenv=getenv("HK_ULTRA_SUN");
	if(sunenv)
	{
		V3 d=Norm(v3(0,(float)atof(sunenv),1));
		scene.sundir[0]=d.x;
		scene.sundir[1]=d.y;
		scene.sundir[2]=d.z;
	}
}

void RUltraDrawScene(const RSokolFrame &f,const RSokolCmd *cmds,int n)
{
	if(!inited||failed) return;
	LoadAssets();
	EnsureTargets(f.width,f.height);
	UpdateLand();
	UpdateSky();
	VegPlace();
	int shadows=rultraprefs.shadows;
	int size=shadows>=2?4096:2048;
	if(shadows&&shadowsize!=size)
	{
		FreeTarget(shadowatlas);
		MakeTarget(shadowatlas,size,size,SG_PIXELFORMAT_DEPTH,1,"ultra shadow atlas");
		shadowsize=size;
	}
	if(!shadowatlas.image.id)
	{
		MakeTarget(shadowatlas,16,16,SG_PIXELFORMAT_DEPTH,1,"ultra shadow atlas");
		shadowsize=16;
	}

	//the scene's camera: the first lit draw's projection (the zoom included), else the begin mark's
	const RSokolUniforms &begin=f.uniforms[cmds[0].uniforms];
	Mat4 proj=begin.proj;
	int haveproj=0;
	const RSokolUniforms *wateru=0,*grassu=0;
	for(int i=1;i<n;i++)
	{
		if(!haveproj&&IsLit(cmds[i]))
		{
			proj=f.uniforms[cmds[i].uniforms].proj;
			haveproj=1;
		}
		if(cmds[i].mark==RMARK_WATER) wateru=&f.uniforms[cmds[i].uniforms];
		if(cmds[i].mark==RMARK_GRASS) grassu=&f.uniforms[cmds[i].uniforms];
	}
	if(wateru&&!haveproj) proj=wateru->proj;

	//the scene's uniforms
	V3 sun=Norm(v3(scene.sundir[0],scene.sundir[1],scene.sundir[2]));
	memset(&sp,0,sizeof(sp));
	Set4(sp.sundir,sun.x,sun.y,sun.z,Smooth(-0.03f,0.02f,sun.y));
	Set4(sp.suncolor,suncolor[0],suncolor[1],suncolor[2],scene.time);
	Set4(sp.skyzenith,skyzenith[0],skyzenith[1],skyzenith[2],haze*0.35f);
	Set4(sp.skyhorizon,skyhorizon[0],skyhorizon[1],skyhorizon[2],scene.waterheight);
	Set4(sp.groundcolor,skyground[0],skyground[1],skyground[2],scene.underwater?1.0f:0.0f);
	Set4(sp.eyepos,scene.eye[0],scene.eye[1],scene.eye[2],scene.fogend);
	//height fog: thicker on overcast maps; the classic draws' brightness in HDR
	Set4(sp.fogparams,0.0015f+overcast*0.003f,0.045f,1.25f*darkness,0);
	Set4(sp.watercolor,0.012f,0.05f,0.06f,1.0f);
	RSokolInverse(scene.view,&sp.invview);
	Cascades cs;
	memset(&cs,0,sizeof(cs));
	int ncasc=shadows>=2?4:shadows?2:0;
	if(ncasc)
	{
		FitCascades(cs,proj,ncasc,size/2,f);
		for(int i=0;i<4;i++)
		{
			sp.cascades[i]=cs.lookup[i];
			sp.cascadeends[i]=i<ncasc?cs.ends[i]:0;
		}
		Set4(sp.shadowparams,(float)ncasc,cs.texel,0.0008f,0.04f);
		sp.fogparams[3]=1;
	}

	//2. shadow maps: the land, blocks and models of the scene's camera (not the gun's)
	sg_pass p;
	if(ncasc)
	{
		p=Pass(0,0,&shadowatlas,1,"ultra shadows");
		sg_begin_pass(&p);
		int tile=size/2;
		for(int i=0;i<ncasc;i++)
		{
			sg_apply_pipeline(shadowpipe);
			sg_bindings b;
			memset(&b,0,sizeof(b));
			b.vertex_buffers[0]=f.vbuf;
			sg_apply_bindings(&b);
			sg_apply_viewport((i%2)*tile,(i/2)*tile,tile,tile,false);
			int last=-1;
			for(int k=1;k<n;k++)
			{
				const RSokolCmd &c=cmds[k];
				if(!IsLit(c)||KeyBlend(c.pipe)>2) continue;
				const RSokolUniforms &u=f.uniforms[c.uniforms];
				if(!SameMat(u.proj,proj)) continue;
				if(c.uniforms!=last)
				{
					shadow_vs_params_t vs;
					vs.mvp=Mat4Mul(cs.vp[i],u.model);
					sg_range r={&vs,sizeof(vs)};
					sg_apply_uniforms(UB_shadow_vs_params,&r);
					last=c.uniforms;
				}
				sg_draw(c.first,c.count,1);
			}
			VegDraw(cs.vp[i],1,0);
		}
		sg_end_pass();
	}

	Drawer d;
	memset(&d,0,sizeof(d));
	d.f=&f;

	//3. the water's reflection: the same draws, mirrored in the water's plane
	int planar=rultraprefs.water>0&&wateru&&!scene.underwater;
	if(planar)
	{
		Mat4 r=Mat4Identity();
		r.m[5]=-1;
		r.m[13]=2*scene.waterheight;
		Mat4 inv;
		RSokolInverse(scene.view,&inv);
		d.mirror=Mat4Mul(scene.view,Mat4Mul(r,inv));
		d.reflect=1;
		d.w=refl.w;
		d.h=refl.h;
		p=Pass(&refl,1,&refldepth,1,"ultra reflection");
		sg_begin_pass(&p);
		d.Reset();
		for(int k=1;k<n;k++)
		{
			const RSokolCmd &c=cmds[k];
			if(!IsLit(c)||!SameMat(f.uniforms[c.uniforms].proj,proj)) continue;
			d.Lit(c);
		}
		VegDraw(Mat4Mul(proj,Mat4Mul(scene.view,r)),0,1);
		sg_end_pass();
		d.reflect=0;
	}
	else
	{
		//nothing reflected: alpha 0, the water shows the sky
		p=Pass(&refl,1,0,1,"ultra reflection");
		sg_begin_pass(&p);
		sg_end_pass();
	}

	//the clouds, a quarter of the size, which the sky reads
	if(rultraprefs.clouds)
	{
		Mat4 rot=scene.view;
		rot.m[12]=rot.m[13]=rot.m[14]=0;
		Mat4 inv;
		if(RSokolInverse(Mat4Mul(proj,rot),&inv))
		{
			Fullpass(clouds,cloudspipe,"ultra clouds",0);
			sg_bindings b;
			memset(&b,0,sizeof(b));
			b.views[VIEW_noise3d]=noise3d;
			b.samplers[SMP_noisesmp]=noisesampler;
			sg_apply_bindings(&b);
			clouds_params_t cp;
			memset(&cp,0,sizeof(cp));
			cp.ndctodir=inv;
			Set4(cp.texel,1.0f/clouds.w,1.0f/clouds.h,f.origintopleft?1.0f:0.0f,0);
			Set4(cp.eye,scene.eye[0],scene.eye[1],scene.eye[2],scene.time);
			//the classic layer's height; more of them under an overcast sky
			float base=scene.waterheight+150;
			Set4(cp.layer,base,base+120,0.48f+overcast*0.32f,0.045f);
			Set4(cp.sun,sun.x,sun.y,sun.z,0);
			Set4(cp.suncol,suncolor[0],suncolor[1],suncolor[2],0);
			Set4(cp.ambient,skyzenith[0]*0.9f,skyzenith[1]*0.9f,skyzenith[2]*0.95f,0);
			float wa=0.6f+0.3f*sinf(scene.time*0.05f);
			Set4(cp.wind,cosf(wa)*7,sinf(wa)*7,0,0);
			sg_range r={&cp,sizeof(cp)};
			sg_apply_uniforms(UB_clouds_params,&r);
			sg_draw(0,3,1);
			sg_end_pass();
		}
	}

	//4. the scene
	d.w=targetw;
	d.h=targeth;
	p=Pass(&hdr,1,&hdrdepth,1,"ultra scene");
	sg_begin_pass(&p);
	sg_apply_viewport(0,0,targetw,targeth,false);
	DrawSky(proj,scene.view);
	d.Reset();
	int copied=0;
	for(int k=1;k<n;k++)
	{
		const RSokolCmd &c=cmds[k];
		if(c.mark==RMARK_WATER)
		{
			if(!copied)
			{
				sg_end_pass();
				CopyScene(f,f.uniforms[c.uniforms].proj);
				copied=1;
				p=Pass(&hdr,1,&hdrdepth,0,"ultra scene, water");
				sg_begin_pass(&p);
			}
			DrawWater(f,f.uniforms[c.uniforms],planar);
			d.Reset();
			continue;
		}
		if(c.mark==RMARK_GRASS)
		{
			DrawGrass(f,f.uniforms[c.uniforms]);
			VegDraw(Mat4Mul(f.uniforms[c.uniforms].proj,f.uniforms[c.uniforms].modelview),0,0);
			d.Reset();
			continue;
		}
		if(c.mark>=0) continue;
		if(IsLit(c)) d.Lit(c);
		else d.Classic(c);
	}
	sg_end_pass();
	if(!copied) CopyScene(f,proj);

	//5. post-processing: bloom
	for(int i=0;i<NUMBLOOM;i++)
	{
		const Target &src=i?bloom[i-1]:hdr;
		Fullpass(bloom[i],downpipe,"ultra bloom down",0);
		sg_bindings b;
		memset(&b,0,sizeof(b));
		b.views[VIEW_src]=src.tex;
		b.samplers[SMP_linsmp]=linclamp;
		sg_apply_bindings(&b);
		down_params_t dp;
		Set4(dp.texel,1.0f/bloom[i].w,1.0f/bloom[i].h,1.0f/src.w,1.0f/src.h);
		Set4(dp.opts,i==0?1.0f:0.0f,64,0,0);
		sg_range r={&dp,sizeof(dp)};
		sg_apply_uniforms(UB_down_params,&r);
		sg_draw(0,3,1);
		sg_end_pass();
	}
	for(int i=NUMBLOOM-1;i>0;i--)
	{
		Fullpass(bloom[i-1],uppipe,"ultra bloom up",1);
		sg_bindings b;
		memset(&b,0,sizeof(b));
		b.views[VIEW_src]=bloom[i].tex;
		b.samplers[SMP_linsmp]=linclamp;
		sg_apply_bindings(&b);
		up_params_t up;
		Set4(up.texel,1.0f/bloom[i-1].w,1.0f/bloom[i-1].h,1.0f/bloom[i].w,1.0f/bloom[i].h);
		Set4(up.opts,1,1,0,0);
		sg_range r={&up,sizeof(up)};
		sg_apply_uniforms(UB_up_params,&r);
		sg_draw(0,3,1);
		sg_end_pass();
	}

	//god rays: where the sun is on the screen (if in front of the camera)
	float sunuv[2]={0.5f,0.5f},raystrength=0;
	{
		float s4[4]={sun.x*1000+scene.eye[0],sun.y*1000+scene.eye[1],sun.z*1000+scene.eye[2],1},e4[4],c4[4];
		Xform(scene.view,s4,e4);
		Xform(proj,e4,c4);
		if(c4[3]>0&&sun.y>-0.05f&&!scene.underwater)
		{
			float x=c4[0]/c4[3],y=c4[1]/c4[3];
			sunuv[0]=0.5f+0.5f*x;
			sunuv[1]=f.origintopleft?0.5f-0.5f*y:0.5f+0.5f*y;
			float off=sqrtf(x*x+y*y);
			raystrength=(1-Smooth(1.0f,1.8f,off))*(0.35f+0.65f*(1-Smooth(0.1f,0.5f,sun.y)))*(1-overcast*0.6f);
		}
	}
	if(raystrength>0.001f)
	{
		Fullpass(rays,rayspipe,"ultra god rays",0);
		sg_bindings b;
		memset(&b,0,sizeof(b));
		b.views[VIEW_src]=hdr.tex;
		b.views[VIEW_depth]=lindepth.tex;
		b.samplers[SMP_linsmp]=linclamp;
		b.samplers[SMP_pointsmp]=pointclamp;
		sg_apply_bindings(&b);
		rays_params_t rp;
		Set4(rp.texel,1.0f/rays.w,1.0f/rays.h,0,0);
		Set4(rp.sun,sunuv[0],sunuv[1],raystrength*0.9f,(float)targetw/targeth);
		Set4(rp.color,1,0.92f,0.8f,0);
		sg_range r={&rp,sizeof(rp)};
		sg_apply_uniforms(UB_rays_params,&r);
		sg_draw(0,3,1);
		sg_end_pass();
	}

	//tonemapping
	Fullpass(ldr,compositepipe,"ultra composite",0);
	{
		sg_bindings b;
		memset(&b,0,sizeof(b));
		b.views[VIEW_hdr]=hdr.tex;
		b.views[VIEW_bloom]=bloom[0].tex;
		b.views[VIEW_rays]=rays.tex;
		b.samplers[SMP_linsmp]=linclamp;
		sg_apply_bindings(&b);
		composite_params_t cp;
		Set4(cp.texel,1.0f/targetw,1.0f/targeth,0,0);
		//darker maps keep their mood, but the eye adapts a little
		float exposure=0.8f/(0.55f+0.45f*darkness);
		Set4(cp.params,exposure,0.05f,raystrength>0.001f?1.0f:0.0f,scene.underwater?1.0f:0.0f);
		Set4(cp.sun,sunuv[0],sunuv[1],raystrength>0.001f?0.06f*raystrength:0,(float)targetw/targeth);
		Set4(cp.grade,1.0f,0.99f,0.97f,0.25f);
		sg_range r={&cp,sizeof(cp)};
		sg_apply_uniforms(UB_composite_params,&r);
		sg_draw(0,3,1);
	}
	sg_end_pass();

	//FXAA into the frame, its depth cleared for the HUD
	{
		sg_pass fp;
		memset(&fp,0,sizeof(fp));
		fp.attachments.colors[0]=f.colorview;
		fp.attachments.depth_stencil=f.depthview;
		fp.action.colors[0].load_action=SG_LOADACTION_DONTCARE;
		fp.action.colors[0].store_action=SG_STOREACTION_STORE;
		fp.action.depth.load_action=SG_LOADACTION_CLEAR;
		fp.action.depth.store_action=SG_STOREACTION_STORE;
		fp.action.depth.clear_value=1;
		fp.label="ultra fxaa";
		sg_begin_pass(&fp);
		sg_apply_pipeline(fxaapipe);
		sg_bindings b;
		memset(&b,0,sizeof(b));
		b.views[VIEW_src]=ldr.tex;
		b.samplers[SMP_linsmp]=linclamp;
		sg_apply_bindings(&b);
		fxaa_params_t fx;
		Set4(fx.texel,1.0f/targetw,1.0f/targeth,0,0);
		sg_range r={&fx,sizeof(fx)};
		sg_apply_uniforms(UB_fxaa_params,&r);
		sg_draw(0,3,1);
		sg_end_pass();
	}
	(void)grassu;
}

#endif
