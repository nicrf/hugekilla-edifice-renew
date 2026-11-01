//functions to draw everything in the 3d world, and some 2d overlays


#include "renderer.h"
#include "platform.h"
#include "fileio.h"
#include "models.h"
#include "gameplay.h"
#include "sock.h"
#include "stdio.h"
#include "land.h"
#include "vector.h"
#include "ase.h"
#include "hugekilla.h"
#include "playing.h"
#include "cfgfile.h"
#include "redrawing.h"
#include "math.h"
#include "multiplay.h"
#include "sound.h"
#include "effects.h"
#include "ui.h"
#include "hktime.h"
//a vertex at a computed point: the temporary binds to the reference, where
//glVertex3fv((float *)&(a+b)) took the address of an rvalue (GCC: -fpermissive)
static inline void Vertex(const Vector &v)
{
	RVertex3f(v.x,v.y,v.z);
}

float cloudposx,cloudposy;
float cloudmistposx,cloudmistposy;	
#define PI (3.1415926)
#define LANDSCAPERENDER RP_TRISTRIP
#define GTEXREPEAT 16
#define TEXMULT ((float)1/GTEXREPEAT)
#define BTEXMULT ((float)96/512)
#define MAXSCENERY 100
Vector camup;

int curfps;
int framefps;
float sunflarebright;

Particle particle[MAXPARTICLES];

int numscenery;
AseObj scenery[MAXSCENERY];
AseObj plobj[4];
AseObj projectile[10];
AseObj servercobs[50][3];
AseObj hangglider;
GraphicsPrefs graphicsprefs;
FxPrefs fxprefs={1,1,1,1,1,0};
GunDrawInfo gun[10];

int landfaces;
int blockfaces;

int cursendrate;
int currecvrate;


int numlillypad;
LillyPad lillypad[200];

Vector spark[30];
Vector sparkvel[30];
float sparklen[30];
float sparkbright[30];
int curspark;

float recvbright,sendbright;

DWORD nextlightning;
Vector lightningpos[500];
Vector lightningend[500];
int numlightning;




//sets the light to this point's light

inline void SetLandLight(int x, int y)
{
	int xs=x*4+cloudposx*LANDGRIDSIZE*8;
	int ys=y*4+cloudposy*LANDGRIDSIZE*8;
	x&=LANDGRIDSIZE-1;
	y&=LANDGRIDSIZE-1;
	float l=lighting[x][y];
	
	if(skysettings.sunheight&&skysettings.uselandclouds)
	{
		ys-=*GetRepeat(x,y)/skysettings.sunheight;
		xs&=1024-1;
		ys&=1024-1;
		float nf;
		nf=((float)*((BYTE*)&clouds[xs*1024+ys]))/256;
		l*=nf;
		if(l<.3)l=.3;
	}
	RColor4f(l,l,l,1);
}

//this is bad style, but it's the fastest way to do this

#define LV(x,y) \
{\
landfaces++;\
SetLandLight(x,y); \
RTexCoord2f(((float)x+xtexoffset)*TEXMULT,((float)y+ytexoffset)*TEXMULT);\
cf=1;\
if(x<inminx) cf*=1-((float)inminx-x)/(float)(inminx-minx+2);\
else if(x>inmaxx) cf*=1-(float)(inmaxx-x)/(float)(inmaxx-maxx-4);\
if(cf<0)cf=0;\
if(y<inminy) cf*=1-(float)(inminy-y)/(float)(inminy-miny+2);\
else if(y>inmaxy) cf*=1-(float)(inmaxy-y)/(float)(inmaxy-maxy-4);\
if(cf<0)cf=0;\
if(cf>1)cf=1;\
curheight=landmip[mipnum][x&(LANDGRIDSIZE-1)][y&(LANDGRIDSIZE-1)]*cf+landmip[mipnum+1][x&(LANDGRIDSIZE-1)][y&(LANDGRIDSIZE-1)]*(1-cf);\
}\
RVertex3f(x,curheight,y);

