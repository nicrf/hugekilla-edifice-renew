//Fixed-function OpenGL backend of the renderer (rbackend.h): with texture_gl.cpp,
//the only code that calls gl*. A batch is drawn from client-side vertex arrays,
//one glDrawArrays per run of primitives, after the GL state is brought to the
//batch's (only what changed is set).

#include "gl.h"
#include "rbackend.h"
#include <string.h>

static RState applied;        //the state GL is in
static int appliedvalid;      //0: unknown, set everything

static const GLenum capgl[]={GL_TEXTURE_2D,GL_BLEND,GL_DEPTH_TEST,GL_CULL_FACE,GL_FOG,GL_SCISSOR_TEST,GL_LINE_SMOOTH};
static const GLenum primgl[]={GL_POINTS,GL_LINES,GL_LINE_STRIP,GL_LINE_LOOP,GL_TRIANGLES,GL_TRIANGLE_STRIP,GL_TRIANGLE_FAN,GL_QUADS};

void RBackendInit()
{
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	//fixed for the whole game: linear fog (RFogRange), texture times vertex color
	//(GL_MODULATE, the default), the fastest perspective correction (set every
	//frame by the old Draw, ignored by today's drivers)
	glFogi(GL_FOG_MODE,GL_LINEAR);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT,GL_FASTEST);
	appliedvalid=0;
}

static void BlendGL(RBlend blend)
{
	switch(blend)
	{
	case RBLEND_OPAQUE: glBlendFunc(GL_ONE,GL_ZERO); break;
	case RBLEND_ALPHA: glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); break;
	case RBLEND_ADD: glBlendFunc(GL_SRC_ALPHA,GL_ONE); break;
	case RBLEND_ADDONE: glBlendFunc(GL_ONE,GL_ONE); break;
	case RBLEND_SCALE: glBlendFunc(GL_SRC_ALPHA,GL_ZERO); break;
	}
}

//brings GL to the state s
static void Apply(const RState &s)
{
	int all=!appliedvalid;
	for(int i=0;i<(int)(sizeof(capgl)/sizeof(capgl[0]));i++)
	{
		int bit=1<<i;
		if(all||((s.caps^applied.caps)&bit))
		{
			if(s.caps&bit) glEnable(capgl[i]);
			else glDisable(capgl[i]);
		}
	}
	if(all||s.blend!=applied.blend) BlendGL(s.blend);
	if(all||s.cullface!=applied.cullface) glCullFace(s.cullface==RFACE_FRONT?GL_FRONT:GL_BACK);
	if(all||s.depthwrite!=applied.depthwrite) glDepthMask(s.depthwrite?GL_TRUE:GL_FALSE);
	if(all||s.linewidth!=applied.linewidth) glLineWidth(s.linewidth);
	if(all||s.pointsize!=applied.pointsize) glPointSize(s.pointsize);
	if(all||s.fogstart!=applied.fogstart) glFogf(GL_FOG_START,s.fogstart);
	if(all||s.fogend!=applied.fogend) glFogf(GL_FOG_END,s.fogend);
	if(all||memcmp(s.fogcolor,applied.fogcolor,sizeof(s.fogcolor))) glFogfv(GL_FOG_COLOR,s.fogcolor);
	if(all||memcmp(s.clearcolor,applied.clearcolor,sizeof(s.clearcolor)))
		glClearColor(s.clearcolor[0],s.clearcolor[1],s.clearcolor[2],s.clearcolor[3]);
	TexBackendBind(s.texture);   //keeps its own record: uploads bind too
	if(all||memcmp(s.viewport,applied.viewport,sizeof(s.viewport)))
		glViewport(s.viewport[0],s.viewport[1],s.viewport[2],s.viewport[3]);
	if(all||memcmp(s.scissor,applied.scissor,sizeof(s.scissor)))
		glScissor(s.scissor[0],s.scissor[1],s.scissor[2],s.scissor[3]);
	if(all||memcmp(&s.projection,&applied.projection,sizeof(s.projection)))
	{
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(s.projection.m);
		glMatrixMode(GL_MODELVIEW);
	}
	if(all||memcmp(&s.modelview,&applied.modelview,sizeof(s.modelview)))
	{
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(s.modelview.m);
	}
	applied=s;
	appliedvalid=1;
}

void RBackendDraw(const RState &state,const RVertex *verts,const RCmd *cmds,int numcmds)
{
	Apply(state);
	glVertexPointer(3,GL_FLOAT,sizeof(RVertex),&verts->x);
	glTexCoordPointer(2,GL_FLOAT,sizeof(RVertex),&verts->u);
	glColorPointer(4,GL_FLOAT,sizeof(RVertex),&verts->r);
	for(int i=0;i<numcmds;i++)
		glDrawArrays(primgl[cmds[i].prim],cmds[i].first,cmds[i].count);
}

void RBackendClear(const RState &state,int color,int depth)
{
	Apply(state);   //scissor, clear color, depth mask
	GLbitfield mask=0;
	if(color) mask|=GL_COLOR_BUFFER_BIT;
	if(depth) mask|=GL_DEPTH_BUFFER_BIT;
	if(mask) glClear(mask);
}

void RBackendReadPixels(int x,int y,int w,int h,int bgr,unsigned char *out)
{
	glPixelStorei(GL_PACK_ALIGNMENT,1);
	glReadBuffer(GL_BACK);
	glReadPixels(x,y,w,h,bgr?GL_BGR_EXT:GL_RGB,GL_UNSIGNED_BYTE,out);
}

void RBackendReadDepth(int x,int y,float *depth)
{
	glReadPixels(x,y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,depth);
}

//no Ultra graphics with fixed-function GL: Classic only
int RBackendUltraSupported()
{
	return 0;
}

void RBackendUltraMark(int mark,const RState &state,const RUltraScene *scene)
{
	(void)mark;
	(void)state;
	(void)scene;
}
