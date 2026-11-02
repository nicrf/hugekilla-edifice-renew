//sokol_gfx backend of the renderer (rbackend.h), with HK_RENDER_SOKOL: Metal on macOS,
//D3D11 on Windows, GL 4.1 core on Linux (the device and window side is in
//sokol_native.cpp). The fixed-function state becomes pipelines (blend, depth, culling)
//and shader uniforms (matrices, fog), shaders/edifice.glsl.
//
//A frame is recorded first: each batch is converted to triangle or line lists (no
//quads, strips, fans or loops in the APIs; wide lines and points become quads, which
//only GL had) and gets a command with its pipeline, uniforms, texture, viewport and
//scissor. The recording is drawn into an offscreen image when the frame is presented,
//or when the game reads it back (the sun's depth, HK_SHOT, screenshots): sokol_gfx
//fills a streamed vertex buffer once per frame, and a read needs what came before it
//drawn. The window gets a copy of that image, so every read sees what GL's back
//buffer had.

#include "rbackend.h"
#include "rcontext.h"
#include "rsokol.h"
#include "textures.h"
#include "edifice.glsl.h"
#include <math.h>
#include <stddef.h>   //offsetof
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

//---- resources ----

//pipeline states: primitive, blending, depth, culling, color writes (clears)
enum
{
	PRIM_TRIS,
	PRIM_LINES,
	NUMPRIMS
};
#define NUMBLENDS 6       //off, then RBlend+1 (RBLEND_OPAQUE draws as off)
#define NUMCULLS 3        //none, back, front
#define NUMPIPES (NUMPRIMS*NUMBLENDS*2*2*NUMCULLS*2)

static int PipeKey(int prim,int blend,int depthtest,int depthwrite,int cull,int colorwrite)
{
	return ((((prim*NUMBLENDS+blend)*2+depthtest)*2+depthwrite)*NUMCULLS+cull)*2+colorwrite;
}

static sg_shader drawshader,presentshader;
static sg_pipeline pipes[NUMPIPES],presentpipe;
static sg_sampler samplers[2];    //TEXF_MIPNEAREST, TEXF_MIPLINEAR
static sg_sampler presentsampler;
static sg_image whiteimage;
static sg_view whiteview;         //texturing off, or a slot never filled: GL drew untextured
static sg_buffer vbuf;
static size_t vbufsize;
static int ztozero;               //the API's depth range is 0..1 (not GL's -1..1)

//the offscreen frame, at the drawable's size
static int targetw,targeth;
static sg_image colorimage,depthimage;
static sg_view colorview,depthview,colortexview;

static int errors;

static void Log(const char *tag,uint32_t level,uint32_t id,const char *msg,uint32_t line,const char *file,void *user)
{
	(void)tag;
	(void)user;
	static const char *levels[]={"panic","error","warning","info"};
	if(level<=1) errors++;
	fprintf(stderr,"sokol_gfx %s %u: %s (%s:%u)\n",levels[level<4?level:3],id,msg?msg:"",file?file:"",line);
	if(level==0) abort();
}