#define LANDVERTEX(x,y) LV((x),(y))			
#define TEXTURESWITCHRD(x,y,type,x1,y1,x2,y2)\
if(curtex!=landtex[(x&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE][(y&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE])\
{REnd();TexBind((curtex=landtex[(x&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE][(y&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE])+TEX_GROUND0);RBegin(type);\
LANDVERTEX(x1,y1);LANDVERTEX(x2,y2);}\

#define TEXTURESWITCH(x,y,type)\
if(curtex!=landtex[(x&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE][(y&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE])\
{REnd();TexBind((curtex=landtex[(x&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE][(y&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE])+TEX_GROUND0);RBegin(type);}\


//draws a block of land with a hole in the middle to allow for less detail far away by calling this function a few times

void DrawMip(float fcx,float fcy,float fhcx,float fhcy,int prevsize,int newsize,int res)
{
	int curtex=-1;
	if(newsize==0) return;
	float curheight;
	int mipnum=res;
	int cx,cy;
	cx=(((int)fcx)/(1<<mipnum+1))*(1<<mipnum+1);
	cy=(((int)fcy)/(1<<mipnum+1))*(1<<mipnum+1);
	int hcx,hcy;
	hcx=(((int)fhcx)/(1<<mipnum))*(1<<mipnum);
	hcy=(((int)fhcy)/(1<<mipnum))*(1<<mipnum);
	float xoff=fcx-cx;
	float yoff=fcy-cy;
	float cf;
	newsize*=1<<res;
	prevsize*=1<<(res-1);
	newsize-=1<<res;
	int minx,miny,maxx,maxy;
	int inminx,inminy,inmaxx,inmaxy;
	res=1<<res;
	minx=cx-newsize;
	maxx=cx+newsize-res;
	miny=cy-newsize;
	maxy=cy+newsize-res;
	inminx=hcx-prevsize;
	inmaxx=hcx+prevsize;
	inminy=hcy-prevsize;
	inmaxy=hcy+prevsize;
	float xtexoffset=-cx/GTEXREPEAT/TEXMULT;
	float ytexoffset=-cy/GTEXREPEAT/TEXMULT;
	int lastres=res;
	float height;
	int x,y;
	y=maxy;
	x=minx;
	TexBind(TEX_LAND);
	RCullFace(RFACE_BACK);
	RBegin(RP_TRIANGLES);
	y=maxy;
	x=minx;
	
	for(x=minx+res;x<=maxx-res;x+=res*2)
	{
		TEXTURESWITCH(x,y,RP_TRIANGLES);
		LANDVERTEX(x,y);
		LANDVERTEX(x,y+res*2);
		LANDVERTEX(x+res,y);
		LANDVERTEX(x+res,y);
		LANDVERTEX(x,y+res*2);
		LANDVERTEX(x+res*2,y+res*2);
		LANDVERTEX(x+res*2,y+res*2);
		LANDVERTEX(x+res*2,y);
		LANDVERTEX(x+res,y);
	}
	y=miny-res;
	x=minx;
	REnd();
	RCullFace(RFACE_FRONT);
	RBegin(RP_TRIANGLES);
	for(x=minx+res;x<=maxx-res;x+=res*2)
	{
		TEXTURESWITCH(x,y,RP_TRIANGLES);
		LANDVERTEX(x,y+res*2);
		LANDVERTEX(x,y);
		LANDVERTEX(x+res,y+res*2);
		LANDVERTEX(x+res,y+res*2);
		LANDVERTEX(x,y);
		LANDVERTEX(x+res*2,y);
		LANDVERTEX(x+res*2,y);
		LANDVERTEX(x+res*2,y+res*2);
		LANDVERTEX(x+res,y+res*2);
	}
	REnd();
	y=miny;
	x=minx-res;
	RCullFace(RFACE_BACK);
	RBegin(RP_TRIANGLES);
	for(y=miny+res;y<=maxy-res;y+=res*2)
	{
		TEXTURESWITCH(x,y,RP_TRIANGLES);
		LANDVERTEX(x+res*2,y);
		LANDVERTEX(x,y);
		LANDVERTEX(x+res*2,y+res);
		LANDVERTEX(x+res*2,y+res);
		LANDVERTEX(x,y);
		LANDVERTEX(x,y+res*2);
		LANDVERTEX(x,y+res*2);
		LANDVERTEX(x+res*2,y+res*2);
		LANDVERTEX(x+res*2,y+res);
	}
	REnd();
	y=miny;
	x=maxx;
	RCullFace(RFACE_FRONT);
	RBegin(RP_TRIANGLES);
	for(y=miny+res;y<=maxy-res;y+=res*2)
	{
		TEXTURESWITCH(x,y,RP_TRIANGLES);
		LANDVERTEX(x,y);
		LANDVERTEX(x+res*2,y);
		LANDVERTEX(x,y+res);
		LANDVERTEX(x,y+res);
		LANDVERTEX(x+res*2,y);
		LANDVERTEX(x+res*2,y+res*2);
		LANDVERTEX(x+res*2,y+res*2);
		LANDVERTEX(x,y+res*2);
		LANDVERTEX(x,y+res);
	}
	TEXTURESWITCH(minx-res,miny-res,RP_TRIANGLES);
	LANDVERTEX(minx-res,miny-res);
	LANDVERTEX(minx-res+res*2,miny-res);
	LANDVERTEX(minx-res,miny-res+res*2);
	LANDVERTEX(minx-res,miny-res+res*2);
	LANDVERTEX(minx-res+res*2,miny-res);
	LANDVERTEX(minx-res+res*2,miny-res+res*2);
	
	TEXTURESWITCH(maxx,miny-res,RP_TRIANGLES);
	LANDVERTEX(maxx,miny-res);
	LANDVERTEX(maxx+res*2,miny-res);
	LANDVERTEX(maxx,miny-res+res*2);
	LANDVERTEX(maxx,miny-res+res*2);
	LANDVERTEX(maxx+res*2,miny-res);
	LANDVERTEX(maxx+res*2,miny-res+res*2);
	
	TEXTURESWITCH(minx-res,maxy,RP_TRIANGLES);
	LANDVERTEX(minx-res,maxy);
	LANDVERTEX(minx-res+res*2,maxy);
	LANDVERTEX(minx-res,maxy+res*2);
	LANDVERTEX(minx-res,maxy+res*2);
	LANDVERTEX(minx-res+res*2,maxy);
	LANDVERTEX(minx-res+res*2,maxy+res*2);
	
	TEXTURESWITCH(maxx,maxy,RP_TRIANGLES);
	LANDVERTEX(maxx,maxy);
	LANDVERTEX(maxx+res*2,maxy);
	LANDVERTEX(maxx,maxy+res*2);
	LANDVERTEX(maxx,maxy+res*2);
	LANDVERTEX(maxx+res*2,maxy);
	LANDVERTEX(maxx+res*2,maxy+res*2);
	
	REnd();

	
	RCullFace(RFACE_BACK);
	
	for(y=inmaxy;y<maxy;y+=res)
	{
		
		RBegin(LANDSCAPERENDER);		
		for(x=minx+res;x<=maxx;x+=res)
		{
			LANDVERTEX(x,y);
			LANDVERTEX(x,y+res);
			TEXTURESWITCHRD(x,y,LANDSCAPERENDER,x,y,x,y+res);
			
			
		}
		REnd();
		
	}
	for(y=miny+res;y<inminy;y+=res)
	{
		RBegin(LANDSCAPERENDER);		
		for(x=minx+res;x<=maxx;x+=res)
		{
			LANDVERTEX(x,y);
			LANDVERTEX(x,y+res);
			TEXTURESWITCHRD(x,y,LANDSCAPERENDER,x,y,x,y+res);
			
		}
		REnd();
	}
	RCullFace(RFACE_FRONT);
	for(x=minx+res;x<inminx;x+=res)
	{
		RBegin(LANDSCAPERENDER);		
		for(y=inminy;y<=inmaxy;y+=res)
		{
			
			LANDVERTEX(x,y);
			LANDVERTEX(x+res,y);
			TEXTURESWITCHRD(x,y,LANDSCAPERENDER,x,y,x+res,y);
			
		}
		REnd();
	}
	for(x=inmaxx;x<maxx;x+=res)
	{
		RBegin(LANDSCAPERENDER);		
		for(y=inminy;y<=inmaxy;y+=res)
		{
			
			LANDVERTEX(x,y);
			LANDVERTEX(x+res,y);
			TEXTURESWITCHRD(x,y,LANDSCAPERENDER,x,y,x+res,y);
			
		}
		REnd();
	}
}	


//draw a few layers of clouds (not really a box)

void DrawCloudBox(Vector pos)
{
	RDisable(RCAP_DEPTHTEST);
	REnable(RCAP_TEXTURE);
	
	static Vector opos;
	SetCloudShift(cloudposx,cloudposy);
	if(cloudposx>8)cloudposx-=8;
	if(cloudposy>8)cloudposy-=8;
	if(cloudposx<0)cloudposx+=8;
	if(cloudposy<0)cloudposy+=8;
	if(cloudmistposx>8)cloudmistposx-=8;
	if(cloudmistposy>8)cloudmistposy-=8;
	if(cloudmistposx<0)cloudmistposx+=8;
	if(cloudmistposy<0)cloudmistposy+=8;
	
	float boxsize=1024;
	float highest=pos.y+GetSkySettings()->cloudheight;
	
	float texrepeat=2;
	int repeats=3;
	
	opos=pos;
	RCullFace(RFACE_BACK);
	TexBind(TEX_CLOUDS);
	RBlendFunc(RBLEND_ALPHA);
		
	RColor4f(1,1,1,.5);
	float fy;
	float fx;
	float of=(float)1/repeats;
	
	int x;
	for(x=-repeats;x<repeats;x++)
	{
		RBegin(RP_TRISTRIP);
		fx=(float)x/repeats;
		for(int y=-repeats;y<repeats;y++)
		{
			fy=(float)y/repeats;
			RTexCoord2f(texrepeat*fx+cloudposx,texrepeat*fy+cloudposy);
			RVertex3f(pos.x+boxsize*fx,highest,pos.z+boxsize*fy);
			RTexCoord2f(texrepeat*(fx+of)+cloudposx,texrepeat*fy+cloudposy);
			RVertex3f(pos.x+boxsize*(fx+of),highest,pos.z+boxsize*fy);
		}
		REnd();
	}
	if(GetSkySettings()->usemist)
	{
		highest-=60;
		RBlendFunc(RBLEND_ALPHA);
		TexBind(TEX_CLOUDMIST);
		RColor4f(1,1,1,GetSkySettings()->mistopacity);
		for(x=-repeats;x<repeats;x++)
		{
			RBegin(RP_TRISTRIP);
			fx=(float)x/repeats;
			for(int y=-repeats;y<repeats;y++)
			{
				fy=(float)y/repeats;
				RTexCoord2f(texrepeat*fx+cloudmistposx,texrepeat*fy+cloudmistposy);
				RVertex3f(pos.x+boxsize*fx,highest,pos.z+boxsize*fy);
				RTexCoord2f(texrepeat*(fx+of)+cloudmistposx,texrepeat*fy+cloudmistposy);
				RVertex3f(pos.x+boxsize*(fx+of),highest,pos.z+boxsize*fy);
			}
			REnd();
		}
	}
}
float XNC,XPC,YNC,YPC,ZNC,ZPC,TC2,TC3,TC4,TC5;  //global for efficiency

struct BlockBuf
{
	char side;
	short x,y,z;
};

BlockBuf blockbuf[32][10000];
int numblockbuf[32];

//draws a face of a block

inline void DrawBlockSide(BlockBuf *cb)
{
	blockfaces+=2;
	short x,y,z;
	x=cb->x;
	y=cb->y;
	z=cb->z;
	char type=GetBlock(x,y,z)->type;
	x*=BLOCKSIZE;
	y*=BLOCKSIZE;
	z*=BLOCKSIZE;
	char side=cb->side;
	
	short t1,t2,t3,t4;
	if (type==1){t1=1;t2=1;t3=1;t4=1;}
	else
	if (type==2){t1=1;t2=1;t3=0;t4=0;}
	else
	if (type==3){t1=0;t2=1;t3=1;t4=0;}
	else
	if (type==4){t1=0;t2=0;t3=1;t4=1;}
	else
	if (type==5){t1=1;t2=0;t3=0;t4=1;}
	switch(side)
	{
	case SIDEXN:
	{
		RColor3f(XNC,XNC,XNC);
		RTexCoord2f(0,0);				RVertex3f(x,y,z);
		RTexCoord2f(1,0);		RVertex3f(x,y,z+BLOCKSIZE);
		RTexCoord2f(1,1);RVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		RTexCoord2f(0,t1);		RVertex3f(x,y+BLOCKSIZE*t1,z);
		
		break;
	}
	case SIDEXP:
	{
		RColor3f(XPC,XPC,XPC);
		RTexCoord2f(1,0);				RVertex3f(x+BLOCKSIZE,y,z);
		RTexCoord2f(1,t4);		RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		RTexCoord2f(0,t3);RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
		RTexCoord2f(0,0);		RVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		break;
	}
	case SIDEYN:
	{
		RColor3f(YNC,YNC,YNC);
		RTexCoord2f(0,0);				RVertex3f(x,y,z);
		RTexCoord2f(1,0);		RVertex3f(x+BLOCKSIZE,y,z);
		RTexCoord2f(1,1);RVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		RTexCoord2f(0,1);		RVertex3f(x,y,z+BLOCKSIZE);
		
		break;
	}
	case SIDEYP:
	{
		switch(type)
		{
		case 1:
			RColor3f(YPC,YPC,YPC);
			break;
		case 2:
			RColor3f(TC2,TC2,TC2);
			break;
		case 3:
			RColor3f(TC3,TC3,TC3);
			break;
		case 4:
			RColor3f(TC4,TC4,TC4);
			break;
		case 5:
			RColor3f(TC5,TC5,TC5);
			break;
		}
		
		RTexCoord2f(0,0);				RVertex3f(x,y+BLOCKSIZE*t1,z);
		RTexCoord2f(0,1);		RVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		RTexCoord2f(1,1);RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
		RTexCoord2f(1,0);		RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		
		break;
	}
	case SIDEZN:
	{
		RColor3f(ZNC,ZNC,ZNC);
		RTexCoord2f(1,0);		RVertex3f(x,y,z);
		RTexCoord2f(1,t1);RVertex3f(x,y+BLOCKSIZE*t1,z);
		RTexCoord2f(0,t4);		RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		RTexCoord2f(0,0);				RVertex3f(x+BLOCKSIZE,y,z);
		
		break;
	}
	
	case SIDEZP:
	{
		RColor3f(ZPC,ZPC,ZPC);
		RTexCoord2f(0,0);				RVertex3f(x,y,z+BLOCKSIZE);
		RTexCoord2f(1,0);		RVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		RTexCoord2f(1,t3);RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
		RTexCoord2f(0,t2);		RVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		
		break;
	}	
	}
}

//the shade of a face of the build preview: grey, or red where it can't be built
static inline void BlockColor(float c,char red)
{
	if(red) RColor3f(.5f+c*.5f,c*.15f,c*.15f);
	else RColor3f(c,c,c);
}

//draws an individual block (the wireframe of the build preview)

void DrawBlock(int x,int y,int z,unsigned char ds,char type,int &px,int &py,int &pz,char red)
{
	x*=BLOCKSIZE;
	y*=BLOCKSIZE;
	z*=BLOCKSIZE;
	int t1,t2,t3,t4;
	float xtexoffset=0,ytexoffset=0;
	xtexoffset=0,ytexoffset=0;
	float stdxoff=(float)16/512;
	float stdyoff=(float)16/512;
	xtexoffset=xtexoffset/4+stdxoff;
	ytexoffset=ytexoffset/4+stdyoff;
	if (type==1){t1=1;t2=1;t3=1;t4=1;}
	else
	if (type==2){t1=1;t2=1;t3=0;t4=0;}
	else
	if (type==3){t1=0;t2=1;t3=1;t4=0;}
	else
	if (type==4){t1=0;t2=0;t3=1;t4=1;}
	else
	if (type==5){t1=1;t2=0;t3=0;t4=1;}
	if(x>px&&ds&SIDEXN)
	{
		BlockColor(XNC,red);
		RTexCoord2f(xtexoffset,ytexoffset);				RVertex3f(x,y,z);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x,y,z+BLOCKSIZE);
		RTexCoord2f(xtexoffset,BTEXMULT*t1+ytexoffset);		RVertex3f(x,y+BLOCKSIZE*t1,z);
		RTexCoord2f(xtexoffset,BTEXMULT*t1+ytexoffset);		RVertex3f(x,y+BLOCKSIZE*t1,z);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x,y,z+BLOCKSIZE);
		RTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT+ytexoffset);RVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
	}
	 if(x<px&&ds&SIDEXP)
	{
		BlockColor(XPC,red);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);				RVertex3f(x+BLOCKSIZE,y,z);
		RTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t4+ytexoffset);		RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		RTexCoord2f(xtexoffset,ytexoffset);		RVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		RTexCoord2f(xtexoffset,ytexoffset);		RVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		RTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t4+ytexoffset);		RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		RTexCoord2f(xtexoffset,BTEXMULT*t3+ytexoffset);RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
	}
	xtexoffset=1,ytexoffset=0;
	xtexoffset=xtexoffset/4+stdxoff;
	ytexoffset=ytexoffset/4+stdyoff;
	if(y>py&&ds&SIDEYN)
	{
		BlockColor(YNC,red);
		RTexCoord2f(xtexoffset,ytexoffset);				RVertex3f(x,y,z);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x+BLOCKSIZE,y,z);
		RTexCoord2f(xtexoffset,BTEXMULT+ytexoffset);		RVertex3f(x,y,z+BLOCKSIZE);
		RTexCoord2f(xtexoffset,BTEXMULT+ytexoffset);		RVertex3f(x,y,z+BLOCKSIZE);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x+BLOCKSIZE,y,z);
		RTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT+ytexoffset);RVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
	}
	xtexoffset=0,ytexoffset=1;
	xtexoffset=xtexoffset/4+stdxoff;
	ytexoffset=ytexoffset/4+stdyoff;
	
	if(y<=py-BLOCKSIZE&&ds&SIDEYP||type!=1)
	{
		switch(type)
		{
		case 1:
			BlockColor(YPC,red);
			break;
		case 2:
			BlockColor(TC2,red);
			break;
		case 3:
			BlockColor(TC3,red);
			break;
		case 4:
			BlockColor(TC4,red);
			break;
		case 5:
			BlockColor(TC5,red);
			break;
		}
		
		RTexCoord2f(xtexoffset,ytexoffset);				RVertex3f(x,y+BLOCKSIZE*t1,z);
		RTexCoord2f(xtexoffset,BTEXMULT+ytexoffset);		RVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		RTexCoord2f(xtexoffset,BTEXMULT+ytexoffset);		RVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		RTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT+ytexoffset);RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
	}
	xtexoffset=0,ytexoffset=0;
	xtexoffset=xtexoffset/4+stdxoff;
	ytexoffset=ytexoffset/4+stdyoff;
	if(z>pz&&ds&SIDEZN)
	{
		BlockColor(ZNC,red);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x,y,z);
		RTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t1+ytexoffset);RVertex3f(x,y+BLOCKSIZE*t1,z);
		RTexCoord2f(xtexoffset,ytexoffset);				RVertex3f(x+BLOCKSIZE,y,z);
		RTexCoord2f(xtexoffset,ytexoffset);				RVertex3f(x+BLOCKSIZE,y,z);
		RTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t1+ytexoffset);RVertex3f(x,y+BLOCKSIZE*t1,z);
		RTexCoord2f(xtexoffset,BTEXMULT*t4+ytexoffset);		RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
	}
	if(z<pz&&ds&SIDEZP)
	{
		BlockColor(ZPC,red);
		RTexCoord2f(xtexoffset,ytexoffset);				RVertex3f(x,y,z+BLOCKSIZE);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		RTexCoord2f(xtexoffset,BTEXMULT*t2+ytexoffset);		RVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		RTexCoord2f(xtexoffset,BTEXMULT*t2+ytexoffset);		RVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		RTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		RVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		RTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t3+ytexoffset);RVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
	}	
}


