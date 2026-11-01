//Fixed-function OpenGL backend of the renderer (renderer.h).

#include "gl.h"
#include "renderer.h"

static Mat4 projection=Mat4Identity(),modelview=Mat4Identity();
static int viewport[4];

void RSetProjection(const Mat4 &m)
{
	projection=m;
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(m.m);
	glMatrixMode(GL_MODELVIEW);
}

void RSetModelView(const Mat4 &m)
{
	modelview=m;
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(m.m);
}

const Mat4 &RProjection()
{
	return projection;
}

const Mat4 &RModelView()
{
	return modelview;
}

void RSet2D()
{
	RSetProjection(Mat4Identity());
	RSetModelView(Mat4Identity());
}

void RViewport(int x,int y,int w,int h)
{
	viewport[0]=x;
	viewport[1]=y;
	viewport[2]=w;
	viewport[3]=h;
	glViewport(x,y,w,h);
}

void RGetViewport(int vp[4])
{
	for(int i=0;i<4;i++) vp[i]=viewport[i];
}

void RReadPixels(int x,int y,int w,int h,int bgr,unsigned char *out)
{
	glPixelStorei(GL_PACK_ALIGNMENT,1);
	glReadBuffer(GL_BACK);
	glReadPixels(x,y,w,h,bgr?GL_BGR_EXT:GL_RGB,GL_UNSIGNED_BYTE,out);
}

void RReadDepth(int x,int y,float *depth)
{
	glReadPixels(x,y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,depth);
}
