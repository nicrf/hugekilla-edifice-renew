//The renderer's state and batch (renderer.h), the same for every backend: a state
//change draws what was batched under the old state, then the backend gets each
//batch with its whole state (rbackend.h).

#include "rbackend.h"
#include "textures.h"
#include <stdlib.h>
#include <string.h>
#include <vector>

RBatch rbatch;

static RState state;
static std::vector<RCmd> cmds;
static int inprim;            //between RBegin and REnd
static RPrim curprim;
static int primfirst;         //first vertex of the open primitive

struct SavedAttrib
{
	int mask;
	RState state;
	RVertex cur;
};
static std::vector<SavedAttrib> attribstack;

//OpenGL's initial state; the viewport comes from the platform layer
static void InitState()
{
	memset(&state,0,sizeof(state));
	state.blend=RBLEND_OPAQUE;
	state.cullface=RFACE_BACK;
	state.depthwrite=1;
	state.linewidth=1;
	state.pointsize=1;
	state.fogend=1;
	state.texture=-1;
	state.projection=Mat4Identity();
	state.modelview=Mat4Identity();
	memset(&rbatch.cur,0,sizeof(rbatch.cur));
	rbatch.cur.r=rbatch.cur.g=rbatch.cur.b=rbatch.cur.a=1;
}

void RInit()
{
	InitState();
	RBackendInit();
}

void RGrowBatch()
{
	rbatch.capacity=rbatch.capacity?rbatch.capacity*2:65536;
	RVertex *grown=(RVertex *)realloc(rbatch.verts,(size_t)rbatch.capacity*sizeof(RVertex));
	if(!grown) abort();
	rbatch.verts=grown;
}

void RFlush()
{
	if(!cmds.empty()) RBackendDraw(state,rbatch.verts,cmds.data(),(int)cmds.size());
	cmds.clear();
	if(inprim)
	{
		//a state changed inside RBegin/REnd (GL ignored that): keep the open primitive
		int open=rbatch.count-primfirst;
		memmove(rbatch.verts,rbatch.verts+primfirst,(size_t)open*sizeof(RVertex));
		rbatch.count=open;
		primfirst=0;
	}
	else
		rbatch.count=0;
}

//---- geometry ----

void RBegin(RPrim prim)
{
	if(inprim) REnd();
	if(!rbatch.verts) RGrowBatch();
	inprim=1;
	curprim=prim;
	primfirst=rbatch.count;
}

void REnd()
{
	if(!inprim) return;
	inprim=0;
	int first=primfirst,count=rbatch.count-first;
	int unit=0,least=1;
	switch(curprim)
	{
	case RP_POINTS: unit=1; break;
	case RP_LINES: unit=2; break;
	case RP_TRIANGLES: unit=3; break;
	case RP_QUADS: unit=4; break;
	case RP_LINESTRIP: case RP_LINELOOP: least=2; break;
	case RP_TRISTRIP: case RP_TRIFAN: least=3; break;
	}
	if(unit) count-=count%unit;
	if(count<least) count=0;
	rbatch.count=first+count;
	if(!count) return;
	//lists continue the previous run of the same type: one draw for all of them
	if(unit&&!cmds.empty()&&cmds.back().prim==curprim&&cmds.back().first+cmds.back().count==first)
	{
		cmds.back().count+=count;
		return;
	}
	RCmd cmd={curprim,first,count};
	cmds.push_back(cmd);
}

static void PushVerts(RPrim prim,const RVertex *v,int n)
{
	RBegin(prim);
	for(int i=0;i<n;i++)
	{
		if(rbatch.count==rbatch.capacity) RGrowBatch();
		rbatch.verts[rbatch.count++]=v[i];
	}
	REnd();
}

void RPushPoint(const RVertex &v)
{
	PushVerts(RP_POINTS,&v,1);
}

void RPushLine(const RVertex v[2])
{
	PushVerts(RP_LINES,v,2);
}

void RPushTri(const RVertex v[3])
{
	PushVerts(RP_TRIANGLES,v,3);
}

void RPushQuad(const RVertex v[4])
{
	PushVerts(RP_QUADS,v,4);
}

void RDrawMesh(const Mat4 &model,const RVertex *verts,const RMeshPart *parts,int numparts)
{
	Mat4 modelview=state.modelview;
	RSetModelView(Mat4Mul(modelview,model));
	for(int i=0;i<numparts;i++)
	{
		if(parts[i].texture!=RMESH_KEEPTEX) TexBind(parts[i].texture);
		PushVerts(RP_TRIANGLES,verts+parts[i].first,parts[i].count);
	}
	RSetModelView(modelview);
	//the vertices were given one by one, so GL's current color and texture
	//coordinate were the last vertex's
	for(int i=numparts-1;i>=0;i--)
		if(parts[i].count)
		{
			const RVertex &last=verts[parts[i].first+parts[i].count-1];
			RColor4f(last.r,last.g,last.b,last.a);
			RTexCoord2f(last.u,last.v);
			break;
		}
}