//draws the blocks that make up the buildings

Vector eyepos;

void DrawBlocks(int px,int py,int pz,int size)
{
	px/=BLOCKSIZE;
	py/=BLOCKSIZE;
	pz/=BLOCKSIZE;
	Block *cb;
	int y;
	float color;
	
	Vector lightdir(0,GetSkySettings()->sunheight,-1);
	lightdir=Normalize(lightdir);
	

	//first calculate the brightness of the six faces

	color=(DotProduct(lightdir,Vector(-1,0,0)));
	if(color<.3)color=.3;
	XNC=color;
	color=(DotProduct(lightdir,Vector(1,0,0)));
	if(color<.3)color=.3;
	XPC=color;
	color=(DotProduct(lightdir,Vector(0,-1,0)));
	if(color<.3)color=.3;
	YNC=color;
	color=(DotProduct(lightdir,Vector(0,1,0)));
	if(color<.3)color=.3;
	YPC=color;
	color=(DotProduct(lightdir,Vector(0,0,1)));
	if(color<.3)color=.3;
	ZNC=color;
	color=(DotProduct(lightdir,Vector(0,0,-1)));
	if(color<.3)color=.3;
	ZPC=color;
	color=(DotProduct(lightdir,Normalize(Vector(1,1,0))));
	if(color<.3)color=.3;
	TC2=color;
	color=(DotProduct(lightdir,Normalize(Vector(0,1,1))));
	if(color<.3)color=.3;
	TC3=color;
	color=(DotProduct(lightdir,Normalize(Vector(-1,1,0))));
	if(color<.3)color=.3;
	TC4=color;
	color=(DotProduct(lightdir,Normalize(Vector(0,1,-1))));
	if(color<.3)color=.3;
	TC5=color;
	
	int i;
	for(i=0;i<32;i++)
	{
		numblockbuf[i]=0;
	}

	int plx,ply,plz;
	Vector ppos;
	ppos=eyepos;
	plx=ppos.x/BLOCKSIZE;
	ply=ppos.y/BLOCKSIZE;
	plz=ppos.z/BLOCKSIZE;
	//RDisable(RCAP_CULL);
	for(int x=px-size;x<px+size;x++)
	{
		for(int z=pz-size;z<pz+size;z++)
		{
			y=py-size;
			if(y<1) y=1;
			do
			{
				cb=GetBlock(x,y,z);//&blocks[x&(BLOCKGRIDSIZE-1)][y&(BLOCKGRIDHEIGHT-1)][z&(BLOCKGRIDSIZE-1)];
				if(cb->type&&cb->drawside)
				{
					//DrawBlock(x,y,z,cb->drawside,cb->type,plx,ply,plz);
					if((cb->drawside&SIDEXP)&&x<plx)
					{
						BlockBuf *curbuf=&blockbuf[cb->side][numblockbuf[cb->side]++];
						curbuf->side=SIDEXP;curbuf->x=x;curbuf->y=y;curbuf->z=z;
					}
					if((cb->drawside&SIDEXN)&&x>plx)
					{
						BlockBuf *curbuf=&blockbuf[cb->side][numblockbuf[cb->side]++];
						curbuf->side=SIDEXN;curbuf->x=x;curbuf->y=y;curbuf->z=z;
					}
					if(cb->drawside&SIDEYP)
						if(y<ply+1||cb->type!=1)
					{
						BlockBuf *curbuf=&blockbuf[cb->top][numblockbuf[cb->top]++];
						curbuf->side=SIDEYP;curbuf->x=x;curbuf->y=y;curbuf->z=z;
					}
					if(cb->drawside&SIDEYN&&y>ply)
					{
						BlockBuf *curbuf=&blockbuf[cb->bottom][numblockbuf[cb->bottom]++];
						curbuf->side=SIDEYN;curbuf->x=x;curbuf->y=y;curbuf->z=z;
					}
					if(cb->drawside&SIDEZP&&z<plz)
					{
						BlockBuf *curbuf=&blockbuf[cb->side][numblockbuf[cb->side]++];
						curbuf->side=SIDEZP;curbuf->x=x;curbuf->y=y;curbuf->z=z;
					}
					if(cb->drawside&SIDEZN&&z>plz)
					{
						BlockBuf *curbuf=&blockbuf[cb->side][numblockbuf[cb->side]++];
						curbuf->side=SIDEZN;curbuf->x=x;curbuf->y=y;curbuf->z=z;
					}
				}
				y+=cb->nextblock;
				if(cb->nextblock==0) return;
			}
			while(y<py+size);
		}
	}
	for(i=0;i<32;i++)
	{
		TexBind(TEX_BLOCK0+i);
		RBegin(RP_QUADS);
		for(int i2=0;i2<numblockbuf[i];i2++)
		{
			DrawBlockSide(&blockbuf[i][i2]);
		}
		REnd();
	}
	RPushAttrib(RATTR_ENABLE|RATTR_FOG|RATTR_COLOR|RATTR_POLYGON);
	RDisable(RCAP_TEXTURE);
	RDisable(RCAP_BLEND);	
	RBlendFunc(RBLEND_ADDONE);
	RBegin(RP_LINES);
	if(menu==BUILDING) PlaceStructure(0);
	REnd();
	RPopAttrib();
	
}

//draws 2d text at x,y

float DrawText(float x,float y,const char *text)
{
	
	TexBind(TEX_FONT);
	RBegin(RP_QUADS);
	int xp,yp;
	float xtp,ytp;
	float tsize=(float)1/16;
	float tysize=(float)1/16;
	float toffset=(float)0/64;
	int sp=0;
	float ox=x;
	while(text[sp]!=0)
	{
		if(text[sp]=='\r') 
		{
			x=ox;
			y-=.05;
			sp++;
			continue;
		}
		xp=(WORD)text[sp]-32;
		yp=xp/16;
		xp=xp%16;
		xtp=(float)xp/16; ytp=(float)yp/16;
		RTexCoord2f(xtp+toffset,ytp);
		RVertex2f(x,y+.07);
		RTexCoord2f(xtp+toffset,ytp+tysize);
		RVertex2f(x,y);
		RTexCoord2f(xtp+tsize+toffset,ytp+tysize);
		RVertex2f(x+(.040),y);
		RTexCoord2f(xtp+tsize+toffset,ytp);
		RVertex2f(x+(.040),y+.07);
		x+=.040;
		sp++;
	}
	
	REnd();
	return 1;
}