sg_pipeline_desc RSokolPipeDesc(int key)
{
	int colorwrite=key%2;
	int cull=key/2%NUMCULLS;
	int depthwrite=key/(2*NUMCULLS)%2;
	int depthtest=key/(4*NUMCULLS)%2;
	int blend=key/(8*NUMCULLS)%NUMBLENDS;
	int prim=key/(8*NUMCULLS*NUMBLENDS);
	sg_pipeline_desc d;
	memset(&d,0,sizeof(d));
	d.shader=drawshader;
	d.layout.buffers[0].stride=sizeof(RVertex);
	d.layout.attrs[ATTR_draw_position].format=SG_VERTEXFORMAT_FLOAT3;
	d.layout.attrs[ATTR_draw_position].offset=offsetof(RVertex,x);
	d.layout.attrs[ATTR_draw_texcoord0].format=SG_VERTEXFORMAT_FLOAT2;
	d.layout.attrs[ATTR_draw_texcoord0].offset=offsetof(RVertex,u);
	d.layout.attrs[ATTR_draw_color0].format=SG_VERTEXFORMAT_FLOAT4;
	d.layout.attrs[ATTR_draw_color0].offset=offsetof(RVertex,r);
	d.primitive_type=prim==PRIM_LINES?SG_PRIMITIVETYPE_LINES:SG_PRIMITIVETYPE_TRIANGLES;
	//GL tests less-than; with the test off it doesn't write depth either
	d.depth.pixel_format=SG_PIXELFORMAT_DEPTH;
	d.depth.compare=depthtest?SG_COMPAREFUNC_LESS:SG_COMPAREFUNC_ALWAYS;
	d.depth.write_enabled=depthwrite!=0;
	d.colors[0].pixel_format=SG_PIXELFORMAT_RGBA8;
	d.colors[0].write_mask=colorwrite?SG_COLORMASK_RGBA:SG_COLORMASK_NONE;
	if(blend>RBLEND_OPAQUE+1)
	{
		static const sg_blend_factor src[]={SG_BLENDFACTOR_ONE,SG_BLENDFACTOR_SRC_ALPHA,SG_BLENDFACTOR_SRC_ALPHA,SG_BLENDFACTOR_ONE,SG_BLENDFACTOR_SRC_ALPHA};
		static const sg_blend_factor dst[]={SG_BLENDFACTOR_ZERO,SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SG_BLENDFACTOR_ONE,SG_BLENDFACTOR_ONE,SG_BLENDFACTOR_ZERO};
		sg_blend_state &b=d.colors[0].blend;
		b.enabled=true;
		b.src_factor_rgb=b.src_factor_alpha=src[blend-1];
		b.dst_factor_rgb=b.dst_factor_alpha=dst[blend-1];
	}
	d.cull_mode=cull==1?SG_CULLMODE_BACK:cull==2?SG_CULLMODE_FRONT:SG_CULLMODE_NONE;
	d.face_winding=SG_FACEWINDING_CCW;   //GL's front faces
	d.sample_count=1;
	d.label="draw";
	return d;
}

static sg_pipeline Pipeline(int key)
{
	if(pipes[key].id) return pipes[key];
	sg_pipeline_desc d=RSokolPipeDesc(key);
	pipes[key]=sg_make_pipeline(&d);
	return pipes[key];
}

void RBackendInit()
{
	sg_desc desc;
	memset(&desc,0,sizeof(desc));
	desc.environment=RSokolEnvironment();
	desc.logger.func=Log;
	desc.image_pool_size=2048;      //textures (some 300), and those replaced in a frame
	desc.view_pool_size=2048;
	desc.pipeline_pool_size=NUMPIPES+16;
	//a frame applies two blocks per command, 256 bytes each at worst; split if more
	desc.uniform_buffer_size=16*1024*1024;
	sg_setup(&desc);
	sg_backend backend=sg_query_backend();
	ztozero=backend!=SG_BACKEND_GLCORE&&backend!=SG_BACKEND_GLES3;
	drawshader=sg_make_shader(draw_shader_desc(backend));
	presentshader=sg_make_shader(present_shader_desc(backend));

	sg_pipeline_desc pd;
	memset(&pd,0,sizeof(pd));
	pd.shader=presentshader;
	pd.depth.pixel_format=SG_PIXELFORMAT_NONE;
	pd.label="present";
	presentpipe=sg_make_pipeline(&pd);   //the swapchain's formats (sg_environment)

	//GL_LINEAR magnification; minification within the nearest level, or trilinear
	for(int i=0;i<2;i++)
	{
		sg_sampler_desc sd;
		memset(&sd,0,sizeof(sd));
		sd.min_filter=SG_FILTER_LINEAR;
		sd.mag_filter=SG_FILTER_LINEAR;
		sd.mipmap_filter=i==TEXF_MIPLINEAR?SG_FILTER_LINEAR:SG_FILTER_NEAREST;
		sd.wrap_u=sd.wrap_v=SG_WRAP_REPEAT;
		samplers[i]=sg_make_sampler(&sd);
	}
	sg_sampler_desc sd;
	memset(&sd,0,sizeof(sd));
	sd.min_filter=sd.mag_filter=SG_FILTER_NEAREST;
	sd.wrap_u=sd.wrap_v=SG_WRAP_CLAMP_TO_EDGE;
	presentsampler=sg_make_sampler(&sd);

	static const unsigned char white[4]={255,255,255,255};
	sg_image_desc id;
	memset(&id,0,sizeof(id));
	id.width=id.height=1;
	id.pixel_format=SG_PIXELFORMAT_RGBA8;
	id.data.mip_levels[0].ptr=white;
	id.data.mip_levels[0].size=sizeof(white);
	whiteimage=sg_make_image(&id);
	sg_view_desc vd;
	memset(&vd,0,sizeof(vd));
	vd.texture.image=whiteimage;
	whiteview=sg_make_view(&vd);
	if(RUltraBackendSupported()) RUltraInit();
}

