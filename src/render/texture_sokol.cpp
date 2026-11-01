//sokol_gfx backend of the texture registry (textures.h), the counterpart of
//texture_gl.cpp: one immutable RGBA8 image per upload, with the mipmaps that
//gluBuild2DMipmaps made, computed the same way on the CPU (a power-of-two size,
//then 2x2 box averages down to 1x1), so that the textures look the same.

#include "rbackend.h"
#include "rsokol.h"
#include "textures.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

struct SlotTexture
{
	sg_image image;
	sg_view view;
	int filter;
};
static SlotTexture slots[TEX_NUMSLOTS];
//replaced during a frame: the frame's recorded draws still use them until it is drawn
static std::vector<sg_image> deadimages;
static std::vector<sg_view> deadviews;

unsigned TexName(int slot)
{
	if(slot<0||slot>=TEX_NUMSLOTS) return 0;
	return slots[slot].image.id;
}

void TexBackendBind(int slot)
{
	(void)slot;   //each recorded draw names its texture (TexSokolView)
}

sg_view TexSokolView(int slot,int *filter)
{
	sg_view none={SG_INVALID_ID};
	if(slot<0||slot>=TEX_NUMSLOTS) return none;
	*filter=slots[slot].filter;
	return slots[slot].view;
}

//gluBuild2DMipmaps' size: the nearest power of two, down unless the two top bits are set
//(GLU's nearestPower: 126 -> 128, 640 -> 512, 768 -> 1024)
static int NearestPower(int v)
{
	int p=1;
	if(v<=0) return 1;
	for(;;)
	{
		if(v==1) return p;
		if(v==3) return p*4;
		v>>=1;
		p*=2;
	}
}

//gluScaleImage (GLU's scale_internal): each output pixel averages the box it covers
//in the input, by area, the box being one input pixel wide at least; the input wraps
static void ScaleImage(int c,int win,int hin,const unsigned char *in,int wout,int hout,unsigned char *out)
{
	float convx=(float)win/wout,convy=(float)hin/hout;
	for(int i=0;i<hout;i++)
	{
		float y=convy*(i+0.5f),lowy,highy;
		if(hin>hout)
		{
			highy=y+convy/2;
			lowy=y-convy/2;
		}
		else
		{
			highy=y+0.5f;
			lowy=y-0.5f;
		}
		for(int j=0;j<wout;j++)
		{
			float x=convx*(j+0.5f),lowx,highx;
			if(win>wout)
			{
				highx=x+convx/2;
				lowx=x-convx/2;
			}
			else
			{
				highx=x+0.5f;
				lowx=x-0.5f;
			}
			float totals[4]={0,0,0,0},area=0;
			float yy=lowy;
			int yint=(int)floorf(yy);
			while(yy<highy)
			{
				int yindex=(yint+hin)%hin;
				float ypercent=highy<yint+1?highy-yy:yint+1-yy;
				float xx=lowx;
				int xint=(int)floorf(xx);
				while(xx<highx)
				{
					int xindex=(xint+win)%win;
					float xpercent=highx<xint+1?highx-xx:xint+1-xx;
					float percent=xpercent*ypercent;
					area+=percent;
					const unsigned char *p=in+((size_t)yindex*win+xindex)*c;
					for(int k=0;k<c;k++) totals[k]+=p[k]*percent;
					xint++;
					xx=(float)xint;
				}
				yint++;
				yy=(float)yint;
			}
			unsigned char *o=out+((size_t)i*wout+j)*c;
			for(int k=0;k<c;k++) o[k]=(unsigned char)((totals[k]+0.5f)/area);
		}
	}
}

//the next mip level, as GLU's halveImage (2x2, rounded) or halve1Dimage once a side is 1
static void HalveImage(int c,int w,int h,const unsigned char *in,int nw,int nh,unsigned char *out)
{
	for(int i=0;i<nh;i++)
		for(int j=0;j<nw;j++)
			for(int k=0;k<c;k++)
			{
				int sum;
				if(w>1&&h>1)
				{
					const unsigned char *t=in+((size_t)(2*i)*w+2*j)*c+k;
					sum=(t[0]+t[c]+t[(size_t)w*c]+t[(size_t)w*c+c]+2)/4;
				}
				else if(w>1)
				{
					const unsigned char *t=in+(size_t)(2*j)*c+k;
					sum=(t[0]+t[c]+1)/2;
				}
				else
				{
					const unsigned char *t=in+(size_t)(2*i)*c+k;
					sum=(t[0]+t[c]+1)/2;
				}
				out[((size_t)i*nw+j)*c+k]=(unsigned char)sum;
			}
}