//game mode 0: where each team may build (BUILDZONE around its water pump, see
//CanBuild), on the radar: centered on the local player, turned with its view, 250
//units to the edge; the circles are cut at the edge of the radar with the scissor
static void DrawRadarBuildZones(float cx,float cy,float w,float h)
{
	if(sv.gamemode!=0) return;
	const float scale=1.f/250;
	int vp[4];
	RGetViewport(vp);
	REnable(RCAP_SCISSOR);
	RScissor(vp[0]+(int)((cx-w+1)/2*vp[2]),vp[1]+(int)((cy-h+1)/2*vp[3]),
		(int)(w*vp[2]),(int)(h*vp[3]));
	REnable(RCAP_BLEND);
	RBlendFunc(RBLEND_ALPHA);
	Player *me=GetLocalPlayer();
	Vector side=CrossProduct(Vector(0,1,0),me->fwd);
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		if(sobject[i].type!=WATERPUMP||sobject[i].attached!=-1) continue;
		Vector p=sobject[i].pos;
		MakeClose(&p);
		p-=me->pos;
		p.y=0;
		Vector r=(p.x*me->fwd+p.z*side)*scale;   //same turn as the radar's dots
		float x=cx-r.z*w,y=cy+r.x*h,rx=BUILDZONE*scale*w,ry=BUILDZONE*scale*h;
		float red=sobject[i].team==1?1:.45f,green=sobject[i].team==1?.35f:.6f,blue=sobject[i].team==1?.35f:1;
		float a=sobject[i].team==me->team?1:.6f;   //ours stands out
		RColor4f(red,green,blue,.22f*a);
		RBegin(RP_TRIFAN);
		RVertex2f(x,y);
		for(int k=0;k<=32;k++)
			RVertex2f(x+rx*cosf(k*PI/16),y+ry*sinf(k*PI/16));
		REnd();
		RLineWidth(HKPixelScale());
		RColor4f(red,green,blue,.7f*a);
		RBegin(RP_LINELOOP);
		for(int k=0;k<32;k++)
			RVertex2f(x+rx*cosf(k*PI/16),y+ry*sinf(k*PI/16));
		REnd();
	}
	RDisable(RCAP_SCISSOR);
}

//draw text in 3d space using the 2d DrawText function

void DrawText3D(Vector facing,Vector pos,float scale,char *text)
{
	RCullFace(RFACE_BACK);
	//(here glBlendFunc(GL_COLOR,GL_ZERO): not a blend factor, GL ignored the call)
	facing=Normalize(facing);
	Vector x,y,z;
	x=Normalize(CrossProduct(facing,Vector(0,1,0))*-1);
	y=GetRight(facing,Vector(0,1,0))*.6;
	z=facing;
	float mx[4][4]=
	{
	{x.x*scale,x.y*scale,x.z*scale,0}, 
	{y.x*scale,y.y*scale,y.z*scale,0}, 
	{z.x*scale,z.y*scale,z.z*scale,0}, 
	{pos.x,pos.y,pos.z,1}
	};
	Mat4 modelview=RModelView();
	RSetModelView(Mat4Mul(modelview,Mat4From(mx)));
	DrawText(-(float)strlen(text)*.040/2,0,text);
	RSetModelView(modelview);
	RCullFace(RFACE_FRONT);
}

//the waves' phase, moved a little at each DrawWater
static float wox;
static float woy;

//the surface's height at x, as DrawWater draws it (the waves run along x); effects
//put their ripples there
float WaterSurfaceHeight(float x)
{
	return WATERHEIGHT+sinf(x/WATERBLOCKSIZE*3.141592653f/4+woy*3.141592653f*8)/6;
}

//draws the water where it needs to be drawn (the isdrawing variable is used to restart the triangle strip,
// since having a huge strip was causing problems on some graphics cards)

void DrawWater(int size,float cx,float cy)
{
	RDisable(RCAP_CULL);
	
	cx/=WATERBLOCKSIZE;
	cy/=WATERBLOCKSIZE;
	int isdrawing;
	int isvisible;
	woy+=.0009;
	wox+=.0003;
	if(wox>4) wox-=4;
	if(woy>4) woy-=4;
	RDisable(RCAP_CULL);
	int belowwater=GetLocalPlayer()->pos.y<WATERHEIGHT;
	TexBind(TEX_CLOUDS);
	RBlendFunc(RBLEND_ALPHA);
	float vpx=GetLocalPlayer()->pos.x/-64;
	float vpy=GetLocalPlayer()->pos.z/-64;
	for(int x=cx-size;x<cx+size;x++)
	{
		int ysincerestart=0;
		isdrawing=0;
		float heightoffy=0;
		float heightoffx=sin((float)x*3.141592653/4+woy*3.141592653*8)/6;
		float reflectoffx=(vpx+(float)x/16+sin((float)x*3.141592653/8+woy*3.141592653*1-3.141592653/4)*.1+1/4)/2;
		float heightoffx1=sin((float)(x+1)*3.141592653/4+woy*3.141592653*8)/6;
		float reflectoffx1=(vpx+(float)(x+1)/16+sin((float)(x+1)*3.141592653/8+woy*3.141592653*1-3.141592653/4)*.1+1/4)/2;
		for(int y=cy-size;y<cy+size;y++)
		{
			isvisible=GetWater(x,y);
			if(isvisible&&isdrawing==0)
			{
				RBegin(RP_TRISTRIP);
				isdrawing=1;
				ysincerestart=0;
			}
			if(isdrawing)
			{
				float reflectoffy=vpy+(float)y/16;
				float lightoff=0;
				float light=*GetWaterLight(x,y);
				light+=lightoff;
				if(belowwater) light=.3;
				RColor4f(light,light,light,.7);
				RTexCoord2f(reflectoffx,reflectoffy);
				RVertex3f(x*WATERBLOCKSIZE,WATERHEIGHT+heightoffy+heightoffx,y*WATERBLOCKSIZE);
				light=*GetWaterLight(x+1,y);
				light+=lightoff;
				if(belowwater) light=.3;
				RColor4f(light,light,light,.7);
				RTexCoord2f(reflectoffx1,reflectoffy);
				RVertex3f((x+1)*WATERBLOCKSIZE,WATERHEIGHT+heightoffy+heightoffx1,y*WATERBLOCKSIZE);
				ysincerestart++;
				if(ysincerestart>12) 
				{
					REnd();
					RBegin(RP_TRISTRIP);
					y--;
					ysincerestart=0;
				}
				
			}
			if(!isvisible&&isdrawing)
			{
				isdrawing=0;
				REnd();
			}
		}
		if(isdrawing)
		{
			isdrawing=0;
			REnd();
		}
	}
	TexBind(TEX_LILLYPAD);
	RColor4f(1,1,1,1);
	RBegin(RP_QUADS);
	int underwater=GetLocalPlayer()->pos.y<WATERHEIGHT;
	float yoffset=.1;
	if(underwater) 
	{
		RColor4f(.6,.6,.6,1);
		yoffset=-.1;
	}
	for(int i=0;i<numlillypad;i++)
	if(MakeClose(&lillypad[i].pos))
	{
		float x=lillypad[i].pos.x;
		float z=lillypad[i].pos.z;
		float y=WATERHEIGHT+sin((float)(x/WATERBLOCKSIZE)*3.141592653/4+woy*3.141592653*8)/6+yoffset;
		float size=lillypad[i].size;
		RTexCoord2f(0,0);RVertex3f(x-size,y,z-size);
		RTexCoord2f(0,1);RVertex3f(x-size,y,z+size);
		RTexCoord2f(1,1);RVertex3f(x+size,y,z+size);
		RTexCoord2f(1,0);RVertex3f(x+size,y,z-size);
	}
	REnd();
	REnable(RCAP_CULL); 
	
}

//draws the sun and a flare around it

void DrawSun()
{
	RCullFace(RFACE_FRONT);
	Player *cur=GetLocalPlayer();
	RDisable(RCAP_FOG);
	//RDisable(RCAP_DEPTHTEST);
	RBlendFunc(RBLEND_ADD);
	TexBind(TEX_SUNBACK);
	RDisable(RCAP_DEPTHTEST);
	RBegin(RP_TRISTRIP);
	RColor4f(1,1,1,GetSkySettings()->suneyelight*sunflarebright);
	RTexCoord2f(0,0);
	RVertex3f(cur->pos.x-50,cur->pos.y+60*GetSkySettings()->sunheight+50,cur->pos.z+60);
	RTexCoord2f(1,0);
	RVertex3f(cur->pos.x-50,cur->pos.y+60*GetSkySettings()->sunheight-50,cur->pos.z+60);
	RTexCoord2f(0,1);
	RVertex3f(cur->pos.x+50,cur->pos.y+60*GetSkySettings()->sunheight+50,cur->pos.z+60);
	RTexCoord2f(1,1);
	RVertex3f(cur->pos.x+50,cur->pos.y+60*GetSkySettings()->sunheight-50,cur->pos.z+60);
	REnd();

	TexBind(TEX_SUN);
	REnable(RCAP_DEPTHTEST);
	RBegin(RP_TRISTRIP);
	RColor4f(1,1,1,1);
	RTexCoord2f(0,0);
	float dist=160;
	float size=dist/60*2;
	RVertex3f(cur->pos.x-size,cur->pos.y+dist*GetSkySettings()->sunheight+size,cur->pos.z+dist);
	RTexCoord2f(1,0);
	RVertex3f(cur->pos.x-size,cur->pos.y+dist*GetSkySettings()->sunheight-size,cur->pos.z+dist);
	RTexCoord2f(0,1);
	RVertex3f(cur->pos.x+size,cur->pos.y+dist*GetSkySettings()->sunheight+size,cur->pos.z+dist);
	RTexCoord2f(1,1);
	RVertex3f(cur->pos.x+size,cur->pos.y+dist*GetSkySettings()->sunheight-size,cur->pos.z+dist);
	REnd();
}

//the local player's gun (the model in front of the camera), where the scene's
//matrices put it

static void DrawLocalGun(Player *cur)
{
	Vector pos,front,top,right;
	Vector gunup,partup,partright,partfront;
	int closetime=curtime-cur->gundowntime;
	if(fabs((float)(cur->nextfiretime-curtime))<closetime)closetime=fabs((float)(cur->nextfiretime-curtime))<closetime;
	if(!cur->gundowntime) closetime=0;
	if(closetime>500)closetime=500;
	top=GetRight(cur->dir,camup);
	front=Normalize(cur->dir-top*(float)closetime*.003);
	top=GetRight(front,top);
	right=CrossProduct(top,front);
	top=VectMult(cur->gunmainup,front,top,right);
	pos=cur->pos+cur->fwd*.03+cur->gunoffset-top*(float)closetime*.0015;
	
	
	partup=VectMult(gun[cur->weapon].partup[0],front,top,right);
	partright=VectMult(gun[cur->weapon].partright[0],front,top,right);
	partfront=front;
	
	gunup=VectMult(cur->gunup,partfront,partup,partright);
	pos+=cur->dir*(1-cur->gunkick)*.5;
	pos+=VectMult(gun[cur->weapon].offset,front,top,right);
	Vector partpos=pos+VectMult(gun[cur->weapon].partoffset[0],front,top,right);
	gun[cur->weapon].part[0].Draw(partpos,partfront,gunup);
	gun[cur->weapon].main.Draw(pos,front,top);
}