static void DestroyTarget()
{
	sg_destroy_view(colortexview);
	sg_destroy_view(colorview);
	sg_destroy_view(depthview);
	sg_destroy_image(colorimage);
	sg_destroy_image(depthimage);
	colortexview.id=colorview.id=depthview.id=SG_INVALID_ID;
	colorimage.id=depthimage.id=SG_INVALID_ID;
	targetw=targeth=0;
}

//the offscreen frame follows the drawable (a resize loses the frame, as GL's did)
static void EnsureTarget()
{
	int w,h;
	RDrawableSize(&w,&h);
	if(w<1) w=1;
	if(h<1) h=1;
	if(w==targetw&&h==targeth) return;
	DestroyTarget();
	sg_image_desc id;
	memset(&id,0,sizeof(id));
	id.width=w;
	id.height=h;
	id.sample_count=1;
	id.usage.color_attachment=true;
	id.pixel_format=SG_PIXELFORMAT_RGBA8;
	id.label="frame";
	colorimage=sg_make_image(&id);
	id.usage.color_attachment=false;
	id.usage.depth_stencil_attachment=true;
	id.pixel_format=SG_PIXELFORMAT_DEPTH;
	id.label="depth";
	depthimage=sg_make_image(&id);
	sg_view_desc vd;
	memset(&vd,0,sizeof(vd));
	vd.color_attachment.image=colorimage;
	colorview=sg_make_view(&vd);
	memset(&vd,0,sizeof(vd));
	vd.depth_stencil_attachment.image=depthimage;
	depthview=sg_make_view(&vd);
	memset(&vd,0,sizeof(vd));
	vd.texture.image=colorimage;
	colortexview=sg_make_view(&vd);
	targetw=w;
	targeth=h;
}

void RSokolShutdown()
{
	if(RUltraBackendSupported()) RUltraShutdown();
	DestroyTarget();
	if(errors) fprintf(stderr,"sokol_gfx: %d error(s)\n",errors);
	sg_shutdown();
}

//---- recording ----

typedef RSokolUniforms Uniforms;
typedef RSokolCmd Cmd;
static_assert(sizeof(draw_vs_params_t)==2*sizeof(Mat4),"draw_vs_params layout");
static_assert(sizeof(draw_fs_params_t)==8*sizeof(float),"draw_fs_params layout");

static std::vector<RVertex> frameverts;
static std::vector<Cmd> framecmds;
static std::vector<Uniforms> frameuniforms;

//no scissor test: a rectangle over any frame, which sokol_gfx clips to it
static const int noscissor[4]={0,0,1<<15,1<<15};

static int AddUniforms(const Uniforms &u)
{
	if(!frameuniforms.empty()&&!memcmp(&frameuniforms.back(),&u,sizeof(u)))
		return (int)frameuniforms.size()-1;
	frameuniforms.push_back(u);
	return (int)frameuniforms.size()-1;
}

//the vertices appended since first, drawn as one command (or the previous one, grown)
//the Ultra scene being recorded (between its BEGIN and END marks), and its camera
static int inscene;
static Mat4 sceneinvview;
//the material and texture slot of the draws being recorded
static int curmaterial,curtexslot=-1;

static void AddCmd(int pipe,int first,int uniforms,sg_view view,int sampler,const int viewport[4],const int scissor[4])
{
	int count=(int)frameverts.size()-first;
	if(count<=0) return;
	if(!framecmds.empty())
	{
		Cmd &last=framecmds.back();
		if(last.pipe==pipe&&last.first+last.count==first&&last.uniforms==uniforms&&last.view.id==view.id&&
			last.sampler==sampler&&!memcmp(last.viewport,viewport,sizeof(last.viewport))&&
			!memcmp(last.scissor,scissor,sizeof(last.scissor))&&last.material==curmaterial&&
			last.texslot==curtexslot&&last.mark<0)
		{
			last.count+=count;
			return;
		}
	}
	Cmd c;
	c.pipe=pipe;
	c.first=first;
	c.count=count;
	c.uniforms=uniforms;
	c.view=view;
	c.sampler=sampler;
	memcpy(c.viewport,viewport,sizeof(c.viewport));
	memcpy(c.scissor,scissor,sizeof(c.scissor));
	c.material=curmaterial;
	c.texslot=curtexslot;
	c.mark=-1;
	framecmds.push_back(c);
}