//GL's formats to RGBA: luminance to gray, no alpha to opaque
static void ToRGBA(int c,int n,const unsigned char *in,unsigned char *out)
{
	for(int i=0;i<n;i++,in+=c,out+=4)
	{
		switch(c)
		{
		case 1: out[0]=out[1]=out[2]=in[0]; out[3]=255; break;
		case 2: out[0]=out[1]=out[2]=in[0]; out[3]=in[1]; break;
		case 3: out[0]=in[0]; out[1]=in[1]; out[2]=in[2]; out[3]=255; break;
		default: memcpy(out,in,4); break;
		}
	}
}

static void Release(int slot)
{
	if(slots[slot].view.id) deadviews.push_back(slots[slot].view);
	if(slots[slot].image.id) deadimages.push_back(slots[slot].image);
	slots[slot].view.id=SG_INVALID_ID;
	slots[slot].image.id=SG_INVALID_ID;
}

void TexUpload(int slot,int width,int height,int channels,const void *pixels,TexFilter filter)
{
	//what was batched with the old texture is recorded with it; and GL bound what it
	//filled: the next draws inherit the texture
	RFlush();
	TexBind(slot);
	if(slot<0||slot>=TEX_NUMSLOTS||width<=0||height<=0||channels<1||channels>4||!pixels) return;
	Release(slot);
	int c=channels;
	int w=NearestPower(width),h=NearestPower(height);
	int maxsize=sg_query_limits().max_image_size_2d;
	while(maxsize>0&&(w>maxsize||h>maxsize))
	{
		if(w>1) w/=2;
		if(h>1) h/=2;
	}
	std::vector<unsigned char> level((size_t)w*h*c);
	if(w==width&&h==height)
		memcpy(level.data(),pixels,level.size());
	else
		ScaleImage(c,width,height,(const unsigned char *)pixels,w,h,level.data());
	//every level, converted to RGBA, kept until sg_make_image has copied them
	std::vector<std::vector<unsigned char>> mips;
	sg_image_desc desc;
	memset(&desc,0,sizeof(desc));
	desc.width=w;
	desc.height=h;
	desc.pixel_format=SG_PIXELFORMAT_RGBA8;
	for(;;)
	{
		mips.emplace_back((size_t)w*h*4);
		ToRGBA(c,w*h,level.data(),mips.back().data());
		if((w==1&&h==1)||mips.size()==SG_MAX_MIPMAPS) break;
		int nw=w>1?w/2:1,nh=h>1?h/2:1;
		std::vector<unsigned char> next((size_t)nw*nh*c);
		HalveImage(c,w,h,level.data(),nw,nh,next.data());
		level.swap(next);
		w=nw;
		h=nh;
	}
	desc.num_mipmaps=(int)mips.size();
	for(size_t i=0;i<mips.size();i++)
	{
		desc.data.mip_levels[i].ptr=mips[i].data();
		desc.data.mip_levels[i].size=mips[i].size();
	}
	desc.label="texture";
	slots[slot].image=sg_make_image(&desc);
	sg_view_desc vdesc;
	memset(&vdesc,0,sizeof(vdesc));
	vdesc.texture.image=slots[slot].image;
	slots[slot].view=sg_make_view(&vdesc);
	slots[slot].filter=filter;
}

void TexSokolEndFrame()
{
	for(size_t i=0;i<deadviews.size();i++) sg_destroy_view(deadviews[i]);
	for(size_t i=0;i<deadimages.size();i++) sg_destroy_image(deadimages[i]);
	deadviews.clear();
	deadimages.clear();
}

void TexShutdown()
{
	RFlush();
	for(int i=0;i<TEX_NUMSLOTS;i++) Release(i);
	TexSokolEndFrame();
}