static void DrawGunExplosions(int local);

//The weapon pass (M13.5): the gun and its muzzle flash last, over a cleared depth
//buffer, so no wall or hill in front of the camera ever cuts into them. Its own
//projection has a fixed field of view (the zoom magnifies the world, not the gun
//held at the eye) and a short near plane (the gun is half a unit away, inside the
//scene's .15). The modelview and fog stay the scene's, and the model keeps its
//world-space sun lighting, so it is lit and fogged like everything around it
//(underwater too).

static void DrawViewModel(Player *cur)
{
	Mat4 proj=RProjection();
	RPushAttrib(RATTR_ENABLE|RATTR_COLOR|RATTR_POLYGON|RATTR_TEXTURE);
	REnable(RCAP_TEXTURE|RCAP_BLEND|RCAP_DEPTHTEST|RCAP_CULL);
	RDisable(RCAP_SCISSOR);
	RBlendFunc(RBLEND_ALPHA);
	RCullFace(RFACE_FRONT);   //as the scene draws models (DrawMip leaves it so)
	RDepthMask(1);
	RClear(0,1);              //depth only: the gun's own depth range
	RSetProjection(Mat4Perspective(70,1.2,.02,20));
	DrawLocalGun(cur);
	DrawGunExplosions(1);     //the flash at the muzzle, over the gun as before
	RSetProjection(proj);
	RPopAttrib();
}

//draws the explosion coming out from the gun: of every player but the local one
//(local 0), or of the local one alone (local 1), which the weapon pass draws

static void DrawGunExplosions(int local)
{
	Vector pos,front,top,right;
	for(int i=0;i<GetNumPlayers();i++)
	{
		
		Player *pl=GetPlayer(i);
		if((pl==GetLocalPlayer())!=(local!=0)) continue;
	
		if(curtime-pl->lastfiretime<200)
		{
			float timesincefire=curtime-pl->lastfiretime;
			timesincefire/=10;
			pos=pl->pos+pl->fwd*.03;
			front=pl->dir;
			top=GetRight(front,camup);
			right=CrossProduct(top,front);
			pos+=pl->dir*(1-pl->gunkick)*.5;
			pos+=VectMult(gun[pl->weapon].offset,front,top,right)+pl->gunoffset;
			
			pl->curexplosionrot+=.01;
			
			Vector epos[4];
			float factor=(((float)timesincefire)*.04+1)*gun[pl->weapon].explosionsize;
			float ang=pl->curexplosionrot;
			if(gun[pl->weapon].explosionnorotate) ang=0;
			if(gun[pl->weapon].explosionnorotate) factor=gun[pl->weapon].explosionsize;
			pos+=VectMult(gun[pl->weapon].explosionoffset,front,top,right);
			
			epos[0]=pos+top*factor*(sin(ang)*1)+right*factor*(cos(ang)*1);
			epos[1]=pos+top*factor*(sin(ang+PI/2)*1)+right*factor*(cos(ang+PI/2)*1);
			epos[2]=pos+top*factor*(sin(ang+PI*1.5)*1)+right*factor*(cos(ang+PI*1.5)*1);
			epos[3]=pos+top*factor*(sin(ang+PI)*1)+right*factor*(cos(ang+PI)*1);
			
			//the light of the shot around the sprite's flame, for its first 80 ms
			//(M13.5). Before the sprite: the sprite's whole quad writes depth, and
			//the glow, in about its plane, would fail the test inside it
			if(fxprefs.muzzleflash&&timesincefire<8&&pl->weapon!=4)   //4: thrown grenades
				FxMuzzleGlow(pos,factor*1.1f,.7f*(1-timesincefire/8),pl->curexplosionrot*37);

			REnable(RCAP_DEPTHTEST);
			RDisable(RCAP_FOG);
			RBlendFunc(RBLEND_ALPHA);
			TexBind(TEX_EXPLOSION0+pl->weapon*2);

			RBegin(RP_TRISTRIP);
			RColor4f(1,1,1,(float)(10-timesincefire)/5);
			RTexCoord2f(1,1);
			RVertex3fv((float*)&epos[0]);
			RTexCoord2f(1,0);
			RVertex3fv((float*)&epos[1]);
			RTexCoord2f(0,1);
			RVertex3fv((float*)&epos[2]);
			RTexCoord2f(0,0);
			RVertex3fv((float*)&epos[3]);

			REnd();
		}
	}
}



//draws a cylinder with the names of the guns on it to see what you are selecting

float curgunselrot,curgunselrotvel;
void DrawGunSelection()
{
	float bright=1.8-(float)(curtime-GetLocalPlayer()->lastswitchtime)/1000;
	if(bright<0) return;
	if(bright>1) bright=1;
	RPushAttrib(RATTR_ENABLE|RATTR_FOG|RATTR_COLOR|RATTR_POLYGON);
	RDisable(RCAP_CULL);
	float targrot=-(float)GetLocalPlayer()->weapon/8*pi*2-pi-.3;
	curgunselrotvel+=(targrot-curgunselrot)/20;
	if(fabs(curgunselrot-targrot)<.3)
	{
		curgunselrotvel+=(targrot-curgunselrot)/5;
		curgunselrotvel/=1.6;
	}
	curgunselrotvel/=1.2;
	curgunselrot+=curgunselrotvel;
	float rot=curgunselrot;
	REnable(RCAP_TEXTURE);
	TexBind(TEX_GUNNAMES);
	RBegin(RP_TRISTRIP);
	
	
	for(int i=0;i<64;i++)
	{
		float y=sin((float)i/64*pi*2+rot)/2;
		float z=cos((float)i/64*pi*2+rot)/1.6;
		RColor4f(1,1,1,(-z/1.5+.4)*bright);
		z+=2;
		float offx=.2;
		float offy=-.4;
		RTexCoord2f(0,(float)i/64);
		RVertex2f(offx-.8/z,offy+y/z);
		RTexCoord2f(1,(float)i/64);
		RVertex2f(offx+.8/z,offy+y/z);
	}
	REnd();
	RPopAttrib();
}

//draws the console alone on a black screen (loading, connecting); in game it is drawn
//over the world (DrawConsolePanel, src/ui/console.cpp)

void DrawConsole()
{
	RClearColor(0,0,0,0);
	RClear(1,0);
	DrawConsolePanel(1);
	UIFrameEnd();
	HKSwapWindow();
}

//redraw calls draw to draw the world, then it adds 2d graphics on top

void ScreenShot()
{
	struct BMoPHdr
{
	char id[2];
	DWORD FileSize;
	DWORD Reserved;
	DWORD DataOffset;
	DWORD HeaderSize;
	DWORD Width;
	DWORD Height;
	WORD Planes;
	WORD BPP;
	DWORD Compression;
	DWORD DataSize;
	DWORD XRes;
	DWORD YRes;
	DWORD Colors;
	DWORD ImportantColors;

};

	if(GetLocalPlayer()->takescreenshot)
	{
		int vp[4];
		RGetViewport(vp);
		int planesize=(vp[2]-vp[0])*(vp[3]-vp[1]);
		int bufsize=planesize*3+sizeof(BMoPHdr);
		char *filebuf=new char[bufsize];
		RReadPixels(vp[0],vp[1],vp[2],vp[3],1,(unsigned char *)&filebuf[sizeof(BMoPHdr)]);
		BMoPHdr *hdr=(BMoPHdr *)filebuf;
		hdr->id[0]='B';
		hdr->id[1]='M';
		hdr->FileSize=bufsize;
		hdr->Reserved=0;
		hdr->HeaderSize=40;
		hdr->DataOffset=54;
		hdr->Width=vp[2]-vp[0];
		hdr->Height=vp[3]-vp[1];
		hdr->Planes=1;
		hdr->BPP=24;
		hdr->Compression=0;
		hdr->DataSize=0;
		hdr->XRes=2834;
		hdr->YRes=2834;
		hdr->Colors=0;
		hdr->ImportantColors=0;
		char filename[255];
		strcpy(filename,"screenshot-");
		sprintf(&filename[strlen(filename)],"%x",(unsigned)(int)(RND*1000));
		strcat(filename,".bmp");
		FILE *file=HKFileOpen(UserFile(filename),"wb");
		if(file)
		{
			fwrite(filebuf,1,bufsize,file);
			fclose(file);
		}
		delete []filebuf;
		GetLocalPlayer()->takescreenshot=0;
	}
}