//GL's projection, with clip z taken from -w..w to 0..w where the API wants it
static Mat4 ApiProjection(const Mat4 &p)
{
	if(!ztozero) return p;
	Mat4 r=p;
	for(int c=0;c<4;c++) r.m[c*4+2]=0.5f*(p.m[c*4+2]+p.m[c*4+3]);
	return r;
}

Mat4 RSokolApiProjection(const Mat4 &gl)
{
	return ApiProjection(gl);
}

static void SetFog(Uniforms &u,const RState &s)
{
	memcpy(u.fogcolor,s.fogcolor,sizeof(u.fogcolor));
	u.fog[0]=1;
	u.fog[1]=0;
	u.fog[2]=u.fog[3]=0;
	if((s.caps&RCAP_FOG)&&s.fogend!=s.fogstart)
	{
		float inv=1/(s.fogend-s.fogstart);
		u.fog[0]=s.fogend*inv;
		u.fog[1]=inv;
	}
}

static void Transform(const Mat4 &m,const float in[4],float out[4])
{
	for(int i=0;i<4;i++)
		out[i]=m.m[i]*in[0]+m.m[4+i]*in[1]+m.m[8+i]*in[2]+m.m[12+i]*in[3];
}

//general 4x4 inverse (cofactors); 0 if singular
int RSokolInverse(const Mat4 &a,Mat4 *out)
{
	const float *m=a.m;
	float inv[16];
	inv[0]=m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
	inv[4]=-m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
	inv[8]=m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
	inv[12]=-m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
	inv[1]=-m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
	inv[5]=m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
	inv[9]=-m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
	inv[13]=m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
	inv[2]=m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
	inv[6]=-m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
	inv[10]=m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
	inv[14]=-m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
	inv[3]=-m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
	inv[7]=m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
	inv[11]=-m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
	inv[15]=m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
	float det=m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
	if(det==0) return 0;
	for(int i=0;i<16;i++) out->m[i]=inv[i]/det;
	return 1;
}

//Wide lines and points, which only GL draws, as quads facing the screen. They are
//built in eye space: a vertex moved by (dx, dy) in clip space moves by
//P^-1 (dx, dy, 0, 0) in eye space (no w term with GL's projections), so the quads
//draw with the batch's projection, an identity modelview and the right fog.
struct Expander
{
	Mat4 modelview,proj,inv;
	float pxw,pxh;        //clip units per pixel at w=1: 2/viewport size
	int ok;