//---- state ----

#define SETSTATE(field,value) \
	if(state.field!=(value)) \
	{ \
		RFlush(); \
		state.field=(value); \
	}

void REnable(int caps)
{
	SETSTATE(caps,state.caps|caps);
}

void RDisable(int caps)
{
	SETSTATE(caps,state.caps&~caps);
}

void RBlendFunc(RBlend blend)
{
	SETSTATE(blend,blend);
}

void RCullFace(RFace face)
{
	SETSTATE(cullface,face);
}

void RDepthMask(int write)
{
	SETSTATE(depthwrite,write!=0);
}

void RLineWidth(float pixels)
{
	SETSTATE(linewidth,pixels);
}

void RPointSize(float pixels)
{
	SETSTATE(pointsize,pixels);
}

void RFogRange(float start,float end)
{
	SETSTATE(fogstart,start);
	SETSTATE(fogend,end);
}

void RFogColor(const float rgba[4])
{
	if(memcmp(state.fogcolor,rgba,sizeof(state.fogcolor)))
	{
		RFlush();
		memcpy(state.fogcolor,rgba,sizeof(state.fogcolor));
	}
}

void TexBind(int slot)
{
	SETSTATE(texture,slot);
}

void RSetProjection(const Mat4 &m)
{
	if(memcmp(&state.projection,&m,sizeof(m)))
	{
		RFlush();
		state.projection=m;
	}
}

void RSetModelView(const Mat4 &m)
{
	if(memcmp(&state.modelview,&m,sizeof(m)))
	{
		RFlush();
		state.modelview=m;
	}
}

const Mat4 &RProjection()
{
	return state.projection;
}

const Mat4 &RModelView()
{
	return state.modelview;
}

void RSet2D()
{
	RSetProjection(Mat4Identity());
	RSetModelView(Mat4Identity());
}

void RViewport(int x,int y,int w,int h)
{
	int vp[4]={x,y,w,h};
	if(memcmp(state.viewport,vp,sizeof(vp)))
	{
		RFlush();
		memcpy(state.viewport,vp,sizeof(vp));
	}
}

void RGetViewport(int vp[4])
{
	memcpy(vp,state.viewport,sizeof(state.viewport));
}

void RScissor(int x,int y,int w,int h)
{
	int box[4]={x,y,w,h};
	if(memcmp(state.scissor,box,sizeof(box)))
	{
		RFlush();
		memcpy(state.scissor,box,sizeof(box));
	}
}

void RPushAttrib(int mask)
{
	SavedAttrib saved={mask,state,rbatch.cur};
	attribstack.push_back(saved);
}

void RPopAttrib()
{
	if(attribstack.empty()) return;   //GL: stack underflow, nothing restored
	SavedAttrib saved=attribstack.back();
	attribstack.pop_back();
	RState restored=state;
	if(saved.mask&RATTR_ENABLE) restored.caps=saved.state.caps;
	if(saved.mask&RATTR_COLOR)
	{
		restored.caps=(restored.caps&~RCAP_BLEND)|(saved.state.caps&RCAP_BLEND);
		restored.blend=saved.state.blend;
		memcpy(restored.clearcolor,saved.state.clearcolor,sizeof(restored.clearcolor));
	}
	if(saved.mask&RATTR_FOG)
	{
		restored.caps=(restored.caps&~RCAP_FOG)|(saved.state.caps&RCAP_FOG);
		restored.fogstart=saved.state.fogstart;
		restored.fogend=saved.state.fogend;
		memcpy(restored.fogcolor,saved.state.fogcolor,sizeof(restored.fogcolor));
	}
	if(saved.mask&RATTR_POLYGON)
	{
		restored.caps=(restored.caps&~RCAP_CULL)|(saved.state.caps&RCAP_CULL);
		restored.cullface=saved.state.cullface;
	}
	if(saved.mask&RATTR_TEXTURE) restored.texture=saved.state.texture;
	if(saved.mask&RATTR_CURRENT)
	{
		RColor4f(saved.cur.r,saved.cur.g,saved.cur.b,saved.cur.a);
		RTexCoord2f(saved.cur.u,saved.cur.v);
	}
	if(memcmp(&restored,&state,sizeof(state)))
	{
		RFlush();
		state=restored;
	}
}

//---- frame ----

void RClearColor(float r,float g,float b,float a)
{
	float c[4]={r,g,b,a};
	if(memcmp(state.clearcolor,c,sizeof(c)))
	{
		RFlush();   //not needed to draw, but keeps the batch's state whole
		memcpy(state.clearcolor,c,sizeof(c));
	}
}

void RClear(int color,int depth)
{
	RFlush();
	RBackendClear(state,color,depth);
}

void RReadPixels(int x,int y,int w,int h,int bgr,unsigned char *out)
{
	RFlush();
	RBackendReadPixels(x,y,w,h,bgr,out);
}

void RReadDepth(int x,int y,float *depth)
{
	RFlush();
	RBackendReadDepth(x,y,depth);
}