void Redraw()
{
	HKSwapWindow();
	RSet2D();
	int viewport[4];
	RGetViewport(viewport);
	//(an aspect and zoom scale was multiplied into the projection here, which Draw
	//replaced before drawing anything)
	cloudposx+=GetSkySettings()->cloudvelx;
	cloudposy+=GetSkySettings()->cloudvely;
	cloudmistposx+=GetSkySettings()->cloudmistvelx;
	cloudmistposy+=GetSkySettings()->cloudmistvely;

	blockfaces=0;
	landfaces=0;
	
	{
		RClear(1,0);
	
		if(graphicsprefs.stereoseparation==0)
			Draw(0);
		else
		{
			RViewport(viewport[0],viewport[1],viewport[2]/2,viewport[3]);
			Draw(-graphicsprefs.stereoseparation);
			RViewport(viewport[2]/2,viewport[1],viewport[2]/2,viewport[3]);
			Draw(graphicsprefs.stereoseparation);
			RViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
		}
	}
	RSet2D();
	static int curframe;
	curframe++;
	static DWORD dlasttime;
	if(curtime>dlasttime+1000)
	{
		currecvrate=datarecvthissec;
		datarecvthissec=0;
		cursendrate=datasendthissec;
		datasendthissec=0;
		dlasttime=curtime;
	}
	facesthissec=totalfaces;
	totalfaces=0;
		
	if(curtime>consolescrolltime)
	{
		if(consolepartvisible>0)
			consolepartvisible--;
		consolescrolltime=curtime+500;
	}
	static int64_t lasttime;
	int64_t tmptime=HKPerfCounter();
	if(tmptime>lasttime) framefps=HKPerfFrequency()/(tmptime-lasttime);   //never a division by 0
	curfps=(curfps*5+framefps)/6;
	lasttime=tmptime;
	char tmptext[40];
	sprintf(tmptext,"%d",(int)curfps);
	if(!consolevisible)
	{
		if(fxprefs.netstats) UIText(-.97f,.88f,tmptext);
		if(menu) UITextLines(-.6f,.5f,menutext);
	}
	sprintf(tmptext,"%d",(int)(curtime&999));
	int i;
	if(!consolevisible) DrawConsoleNotify();   //src/ui/console.cpp
	DrawChat();   //src/ui/chat.cpp
	if(GetLocalPlayer()->fzoom>.5f&&GetLocalPlayer()->playstate>=10)
	{
		char zoomtext[32];
		snprintf(zoomtext,sizeof(zoomtext),"Zoom x%.1f",powf(1.1f,GetLocalPlayer()->fzoom));
		RColor3f(0,0,0);   //shadow: readable on snow too
		UIText(.555f,-.905f,zoomtext,.06f);
		RColor3f(1,1,.6f);
		UIText(.55f,-.9f,zoomtext,.06f);
		RColor3f(1,1,1);
	}
	DrawVoiceHUD();
	
	//debug aid of the original game, off unless Options asks for it
	if(fxprefs.netstats)
	{
		sprintf(tmptext,"%d",(int)cursendrate);
		UIText(.5f,.88f,tmptext);
		sprintf(tmptext,"%d",(int)currecvrate);
		UIText(.7f,.88f,tmptext);
		char faceinfo[100];
		
		if(datasendthisframe)
		{
			sendbright=1;
		}
		sendbright/=1.3;
		RDisable(RCAP_TEXTURE);
		RColor4f(1,0,0,sendbright);
		RBegin(RP_QUADS);
		RVertex2f(.5,.75);
		RVertex2f(.6,.75);
		RVertex2f(.6,.82);
		RVertex2f(.5,.82);
		
		REnd();
		REnable(RCAP_TEXTURE);
		RColor3f(1,1,1);
		if(datarecvthisframe)
		{
			recvbright=1;
		}

		recvbright/=1.3;
		RDisable(RCAP_TEXTURE);
		RColor4f(0,0,1,recvbright);
		RBegin(RP_QUADS);
		RVertex2f(.7,.75);
		RVertex2f(.8,.75);
		RVertex2f(.8,.82);
		RVertex2f(.7,.82);
		
		REnd();
		REnable(RCAP_TEXTURE);
		RColor3f(1,1,1);
	}
	datasendthisframe=0;
	datarecvthisframe=0;

	sprintf(tmptext,"%d",(int)(GetLocalNum()));
	UIText(.3f,.88f,tmptext);
	sprintf(tmptext,"%d",(int)(GetLocalPlayer()->ammoreserves[GetLocalPlayer()->weapon]));
	//DrawText(.3,-.95,tmptext);
	float offset=-1.2;
	sprintf(tmptext,"%d",(int)(GetLocalPlayer()->ammoingun[GetLocalPlayer()->weapon]));
	UIText(.37f+offset,-.94f,tmptext,.065f);
	sprintf(tmptext,"%d",(int)(GetLocalPlayer()->life));
	UIText(.57f+offset,-.94f,tmptext,.065f);
	int flags[3];
	flags[0]=flags[1]=flags[2]=0;
	for(i=0;i<MAXSERVEROBJS;i++)
	{
		if(sobject[i].type==1)
		{
			flags[sobject[i].team]++;
		}
	}
	/*char flaglist[500];
	sprintf(flaglist,"White: %i  Red: %i  Blue: %i",flags[0],flags[1],flags[2]);
	DrawText(-.2,-.95,flaglist);*/
	/*char money[500];
	if(GetLocalPlayer()->seemoney)
	{
		sprintf(money,"Money: %i",GetLocalPlayer()->money);
		DrawText(-.4,0,money);
	}*/

	UITextLines(-.4f,-.75f,curobjective);
	UITextLines(-.4f,-.95f,curscore);
	
	TexBind(TEX_AMMOICON);
	float bf=(float)GetLocalPlayer()->ammoingun[GetLocalPlayer()->weapon]/GetAmmoHold(GetLocalPlayer()->weapon);
	RColor4f(bf/2+.5,bf/2+.5,bf/2+.5,bf/2+.5);
	RBegin(RP_QUADS);
	RTexCoord2f(0,1);RVertex2f(.35+offset,-.95);
	RTexCoord2f(0,0);RVertex2f(.35+offset,-.85);
	RTexCoord2f(1,0);RVertex2f(.28+offset,-.85);
	RTexCoord2f(1,1);RVertex2f(.28+offset,-.95);
	REnd();
	
	TexBind(TEX_HEALTHICON);
	bf=(float)GetLocalPlayer()->life/100;
	RColor4f(bf/2+.5,bf/2+.5,bf/2+.5,bf/2+.5);
	RBegin(RP_QUADS);
	RTexCoord2f(0,1);RVertex2f(.55+offset,-.95);
	RTexCoord2f(0,0);RVertex2f(.55+offset,-.85);
	RTexCoord2f(1,0);RVertex2f(.48+offset,-.85);
	RTexCoord2f(1,1);RVertex2f(.48+offset,-.95);
	REnd();

	for(int curcredit=0;curcredit<10;curcredit++)
	{
		int credits=GetLocalPlayer()->credits[curcredit];
		if(credits)
		{
			TexBind(TEX_CREDIT0+curcredit);
			float yoffset=(float)curcredit*.15-.25;
			RColor4f(1,1,1,1);
			RBegin(RP_QUADS);
			RTexCoord2f(1,1);RVertex2f(.35+offset,yoffset-.10);
			RTexCoord2f(1,0);RVertex2f(.35+offset,yoffset);
			RTexCoord2f(0,0);RVertex2f(.28+offset,yoffset);
			RTexCoord2f(0,1);RVertex2f(.28+offset,yoffset-.10);
			REnd();
			if(credits>1)
			{
				char tmptxt[50];
				sprintf(tmptxt,"%d",(int)credits);
				UIText(.36f+offset,yoffset-.09f,tmptxt,.06f);
			}
		}
	}


	RDisable(RCAP_TEXTURE);
	if(sv.radarenable)
	{
		RColor4f(0,0,0,.4);
		float radarcenterx=-.77,radarcentery=-.60,radarwidth=.15,radarheight=.2;
		RBegin(RP_QUADS);
		RVertex2f(radarcenterx-radarwidth,radarcentery-radarheight);
		RVertex2f(radarcenterx+radarwidth,radarcentery-radarheight);
		RVertex2f(radarcenterx+radarwidth,radarcentery+radarheight);
		RVertex2f(radarcenterx-radarwidth,radarcentery+radarheight);
		REnd();
		DrawRadarBuildZones(radarcenterx,radarcentery,radarwidth,radarheight);
		RPointSize(2*HKPixelScale());
		REnable(RCAP_BLEND);
		RBlendFunc(RBLEND_ALPHA);
		RBegin(RP_POINTS);
		Vector mypos=GetLocalPlayer()->pos;
		mypos.y=0;
		Vector theypos;

		float radarscale=(float)1/250;
		for(i=0;i<numplayers;i++)
		{
			if(GetPlayer(i)->playstate==10)
			{
				theypos=GetPlayer(i)->pos;
				theypos.y=0;
				theypos-=mypos;
				theypos=theypos.x*GetLocalPlayer()->fwd+theypos.z*CrossProduct(Vector(0,1,0),GetLocalPlayer()->fwd);
				theypos*=radarscale;
				float bright=1;
				float dist=Magnitude(theypos);
				if(dist<.3) bright-=(.3-dist)*16;
				if(GetPlayer(i)==GetLocalPlayer()) bright=1;
				RColor4f(1,0,0,bright);
				if(fabs(theypos.x)<1&&fabs(theypos.z)<1)
				RVertex2f(radarcenterx-theypos.z*radarwidth,radarcentery+theypos.x*radarheight);
			}
		}
		for(i=0;i<MAXSERVEROBJS;i++)
		{
			if(sobject[i].type)
			{
				theypos=sobject[i].pos;
				theypos.y=0;
				theypos-=mypos;
				theypos=theypos.x*GetLocalPlayer()->fwd+theypos.z*CrossProduct(Vector(0,1,0),GetLocalPlayer()->fwd);
				theypos*=radarscale;
				float bright=1;
				float dist=Magnitude(theypos);
				if(dist<.3) bright-=(.3-dist)*16;
				if(GetPlayer(i)==GetLocalPlayer()) bright=1;
				RColor4f(1,0,0,bright);
				if(fabs(theypos.x)<1&&fabs(theypos.z)<1)
				RVertex2f(radarcenterx-theypos.z*radarwidth,radarcentery+theypos.x*radarheight);
			}
		}
		REnd();
	}
	DrawGunSelection();
	
	if(sv.thrustrechargerate>0)
	{
		RBegin(RP_QUADS);
		RColor4f(1,0,0,.3+GetLocalPlayer()->thrustfuel*.4);
		RVertex2f(-.2,.95);
		RVertex2f(-.2,.9);
		RColor4f(1,0,0,.2+GetLocalPlayer()->thrustfuel*.4);
		RVertex2f(-.2+.4*GetLocalPlayer()->thrustfuel,.9);
		RVertex2f(-.2+.4*GetLocalPlayer()->thrustfuel,.95);
		
		REnd();
	}
	//over the rest of the HUD (radar, console lines), under the console only
	if(GetLocalPlayer()->scores||rgd.gamestate==2)
		DrawScoreboard();
	DrawBuildMenu();
	DrawSpawnScreen();
	if(consolevisible) DrawConsolePanel(0);
	ScreenShot();	
	UIFrameEnd();   //the pad's focus (build menu, spawn screen), not in screenshots
}


//draws everything 3d