	void Init(const RState &s)
	{
		modelview=s.modelview;
		proj=s.projection;
		ok=RSokolInverse(proj,&inv)&&s.viewport[2]>0&&s.viewport[3]>0;
		pxw=ok?2.f/s.viewport[2]:0;
		pxh=ok?2.f/s.viewport[3]:0;
	}
	//eye-space offset of a clip-space move (dx, dy)
	void Offset(float dx,float dy,float out[3]) const
	{
		for(int i=0;i<3;i++) out[i]=inv.m[i]*dx+inv.m[4+i]*dy;
	}
	static void Emit(const RVertex &attrs,const float eye[3],const float off[3],float sign)
	{
		RVertex v=attrs;
		v.x=eye[0]+sign*off[0];
		v.y=eye[1]+sign*off[1];
		v.z=eye[2]+sign*off[2];
		frameverts.push_back(v);
	}
	void Points(const RVertex *v,int n,float size) const
	{
		if(!ok) return;
		for(int i=0;i<n;i++)
		{
			float p[4]={v[i].x,v[i].y,v[i].z,1},eye[4],clip[4];
			Transform(modelview,p,eye);
			Transform(proj,eye,clip);
			float w=clip[3];
			//GL drops a point whose center is outside the view volume
			if(w<=0||fabsf(clip[0])>w||fabsf(clip[1])>w||fabsf(clip[2])>w) continue;
			float a[3],b[3];
			Offset(size*0.5f*pxw*w,size*0.5f*pxh*w,a);    //to a corner
			Offset(size*0.5f*pxw*w,-size*0.5f*pxh*w,b);   //to the next one
			Emit(v[i],eye,a,-1);
			Emit(v[i],eye,b,1);
			Emit(v[i],eye,a,1);
			Emit(v[i],eye,a,-1);
			Emit(v[i],eye,a,1);
			Emit(v[i],eye,b,-1);
		}
	}
	//a line list
	void Lines(const RVertex *v,int n,float width) const
	{
		if(!ok) return;
		for(int i=0;i+1<n;i+=2)
		{
			RVertex va=v[i],vb=v[i+1];
			float pa[4]={va.x,va.y,va.z,1},pb[4]={vb.x,vb.y,vb.z,1},ea[4],eb[4],ca[4],cb[4];
			Transform(modelview,pa,ea);
			Transform(modelview,pb,eb);
			Transform(proj,ea,ca);
			Transform(proj,eb,cb);
			//clipped at the near plane first: behind the eye, screen directions mean nothing
			float da=ca[2]+ca[3],db=cb[2]+cb[3];
			if(da<0&&db<0) continue;
			if(da<0||db<0)
			{
				float t=da/(da-db);
				RVertex m=va;
				const float *fa=&va.x,*fb=&vb.x;
				float *fm=&m.x;
				for(int k=0;k<9;k++) fm[k]=fa[k]+(fb[k]-fa[k])*t;
				float em[4],cm[4];
				for(int k=0;k<4;k++)
				{
					em[k]=ea[k]+(eb[k]-ea[k])*t;
					cm[k]=ca[k]+(cb[k]-ca[k])*t;
				}
				if(da<0)
				{
					va=m;
					memcpy(ea,em,sizeof(ea));
					memcpy(ca,cm,sizeof(ca));
				}
				else
				{
					vb=m;
					memcpy(eb,em,sizeof(eb));
					memcpy(cb,cm,sizeof(cb));
				}
			}
			if(ca[3]<=0||cb[3]<=0) continue;
			//direction in pixels, and the half width across it
			float dx=(cb[0]/cb[3]-ca[0]/ca[3])/pxw,dy=(cb[1]/cb[3]-ca[1]/ca[3])/pxh;
			float len=sqrtf(dx*dx+dy*dy);
			if(len<1e-6f)
			{
				dx=1;
				dy=0;
				len=1;
			}
			float nx=-dy/len*width*0.5f*pxw,ny=dx/len*width*0.5f*pxh;
			float oa[3],ob[3];
			Offset(nx*ca[3],ny*ca[3],oa);
			Offset(nx*cb[3],ny*cb[3],ob);
			Emit(va,ea,oa,1);
			Emit(va,ea,oa,-1);
			Emit(vb,eb,ob,-1);
			Emit(va,ea,oa,1);
			Emit(vb,eb,ob,-1);
			Emit(vb,eb,ob,1);
		}
	}
};

static std::vector<RVertex> linebuf;

//a run of lines as a line list in linebuf
static void ToLineList(RPrim prim,const RVertex *v,int n)
{
	linebuf.clear();
	if(prim==RP_LINES)
	{
		linebuf.insert(linebuf.end(),v,v+n);
		return;
	}
	for(int i=0;i+1<n;i++)
	{
		linebuf.push_back(v[i]);
		linebuf.push_back(v[i+1]);
	}
	if(prim==RP_LINELOOP&&n>2)
	{
		linebuf.push_back(v[n-1]);
		linebuf.push_back(v[0]);
	}
}

//a run of triangles, strips, fans or quads as a triangle list, same winding
static void PushTriangles(RPrim prim,const RVertex *v,int n)
{
	switch(prim)
	{
	case RP_TRIANGLES:
		frameverts.insert(frameverts.end(),v,v+n);
		break;
	case RP_TRISTRIP:
		for(int i=0;i+2<n;i++)
		{
			//GL turns every other triangle around to keep them all facing the same way
			frameverts.push_back(v[i+(i&1)]);
			frameverts.push_back(v[i+1-(i&1)]);
			frameverts.push_back(v[i+2]);
		}
		break;
	case RP_TRIFAN:
		for(int i=1;i+1<n;i++)
		{
			frameverts.push_back(v[0]);
			frameverts.push_back(v[i]);
			frameverts.push_back(v[i+1]);
		}
		break;
	case RP_QUADS:
		for(int i=0;i+3<n;i+=4)
		{
			const int split[6]={0,1,2,0,2,3};
			for(int k=0;k<6;k++) frameverts.push_back(v[i+split[k]]);
		}
		break;
	default:
		break;
	}
}

