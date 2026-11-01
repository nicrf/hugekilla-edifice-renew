//OpenGL backend of the texture registry (textures.h): the only place that
//creates, binds, fills or frees GL texture objects. M13.3 replaces this file.

#include "gl.h"
#include "rbackend.h"
#include "textures.h"

static GLuint texnames[TEX_NUMSLOTS];   //0: not created yet
static int boundslot=-2;                //slot bound to GL_TEXTURE_2D, -2: unknown

unsigned TexName(int slot)
{
	if(slot<0||slot>=TEX_NUMSLOTS) return 0;   //unknown slot: the default texture, never filled
	if(!texnames[slot])
		glGenTextures(1,&texnames[slot]);
	return texnames[slot];
}

void TexBackendBind(int slot)
{
	if(slot==boundslot) return;
	glBindTexture(GL_TEXTURE_2D,TexName(slot));
	boundslot=slot;
}

void TexUpload(int slot,int width,int height,int channels,const void *pixels,TexFilter filter)
{
	GLenum format=GL_RGBA;
	if(channels==1) format=GL_LUMINANCE;
	if(channels==2) format=GL_LUMINANCE_ALPHA;
	if(channels==3) format=GL_RGB;
	//what was batched with this texture draws first; and the upload leaves it bound,
	//as glBindTexture did, which the next draws inherit
	RFlush();
	TexBind(slot);
	TexBackendBind(slot);
	gluBuild2DMipmaps(GL_TEXTURE_2D,channels,width,height,format,GL_UNSIGNED_BYTE,pixels);
	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,
		filter==TEXF_MIPLINEAR?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR_MIPMAP_NEAREST);
}

void TexShutdown()
{
	RFlush();
	for(int i=0;i<TEX_NUMSLOTS;i++)
		if(texnames[i])
		{
			glDeleteTextures(1,&texnames[i]);
			texnames[i]=0;
		}
	boundslot=-2;
}