void Draw(float offset)
{
	Player *cur=GetLocalPlayer();
	
	
	static int frame;

	frame++;
	
	int viewport[4];
	RGetViewport(viewport);
	RBlendFunc(RBLEND_SCALE);
	REnable(RCAP_BLEND);
	float fogend=400;
	float fogbegin=GetSkySettings()->fogbegin;
	float color[4];
	color[0]=GetSkySettings()->fogr;
	color[1]=GetSkySettings()->fogg;
	color[2]=GetSkySettings()->fogb;
	color[3]=0;
	
	if(GetLocalPlayer()->pos.y<WATERHEIGHT) 
	{
		fogend=10;
		fogbegin=.1;
		color[0]=.15;
		color[1]=.15;
		color[2]=.2;
	}
	RFogRange(fogbegin*fogend,fogend);   //linear fog (RInit)
	RFogColor(color);
	
	RClearColor(color[0]*1.0115,color[1]*1.0115,color[2]*1.0110,1);
	
	//the zoom narrows the field of view. Redraw multiplied it into the projection
	//before calling Draw, which then reset it: the camera never zoomed, even in
	//the original game.
	float zoom=powf(1.1f,cur->fzoom);
	Mat4 view=Mat4Mul(Mat4Identity(),Mat4Rotate(GetLocalPlayer()->roll*2,0,0,1));
	RSetProjection(Mat4Mul(Mat4Scale(zoom,zoom,1),Mat4Perspective(70,1.2,.4,1700)));
	camup=GetRight(VectMult(cur->tilt,cur->fwd,Vector(0,1,0),CrossProduct(cur->dir,Vector(0,1,0))),Vector(0,1,0));
	RSetModelView(Mat4LookAt(view,cur->pos.x,cur->pos.y,cur->pos.z,cur->pos.x+cur->dir.x,cur->pos.y+cur->dir.y,cur->pos.z+cur->dir.z,camup.x,camup.y,camup.z));
	
	REnable(RCAP_FOG);
	RFogRange(fogbegin*400,400);

	DrawCloudBox(cur->pos);
	if(GetSkySettings()->lightning&&curtime>nextlightning)
	{
		float c=.5+RND*.5;
		RPushAttrib(RATTR_ENABLE|RATTR_FOG|RATTR_COLOR|RATTR_POLYGON);
		Player *cur=GetLocalPlayer();
		RDisable(RCAP_FOG);
		RDisable(RCAP_DEPTHTEST);
		RBlendFunc(RBLEND_ADD);
		TexBind(TEX_SUNBACK);
		RBegin(RP_TRISTRIP);
		float color=RND*.5+.1;
		RColor4f(1,1,1,color);
		RTexCoord2f(0,0);

		RVertex3f(lightningpos[0].x-200,lightningpos[0].y,lightningpos[0].z+200);
		RTexCoord2f(1,0);
		RVertex3f(lightningpos[0].x-200,lightningpos[0].y,lightningpos[0].z-200);
		RTexCoord2f(0,1);
		RVertex3f(lightningpos[0].x+200,lightningpos[0].y,lightningpos[0].z+200);
		RTexCoord2f(1,1);
		RVertex3f(lightningpos[0].x+200,lightningpos[0].y,lightningpos[0].z-200);
		REnd();
		RDisable(RCAP_TEXTURE);
		RLineWidth(3*HKPixelScale());
		RBlendFunc(RBLEND_OPAQUE);
		RBegin(RP_LINES);

		RColor3f(.8,.8,.8);
		for(int i=0;i<numlightning;i++)
		{
			RVertex3fv((float *)&lightningpos[i]);
			RVertex3fv((float *)&lightningend[i]);
		}
		REnd();
		RPopAttrib();
		if(RND*500<2)
		{
			PlaySound(500,0);
		}
		if(RND*7<2)
		{
			nextlightning=curtime+RND*1000;
			numlightning=1;
			lightningpos[0]=Normalize(Vector(RND*500-250,GetLocalPlayer()->pos.y+GetSkySettings()->cloudheight,RND*500-250))*150+cur->pos;
			lightningend[0]=lightningpos[0]+Vector(RND*50-25,RND*-20-20,RND*50-25)/2;
		}
		else
		{
			for(int i=0;i<15;i++)
			if(RND*5<4)
			{
				//bolts fork until the next strike: stay inside lightningpos/lightningend[500]
				if(numlightning>=(int)(sizeof(lightningpos)/sizeof(lightningpos[0]))) break;
				int forkpos=RND*2+(numlightning-2);
				if(forkpos>numlightning-1) forkpos=numlightning-1;
				if(forkpos<0) forkpos=0;
				lightningpos[numlightning]=lightningend[forkpos];
				lightningend[numlightning]=lightningpos[numlightning]+Vector(RND*30-15,RND*-20-20,RND*30-15)/2;//+lightningend[0]-lightningpos[0];
				numlightning++;
			}
			nextlightning=curtime+RND*200;
		}
		
	}
	RBlendFunc(RBLEND_ALPHA);

	RFogRange(fogbegin*fogend,fogend);
	REnable(RCAP_FOG);
	REnable(RCAP_CULL);
	REnable(RCAP_DEPTHTEST);
	
	RClear(0,1);
	
	RSetProjection(Mat4Mul(Mat4Scale(zoom,zoom,1),Mat4Perspective(70,1.2,.15,fogend)));   //same zoom as the sky

	eyepos=cur->pos+offset*Normalize(CrossProduct(cur->dir,Vector(0,1,0)));

	RSetModelView(Mat4LookAt(view,eyepos.x,eyepos.y,eyepos.z,eyepos.x+cur->dir.x,eyepos.y+cur->dir.y,eyepos.z+cur->dir.z,camup.x,camup.y,camup.z));
	
	
	Vector drawpos=eyepos+cur->dir*(17*2-5);
	
	RDisable(RCAP_BLEND);

	DrawBlocks(drawpos.x,drawpos.y,drawpos.z,48);
	
	float dxpos,dypos;
	float odxpos=cur->pos.x-(int)cur->pos.x%8,odypos=cur->pos.z-(int)cur->pos.z%8;   //draw the land centered on a number divisble by 8
	int size,osize;
	dxpos=cur->pos.x+cur->dir.x*3;
	dypos=cur->pos.z+cur->dir.z*3;
	//DrawMip(dxpos,dypos,dxpos,dypos,0,0,0);
	odxpos=dxpos;odypos=dypos;
	dxpos=cur->pos.x+cur->dir.x*0;
	dypos=cur->pos.z+cur->dir.z*0;
	int unisize=20;
	DrawMip(dxpos,dypos,dxpos,dypos,0,unisize,1);
	odxpos=dxpos;odypos=dypos;
	dxpos=cur->pos.x+cur->dir.x*10;
	dypos=cur->pos.z+cur->dir.z*10;
	DrawMip(dxpos,dypos,odxpos,odypos,unisize,unisize,2);
	odxpos=dxpos;odypos=dypos;
	dxpos=cur->pos.x+cur->dir.x*27;
	dypos=cur->pos.z+cur->dir.z*27;
	DrawMip(dxpos,dypos,odxpos,odypos,unisize,unisize,3);
	DrawMip(dxpos,dypos,odxpos,odypos,unisize,unisize,4);
	//DrawMip(dxpos,dypos,odxpos,odypos,unisize,unisize,5);
	REnable(RCAP_BLEND);
	osize=0;
	
	int i;
	for(i=0;i<GetNumPlayers();i++)  //draws all the players
	{
		Player *cur=GetPlayer(i);
		if(cur->playstate==10)
		{
			//without the weapon pass, the gun is part of the scene, as it was
			if(cur==GetLocalPlayer()&&!fxprefs.viewmodel) DrawLocalGun(cur);
			if(GetLocalPlayer()!=cur)
			if(MakeClose(&cur->pos))
			{
				if(cur->hanggliding)
				{
					Vector fwd=GetRight(cur->dir,Vector(0,-1,0));
					Vector up=GetRight(fwd,cur->fwd);
					fwd-=CrossProduct(fwd,up)*(cur->roll/10);
					fwd=Normalize(fwd);
					gun[cur->weapon].playermodel.Draw(cur->pos-up*.5,fwd,up);
					hangglider.Draw(cur->pos-up*.5,fwd,up);
				}
				else
				{
					Vector fwd=Normalize(cur->fwd+cur->inaccuracy);
					//gun[cur->weapon].playermodel.Draw(cur->pos-Vector(0,1.5,0),fwd,GetRight(fwd,Vector(0,1,0)));
					int team=cur->team-1;
					if(team<0) team=0;
					torso[team][cur->weapon].Draw(cur->pos-Vector(0,1.5,0),fwd,GetRight(fwd,Vector(0,1,0)));
					leg[cur->legframe].Draw(cur->pos-Vector(0,1.5,0),fwd,GetRight(fwd,Vector(0,1,0)));
				}
				if(cur->team==0)RColor3f(1,1,1);
				if(cur->team==1)RColor3f(1,0,0);
				if(cur->team==2)RColor3f(0,0,1);
				DrawText3D(GetLocalPlayer()->dir*-1,cur->pos+Vector(0,.1,0),3,cur->name);
			}
		}
	}
	for(i=0;i<MAXOBJS;i++)
	{
		if(object[i].type)
		{
			object[i].object->Draw(object[i].pos,object[i].dir,GetRight(object[i].dir,Vector(0,1,0)));
		}
	}
	for(i=0;i<MAXSERVEROBJS;i++)
	{
		if(sobject[i].type)
		{
			if(sobject[i].type==1) sobject[i].dir=Normalize(CrossProduct(sobject[i].dir,Vector(0,1,0))*.01+sobject[i].dir);
			if(sobject[i].attached!=(-1))
			{
				sobject[i].pos=GetPlayer(sobject[i].attached)->pos+Vector(0,.3,0);
				sobject[i].dir=GetPlayer(sobject[i].attached)->fwd;
			}
			if(MakeClose(&sobject[i].pos))
			{
				if(servercobs[sobject[i].type][sobject[i].team].loaded)
					servercobs[sobject[i].type][sobject[i].team].Draw(sobject[i].pos,sobject[i].dir,GetRight(sobject[i].dir,Vector(0,1,0)));
				else
					servercobs[sobject[i].type][0].Draw(sobject[i].pos,sobject[i].dir,GetRight(sobject[i].dir,Vector(0,1,0)));			
				RColor3f(1,1,1);
				char *teamtext;

				if(sobject[i].type==1)
				{
					if(sobject[i].team==0){teamtext="White";RColor3f(1,1,1);}
					if(sobject[i].team==1){teamtext="Red";RColor3f(1,0,0);}
					if(sobject[i].team==2){teamtext="Blue";RColor3f(0,0,1);}
					DrawText3D(sobject[i].dir,sobject[i].pos+sobject[i].dir*.2+Vector(0,.5,0),1,teamtext);
					DrawText3D(sobject[i].dir*-1,sobject[i].pos+sobject[i].dir*-.2+Vector(0,.5,0),1,teamtext);
				}
			}
		}
	}
	
	double cposx,cposy,cposz;
	double sposx,sposy,sposz;
	cposx=cur->pos.x;
	cposy=cur->pos.y+50*GetSkySettings()->sunheight;  //find the position of the sun
	cposz=cur->pos.z+50;
	Mat4Project(cposx,cposy,cposz,RModelView(),RProjection(),viewport,&sposx,&sposy,&sposz);
				//find the screen coordinates of the sun


	
	REnable(RCAP_DEPTHTEST);
	int numscenery=GetNumSceneries();
	drawpos=cur->pos+cur->dir*20;
	for(i=0;i<numscenery;i++)
	{
		Scenery *curscenery=GetScenery(i);
		MakeClose(&curscenery->pos);
		if(curscenery->pos.x>drawpos.x-20)
		if(curscenery->pos.x<drawpos.x+20)
		if(curscenery->pos.z>drawpos.z-20)
		if(curscenery->pos.z<drawpos.z+20)
		scenery[curscenery->type].Draw(curscenery->pos,curscenery->side,curscenery->top);
	}
	
	//reads the depth of where the sun is going to be, if there is nothing there, depth=1.
	//Off the window nothing is read: the old code then tested an uninitialized local,
	//in practice what an earlier frame left there; static keeps the last frame's.
	static float depth=1;
	if(fabs(sposx)<1e6&&fabs(sposy)<1e6) RReadDepth((int)sposx,(int)sposy,&depth);
	
	RPushAttrib(RATTR_ENABLE);
	RDisable(RCAP_TEXTURE);
	RDisable(RCAP_CULL);
	RBlendFunc(RBLEND_ADD);
	RBegin(RP_TRIANGLES);
	
	for(i=0;i<MAXPARTICLES;i+=1)
	if(particle[i].type)
	if(MakeClose(&particle[i].pos))  //draws particles if they're close enough
	{

		if(curtime>particle[i].timeout)
			particle[i].type=0;
		particle[i].pos+=particle[i].vel;
		if(particle[i].type==1)
		{
			RColor4f(.5,.5,.9,.2);
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(.05,-.5,0));
			Vertex(particle[i].pos+Vector(-.05,-.5,0));
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(0,-.5,-.05));
			Vertex(particle[i].pos+Vector(0,-.5,.05));
		}
		if(particle[i].type==2)
		{

			RColor4f(.9,.9,.9,.3);
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(.1,-.1,0));
			Vertex(particle[i].pos+Vector(-.1,-.1,0));
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(0,-.1,-.1));
			Vertex(particle[i].pos+Vector(0,-.1,.1));
		}
		if(particle[i].type==3)
		{
			if(particle[i].pos.y<*GetRepeat(particle[i].pos.x,particle[i].pos.z)+.2||particle[i].pos.y<WATERHEIGHT||
				GetBlock(particle[i].pos.x/BLOCKSIZE,particle[i].pos.y/BLOCKSIZE,particle[i].pos.z/BLOCKSIZE)->type)  
			{
				particle[i].vel.y*=-.4;
			}
			int timeleft=particle[i].timeout-curtime;
			if(timeleft<1000) RColor4f(.9,.6,.1,.1);
			else if(timeleft<1500) RColor4f(.9,.6,.6,.3);
			else if(timeleft<2000) RColor4f(.9,.4,.1,.6);
			else if(timeleft<2500) RColor4f(.9,.3,.1,.8);
			else RColor4f(.9,.0,.0,.6);
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(.02,-.2,0));
			Vertex(particle[i].pos+Vector(-.02,-.2,0));
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(0,-.2,-.02));
			Vertex(particle[i].pos+Vector(0,-.2,.02));
		}
		if(particle[i].type==4)
		{
			int timeleft=particle[i].timeout-curtime;
			if(timeleft<200) RColor4f(.9,.6,.1,.1);
			else if(timeleft<400) RColor4f(.9,.6,.6,.3);
			else if(timeleft<600) RColor4f(.9,.4,.1,.6);
			else if(timeleft<850) RColor4f(.9,.3,.1,.8);
			else RColor4f(.9,.0,.0,.6);
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(.1,-.1,0)+particle[i].vel*3);
			Vertex(particle[i].pos+Vector(-.1,-.1,0)+particle[i].vel*3);
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(0,-.1,-.1)+particle[i].vel*3);
			Vertex(particle[i].pos+Vector(0,-.1,.1)+particle[i].vel*3);
		}
		if(particle[i].type==5)
		{
			REnd();
			REnable(RCAP_TEXTURE);
			TexBind(TEX_SUN);
			RDisable(RCAP_FOG);
			RBlendFunc(RBLEND_ADD);
			RBegin(RP_TRISTRIP);
			
			int timeleft=particle[i].timeout-curtime;
			RColor4f(.8,.8,.8,(float)timeleft/200);//(float)timeleft/200);
			Vector fwd=GetLocalPlayer()->pos-particle[i].pos;
			Vector up=GetRight(fwd,Vector(0,1,0));
			Vector right=Normalize(CrossProduct(fwd,up));
			Vector pos=(GetLocalPlayer()->pos+particle[i].pos*10)/11;
			float size=6;
			RTexCoord2f(0,0);
			Vertex(pos+up*size+right*size);
			RTexCoord2f(0,1);
			Vertex(pos+up*size-right*size);
			RTexCoord2f(1,0);
			Vertex(pos-up*size+right*size);
			RTexCoord2f(1,1);
			Vertex(pos-up*size-right*size);
			REnd();
			RDisable(RCAP_TEXTURE);
			REnable(RCAP_FOG);
			RBegin(RP_TRIANGLES);
		}
	}
	REnd();
	RBlendFunc(RBLEND_ALPHA);
	RBegin(RP_TRIANGLES);
	
	for(i=0;i<MAXPARTICLES;i+=1)
	if(particle[i].type)
	if(MakeClose(&particle[i].pos))
	{

		if(particle[i].type==6)
		{
			int timeleft=particle[i].timeout-curtime;
			RColor4f(.3,.15,.1,1);
			
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(.07,-.07,0)+particle[i].vel*3);
			Vertex(particle[i].pos+Vector(-.07,-.07,0)+particle[i].vel*3);
			RVertex3fv((float *)&particle[i].pos);
			Vertex(particle[i].pos+Vector(0,-.07,-.07)+particle[i].vel*3);
			Vertex(particle[i].pos+Vector(0,-.07,.07)+particle[i].vel*3);
		}
	}
	REnd();
	RPopAttrib();
	RCullFace(RFACE_FRONT);
	DrawEffects(0);
	DrawWater(50,drawpos.x,drawpos.z);
	DrawEffects(1);
	
	
	RDisable(RCAP_TEXTURE);
	
	
	REnable(RCAP_LINESMOOTH);
	RLineWidth(3*HKPixelScale());
	
	RBegin(RP_LINES);
	
	
	for(i=0;i<30;i++)
	{
		RColor4f(1,0,0,0);
		RVertex3fv((float *)&spark[i]);
		RColor4f(1,1,0,sparkbright[i]);
		Vertex(spark[i]+sparkvel[i]*sparklen[i]);
		spark[i]+=sparkvel[i];
		sparkbright[i]/=1.1;
		if(spark[i].y<WATERHEIGHT) sparkbright[i]/=3.0;
	}
	REnd();
	REnable(RCAP_TEXTURE);
	DrawGunExplosions(0);
	if(!fxprefs.viewmodel) DrawGunExplosions(1);
	
	
	
	if(GetSkySettings()->precipitation<=10&&GetSkySettings()->precipitation)
	{
		for(int n=0;n<GetSkySettings()->precipitation*3;n++)
		if(RND*10<2)
		{
			for(int i=0;i<MAXPARTICLES;i++)
			{
				if(particle[i].type==0)
				{
					particle[i].type=1;
					particle[i].pos=cur->pos+cur->fwd*2+Vector(RND*26-13,12,RND*26-13)*1;//+cur->vel*10;
					particle[i].vel=Vector(RND*.05+.325,-.9,RND*.05-.025);
					particle[i].timeout=curtime+2000;
					break;
				}
			}
		}
	}
	if(GetSkySettings()->precipitation==11)
	{
		for(int n=0;n<8;n++)
		if(RND*10<8)
		{
			for(int i=0;i<MAXPARTICLES;i++)
			{
				if(particle[i].type==0)
				{
					particle[i].type=2;
					particle[i].pos=cur->pos+Vector(RND*26-13,4,RND*26-13)*2+cur->vel*10;
					particle[i].vel=Vector(RND*.05-.025,-.2,RND*.05-.025);
					particle[i].timeout=curtime+5000;
					break;
				}
			}
		}
	}
	
	
	
	if(depth==1) 
	{
		sunflarebright+=.1;
	}
	else
	{
		sunflarebright-=.1;
	}
	if(sunflarebright>1) sunflarebright=1;
	if(sunflarebright<0) sunflarebright=0;
	//if(sunflarebright==1)
	{
		RPushAttrib(RATTR_ENABLE|RATTR_FOG|RATTR_COLOR|RATTR_POLYGON);
		DrawSun();
		RPopAttrib();
	
	}
	
	if(fxprefs.viewmodel&&cur->playstate==10) DrawViewModel(cur);

	//crosshair
	RCullFace(RFACE_BACK);
	
	RBlendFunc(RBLEND_ADD);
	TexBind(TEX_CROSSHAIR);
	RSetModelView(Mat4Identity());
	RSetProjection(Mat4Mul(Mat4Identity(),Mat4Translate(offset*-.5,0,0)));
	RBegin(RP_TRISTRIP);
	RColor4f(1,1,1,1);
	RTexCoord2f(0,0);
	
	RVertex2f(-.06,-.06);
	RTexCoord2f(0,1);
	RVertex2f(.06,-.06);
	RTexCoord2f(1,0);
	RVertex2f(-.06,.06);
	RTexCoord2f(1,1);
	RVertex2f(.06,.06);
	REnd();
	REnable(RCAP_FOG);
	RDisable(RCAP_DEPTHTEST);
	RBlendFunc(RBLEND_ALPHA);
	RColor4f(1,1,1,1);

	
}

//draw the splash screen

void RedrawSplash(int texnum,char *text,int drawlogo)
{
	texnum=TEX_SPLASH;   //always the splash, whatever the caller passes
	REnable(RCAP_TEXTURE);
	TexBind(texnum);
	RSetModelView(Mat4Identity());
	RDisable(RCAP_DEPTHTEST);
	REnable(RCAP_BLEND);
	RBlendFunc(RBLEND_OPAQUE);
	RBegin(RP_QUADS);
	RColor4f(1,1,1,0);
	RTexCoord2f(1,0); RVertex2f(1,1);
	RTexCoord2f(0,0); RVertex2f(-1,1);
	RTexCoord2f(0,.75); RVertex2f(-1,-1);
	RTexCoord2f(1,.75); RVertex2f(1,-1);
	REnd();
	RColor4f(1,1,1,1);
	RBlendFunc(RBLEND_ALPHA);

	if(text)
		UIText(-.97f,-.97f,text);
	
}


//just a wrapper

void RedrawSplash(int texnum,char *text)
{
	RedrawSplash(texnum,text,0);
}