void RBackendDraw(const RState &s,const RVertex *verts,const RCmd *cmds,int numcmds)
{
	Uniforms u;
	memset(&u,0,sizeof(u));
	u.proj=ApiProjection(s.projection);
	u.modelview=s.modelview;
	SetFog(u,s);
	if(inscene) u.model=Mat4Mul(sceneinvview,s.modelview);
	int uniforms=AddUniforms(u);
	curmaterial=inscene?s.material:0;
	curtexslot=inscene&&(s.caps&RCAP_TEXTURE)?s.texture:-1;
	int expanded=-1;          //the uniforms of the wide lines and points, made when needed
	Expander ex;
	ex.ok=-1;

	sg_view view=whiteview;
	int sampler=0;
	if(s.caps&RCAP_TEXTURE)
	{
		int filter=0;
		sg_view tex=TexSokolView(s.texture,&filter);
		if(tex.id)
		{
			view=tex;
			sampler=filter;
		}
	}
	int blend=(s.caps&RCAP_BLEND)&&s.blend!=RBLEND_OPAQUE?s.blend+1:0;
	int depthtest=(s.caps&RCAP_DEPTHTEST)!=0;
	int depthwrite=depthtest&&s.depthwrite;
	int cull=(s.caps&RCAP_CULL)?(s.cullface==RFACE_FRONT?2:1):0;
	const int *scissor=(s.caps&RCAP_SCISSOR)?s.scissor:noscissor;

	for(int i=0;i<numcmds;i++)
	{
		RPrim prim=cmds[i].prim;
		const RVertex *v=verts+cmds[i].first;
		int n=cmds[i].count;
		int first=(int)frameverts.size();
		if(prim==RP_POINTS||prim==RP_LINES||prim==RP_LINESTRIP||prim==RP_LINELOOP)
		{
			float size=prim==RP_POINTS?s.pointsize:s.linewidth;
			if(prim!=RP_POINTS&&size<=1)
			{
				//one pixel: the APIs' own lines
				ToLineList(prim,v,n);
				frameverts.insert(frameverts.end(),linebuf.begin(),linebuf.end());
				AddCmd(PipeKey(PRIM_LINES,blend,depthtest,depthwrite,0,1),first,uniforms,view,sampler,s.viewport,scissor);
				continue;
			}
			if(ex.ok<0) ex.Init(s);
			if(expanded<0)
			{
				Uniforms eu=u;
				eu.modelview=Mat4Identity();
				if(inscene) eu.model=sceneinvview;
				expanded=AddUniforms(eu);
			}
			if(prim==RP_POINTS)
				ex.Points(v,n,size);
			else
			{
				ToLineList(prim,v,n);
				ex.Lines(linebuf.data(),(int)linebuf.size(),size);
			}
			//GL doesn't cull lines and points
			AddCmd(PipeKey(PRIM_TRIS,blend,depthtest,depthwrite,0,1),first,expanded,view,sampler,s.viewport,scissor);
			continue;
		}
		PushTriangles(prim,v,n);
		AddCmd(PipeKey(PRIM_TRIS,blend,depthtest,depthwrite,cull,1),first,uniforms,view,sampler,s.viewport,scissor);
	}
}

