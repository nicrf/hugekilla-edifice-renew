//OpenGL backend of the texture registry (textures.h): the only place that
//creates, fills or frees GL texture objects. M13 replaces this file.

#include "StdAfx.h"
#include "textures.h"

static GLuint texnames[TEX_NUMSLOTS];   //0: not created yet

unsigned TexName(int slot)
{
	if(slot<0||slot>=TEX_NUMSLOTS) return 0;   //unknown slot: the default texture, never filled
	if(!texnames[slot])
		glGenTextures(1,&texnames[slot]);
	return texnames[slot];
}

void TexBind(int slot)
{
	glBindTexture(GL_TEXTURE_2D,TexName(slot));
}

void TexUpload(int slot,int width,int height,int channels,const void *pixels,TexFilter filter)
{
	GLenum format=GL_RGBA;
	if(channels==1) format=GL_LUMINANCE;
	if(channels==2) format=GL_LUMINANCE_ALPHA;
	if(channels==3) format=GL_RGB;
	TexBind(slot);
	gluBuild2DMipmaps(GL_TEXTURE_2D,channels,width,height,format,GL_UNSIGNED_BYTE,pixels);
	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,
		filter==TEXF_MIPLINEAR?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR_MIPMAP_NEAREST);
}

void TexShutdown()
{
	for(int i=0;i<TEX_NUMSLOTS;i++)
		if(texnames[i])
		{
			glDeleteTextures(1,&texnames[i]);
			texnames[i]=0;
		}
}