//glClear as a quad over the frame at the far plane: color and depth, inside the
//scissor box when the test is on, depth only when depth writes are
void RBackendClear(const RState &s,int color,int depth)
{
	depth=depth&&s.depthwrite;
	if(!color&&!depth) return;
	Uniforms u;
	memset(&u,0,sizeof(u));
	u.proj=ApiProjection(Mat4Identity());
	u.modelview=Mat4Identity();
	u.fog[0]=1;               //no fog
	int uniforms=AddUniforms(u);
	curmaterial=0;
	curtexslot=-1;
	RVertex c;
	memset(&c,0,sizeof(c));
	c.r=s.clearcolor[0];
	c.g=s.clearcolor[1];
	c.b=s.clearcolor[2];
	c.a=s.clearcolor[3];
	c.z=1;
	static const float corners[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
	int first=(int)frameverts.size();
	for(int i=0;i<6;i++)
	{
		c.x=corners[i][0];
		c.y=corners[i][1];
		frameverts.push_back(c);
	}
	static const int wholeframe[4]={0,0,0,0};
	AddCmd(PipeKey(PRIM_TRIS,0,0,depth,0,color),first,uniforms,whiteview,0,wholeframe,
		(s.caps&RCAP_SCISSOR)?s.scissor:noscissor);
}

//---- drawing ----

//draws what was recorded into the offscreen frame (one pass)
static void Submit()
{
	EnsureTarget();
	if(framecmds.empty())
	{
		frameverts.clear();
		frameuniforms.clear();
		return;
	}
	size_t bytes=frameverts.size()*sizeof(RVertex);
	if(bytes>vbufsize)
	{
		if(vbuf.id) sg_destroy_buffer(vbuf);
		vbufsize=vbufsize?vbufsize:4*1024*1024;
		while(vbufsize<bytes) vbufsize*=2;
		sg_buffer_desc bd;
		memset(&bd,0,sizeof(bd));
		bd.size=vbufsize;
		bd.usage.vertex_buffer=true;
		bd.usage.write_transient=true;
		bd.label="frame vertices";
		vbuf=sg_make_buffer(&bd);
	}
	sg_write_buffer_desc wd;
	memset(&wd,0,sizeof(wd));
	wd.src.data.ptr=frameverts.data();
	wd.src.data.size=bytes;
	wd.dst.buffer=vbuf;
	sg_write_buffer_transient(&wd);

	sg_pass pass;
	memset(&pass,0,sizeof(pass));
	//what the frame already has stays: the game clears it itself (RBackendClear)
	pass.action.colors[0].load_action=SG_LOADACTION_LOAD;
	pass.action.colors[0].store_action=SG_STOREACTION_STORE;
	pass.action.depth.load_action=SG_LOADACTION_LOAD;
	pass.action.depth.store_action=SG_STOREACTION_STORE;
	pass.attachments.colors[0]=colorview;
	pass.attachments.depth_stencil=depthview;
	pass.label="frame";
	int passopen=0;
	int pipe=-1,uniforms=-1,sampler=-1;
	uint32_t view=0;
	int viewport[4]={-1,-1,-1,-1},scissor[4]={-1,-1,-1,-1};
	for(size_t i=0;i<framecmds.size();i++)
	{
		const Cmd &c=framecmds[i];
		if(c.mark==RMARK_BEGIN)
		{
			//the Ultra scene, up to its END mark: its own passes, which end in the frame
			if(passopen) sg_end_pass();
			passopen=0;
			size_t end=i+1;
			while(end<framecmds.size()&&framecmds[end].mark!=RMARK_END) end++;
			RSokolFrame f;
			memset(&f,0,sizeof(f));
			f.vbuf=vbuf;
			f.uniforms=frameuniforms.data();
			f.samplers=samplers;
			f.white=whiteview;
			f.colorimage=colorimage;
			f.depthimage=depthimage;
			f.colorview=colorview;
			f.depthview=depthview;
			f.width=targetw;
			f.height=targeth;
			f.ztozero=ztozero;
			f.origintopleft=RSokolOriginTopLeft();
			RUltraDrawScene(f,&framecmds[i],(int)(end-i));
			i=end;
			continue;
		}
		if(c.mark>=0) continue;
		if(!passopen)
		{
			sg_begin_pass(&pass);
			passopen=1;
			pipe=uniforms=sampler=-1;
			view=0;
			for(int k=0;k<4;k++) viewport[k]=scissor[k]=-1;
		}
		if(c.pipe!=pipe)
		{
			sg_apply_pipeline(Pipeline(c.pipe));
			pipe=c.pipe;
			uniforms=sampler=-1;   //sokol_gfx wants them again after a pipeline
			view=0;
		}
		int vp[4]={c.viewport[0],c.viewport[1],c.viewport[2],c.viewport[3]};
		if(!vp[2])
		{
			vp[2]=targetw;
			vp[3]=targeth;
		}
		if(memcmp(vp,viewport,sizeof(vp)))
		{
			sg_apply_viewport(vp[0],vp[1],vp[2],vp[3],false);
			memcpy(viewport,vp,sizeof(vp));
		}
		if(memcmp(c.scissor,scissor,sizeof(scissor)))
		{
			sg_apply_scissor_rect(c.scissor[0],c.scissor[1],c.scissor[2],c.scissor[3],false);
			memcpy(scissor,c.scissor,sizeof(scissor));
		}
		if(c.view.id!=view||c.sampler!=sampler)
		{
			sg_bindings b;
			memset(&b,0,sizeof(b));
			b.vertex_buffers[0]=vbuf;
			b.views[VIEW_tex]=c.view;
			b.samplers[SMP_smp]=samplers[c.sampler];
			sg_apply_bindings(&b);
			view=c.view.id;
			sampler=c.sampler;
		}
		if(c.uniforms!=uniforms)
		{
			const Uniforms &u=frameuniforms[c.uniforms];
			sg_range vs={&u.proj,sizeof(draw_vs_params_t)},fs={u.fogcolor,sizeof(draw_fs_params_t)};
			sg_apply_uniforms(UB_draw_vs_params,&vs);
			sg_apply_uniforms(UB_draw_fs_params,&fs);
			uniforms=c.uniforms;
		}
		sg_draw(c.first,c.count,1);
	}
	if(passopen) sg_end_pass();
	frameverts.clear();
	framecmds.clear();
	frameuniforms.clear();
}

void RSokolEndFrame()
{
	Submit();
	sg_pass pass;
	memset(&pass,0,sizeof(pass));
	pass.action.colors[0].load_action=SG_LOADACTION_DONTCARE;   //all of it is copied
	pass.swapchain=RSokolSwapchain();
	pass.label="present";
	sg_begin_pass(&pass);
	sg_apply_pipeline(presentpipe);
	sg_bindings b;
	memset(&b,0,sizeof(b));
	b.views[VIEW_frame]=colortexview;
	b.samplers[SMP_framesmp]=presentsampler;
	sg_apply_bindings(&b);
	present_vs_params_t p;
	memset(&p,0,sizeof(p));
	p.flip[0]=RSokolOriginTopLeft()?1.f:0.f;
	p.flip[1]=RSokolOriginTopLeft()?-1.f:1.f;
	sg_range r={&p,sizeof(p)};
	sg_apply_uniforms(UB_present_vs_params,&r);
	sg_draw(0,3,1);
	sg_end_pass();
	sg_commit();
	RSokolPresentNative();
	TexSokolEndFrame();
}

void RBackendReadPixels(int x,int y,int w,int h,int bgr,unsigned char *out)
{
	Submit();
	sg_commit();   //the reads wait for the GPU on what was committed
	if(x<0||y<0||w<=0||h<=0||x+w>targetw||y+h>targeth) return;
	std::vector<unsigned char> rgba((size_t)w*h*4);
	RSokolReadColor(colorimage,x,y,w,h,rgba.data());
	for(size_t i=0;i<(size_t)w*h;i++)
	{
		out[i*3]=rgba[i*4+(bgr?2:0)];
		out[i*3+1]=rgba[i*4+1];
		out[i*3+2]=rgba[i*4+(bgr?0:2)];
	}
}

void RBackendReadDepth(int x,int y,float *depth)
{
	if(x<0||y<0||x>=targetw||y>=targeth) return;   //outside: GL left it
	if(inscene) return;       //Ultra: the scene isn't drawn yet (the sun's glare is the post-processing's)
	Submit();
	sg_commit();
	RSokolReadDepth(depthimage,x,y,depth);
}

//---- Ultra marks ----

int RBackendUltraSupported()
{
	return RUltraBackendSupported();
}

void RBackendUltraMark(int mark,const RState &s,const RUltraScene *scene)
{
	if(mark==RMARK_BEGIN)
	{
		if(!scene||!RSokolInverse(scene->view,&sceneinvview)) return;
		RUltraSetScene(*scene);
		inscene=1;
	}
	else if(!inscene) return;
	if(mark==RMARK_END) inscene=0;
	//the matrices at that point (the water and the grass draw with the scene's camera)
	Uniforms u;
	memset(&u,0,sizeof(u));
	u.proj=ApiProjection(s.projection);
	u.modelview=s.modelview;
	SetFog(u,s);
	int uniforms=AddUniforms(u);
	Cmd c;
	memset(&c,0,sizeof(c));
	c.first=(int)frameverts.size();
	c.uniforms=uniforms;
	memcpy(c.viewport,s.viewport,sizeof(c.viewport));
	memcpy(c.scissor,noscissor,sizeof(c.scissor));
	c.texslot=-1;
	c.mark=mark;
	framecmds.push_back(c);
}
