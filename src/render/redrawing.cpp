//functions to draw everything in the 3d world, and some 2d overlays


#include "gl.h"
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
#include "ui.h"
float cloudposx,cloudposy;
float cloudmistposx,cloudmistposy;	
#define PI (3.1415926)
#define LANDSCAPERENDER GL_TRIANGLE_STRIP
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
		if(clouds)
		nf=((float)*((BYTE*)&clouds[xs*1024+ys]))/256;
		l*=nf;
		if(l<.3)l=.3;
	}
	glColor4f(l,l,l,1);
}

//this is bad style, but it's the fastest way to do this

#define LV(x,y) \
{\
landfaces++;\
SetLandLight(x,y); \
glTexCoord2f(((float)x+xtexoffset)*TEXMULT,((float)y+ytexoffset)*TEXMULT);\
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
glVertex3f(x,curheight,y);

#define LANDVERTEX(x,y) LV((x),(y))			
#define TEXTURESWITCHRD(x,y,type,x1,y1,x2,y2)\
if(curtex!=landtex[(x&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE][(y&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE])\
{glEnd();TexBind((curtex=landtex[(x&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE][(y&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE])+TEX_GROUND0);glBegin(type);\
LANDVERTEX(x1,y1);LANDVERTEX(x2,y2);}\

#define TEXTURESWITCH(x,y,type)\
if(curtex!=landtex[(x&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE][(y&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE])\
{glEnd();TexBind((curtex=landtex[(x&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE][(y&LANDGRIDSIZE-1)/LANDTEXBLOCKSIZE])+TEX_GROUND0);glBegin(type);}\


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
	glCullFace(GL_BACK);
	glBegin(GL_TRIANGLES);
	y=maxy;
	x=minx;
	
	for(x=minx+res;x<=maxx-res;x+=res*2)
	{
		TEXTURESWITCH(x,y,GL_TRIANGLES);
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
	glEnd();
	glCullFace(GL_FRONT);
	glBegin(GL_TRIANGLES);
	for(x=minx+res;x<=maxx-res;x+=res*2)
	{
		TEXTURESWITCH(x,y,GL_TRIANGLES);
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
	glEnd();
	y=miny;
	x=minx-res;
	glCullFace(GL_BACK);
	glBegin(GL_TRIANGLES);
	for(y=miny+res;y<=maxy-res;y+=res*2)
	{
		TEXTURESWITCH(x,y,GL_TRIANGLES);
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
	glEnd();
	y=miny;
	x=maxx;
	glCullFace(GL_FRONT);
	glBegin(GL_TRIANGLES);
	for(y=miny+res;y<=maxy-res;y+=res*2)
	{
		TEXTURESWITCH(x,y,GL_TRIANGLES);
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
	TEXTURESWITCH(minx-res,miny-res,GL_TRIANGLES);
	LANDVERTEX(minx-res,miny-res);
	LANDVERTEX(minx-res+res*2,miny-res);
	LANDVERTEX(minx-res,miny-res+res*2);
	LANDVERTEX(minx-res,miny-res+res*2);
	LANDVERTEX(minx-res+res*2,miny-res);
	LANDVERTEX(minx-res+res*2,miny-res+res*2);
	
	TEXTURESWITCH(maxx,miny-res,GL_TRIANGLES);
	LANDVERTEX(maxx,miny-res);
	LANDVERTEX(maxx+res*2,miny-res);
	LANDVERTEX(maxx,miny-res+res*2);
	LANDVERTEX(maxx,miny-res+res*2);
	LANDVERTEX(maxx+res*2,miny-res);
	LANDVERTEX(maxx+res*2,miny-res+res*2);
	
	TEXTURESWITCH(minx-res,maxy,GL_TRIANGLES);
	LANDVERTEX(minx-res,maxy);
	LANDVERTEX(minx-res+res*2,maxy);
	LANDVERTEX(minx-res,maxy+res*2);
	LANDVERTEX(minx-res,maxy+res*2);
	LANDVERTEX(minx-res+res*2,maxy);
	LANDVERTEX(minx-res+res*2,maxy+res*2);
	
	TEXTURESWITCH(maxx,maxy,GL_TRIANGLES);
	LANDVERTEX(maxx,maxy);
	LANDVERTEX(maxx+res*2,maxy);
	LANDVERTEX(maxx,maxy+res*2);
	LANDVERTEX(maxx,maxy+res*2);
	LANDVERTEX(maxx+res*2,maxy);
	LANDVERTEX(maxx+res*2,maxy+res*2);
	
	glEnd();

	
	glCullFace(GL_BACK);
	
	for(y=inmaxy;y<maxy;y+=res)
	{
		
		glBegin(LANDSCAPERENDER);		
		for(x=minx+res;x<=maxx;x+=res)
		{
			LANDVERTEX(x,y);
			LANDVERTEX(x,y+res);
			TEXTURESWITCHRD(x,y,LANDSCAPERENDER,x,y,x,y+res);
			
			
		}
		glEnd();
		
	}
	for(y=miny+res;y<inminy;y+=res)
	{
		glBegin(LANDSCAPERENDER);		
		for(x=minx+res;x<=maxx;x+=res)
		{
			LANDVERTEX(x,y);
			LANDVERTEX(x,y+res);
			TEXTURESWITCHRD(x,y,LANDSCAPERENDER,x,y,x,y+res);
			
		}
		glEnd();
	}
	glCullFace(GL_FRONT);
	for(x=minx+res;x<inminx;x+=res)
	{
		glBegin(LANDSCAPERENDER);		
		for(y=inminy;y<=inmaxy;y+=res)
		{
			
			LANDVERTEX(x,y);
			LANDVERTEX(x+res,y);
			TEXTURESWITCHRD(x,y,LANDSCAPERENDER,x,y,x+res,y);
			
		}
		glEnd();
	}
	for(x=inmaxx;x<maxx;x+=res)
	{
		glBegin(LANDSCAPERENDER);		
		for(y=inminy;y<=inmaxy;y+=res)
		{
			
			LANDVERTEX(x,y);
			LANDVERTEX(x+res,y);
			TEXTURESWITCHRD(x,y,LANDSCAPERENDER,x,y,x+res,y);
			
		}
		glEnd();
	}
}	


//draw a few layers of clouds (not really a box)

void DrawCloudBox(Vector pos)
{
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	
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
	glCullFace(GL_BACK);
	TexBind(TEX_CLOUDS);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		
	glColor4f(1,1,1,.5);
	float fy;
	float fx;
	float of=(float)1/repeats;
	
	int x;
	for(x=-repeats;x<repeats;x++)
	{
		glBegin(GL_TRIANGLE_STRIP);
		fx=(float)x/repeats;
		for(int y=-repeats;y<repeats;y++)
		{
			fy=(float)y/repeats;
			glTexCoord2f(texrepeat*fx+cloudposx,texrepeat*fy+cloudposy);
			glVertex3f(pos.x+boxsize*fx,highest,pos.z+boxsize*fy);
			glTexCoord2f(texrepeat*(fx+of)+cloudposx,texrepeat*fy+cloudposy);
			glVertex3f(pos.x+boxsize*(fx+of),highest,pos.z+boxsize*fy);
		}
		glEnd();
	}
	if(GetSkySettings()->usemist)
	{
		highest-=60;
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		TexBind(TEX_CLOUDMIST);
		glColor4f(1,1,1,GetSkySettings()->mistopacity);
		for(x=-repeats;x<repeats;x++)
		{
			glBegin(GL_TRIANGLE_STRIP);
			fx=(float)x/repeats;
			for(int y=-repeats;y<repeats;y++)
			{
				fy=(float)y/repeats;
				glTexCoord2f(texrepeat*fx+cloudmistposx,texrepeat*fy+cloudmistposy);
				glVertex3f(pos.x+boxsize*fx,highest,pos.z+boxsize*fy);
				glTexCoord2f(texrepeat*(fx+of)+cloudmistposx,texrepeat*fy+cloudmistposy);
				glVertex3f(pos.x+boxsize*(fx+of),highest,pos.z+boxsize*fy);
			}
			glEnd();
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
		glColor3f(XNC,XNC,XNC);
		glTexCoord2f(0,0);				glVertex3f(x,y,z);
		glTexCoord2f(1,0);		glVertex3f(x,y,z+BLOCKSIZE);
		glTexCoord2f(1,1);glVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		glTexCoord2f(0,t1);		glVertex3f(x,y+BLOCKSIZE*t1,z);
		
		break;
	}
	case SIDEXP:
	{
		glColor3f(XPC,XPC,XPC);
		glTexCoord2f(1,0);				glVertex3f(x+BLOCKSIZE,y,z);
		glTexCoord2f(1,t4);		glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		glTexCoord2f(0,t3);glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
		glTexCoord2f(0,0);		glVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		break;
	}
	case SIDEYN:
	{
		glColor3f(YNC,YNC,YNC);
		glTexCoord2f(0,0);				glVertex3f(x,y,z);
		glTexCoord2f(1,0);		glVertex3f(x+BLOCKSIZE,y,z);
		glTexCoord2f(1,1);glVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		glTexCoord2f(0,1);		glVertex3f(x,y,z+BLOCKSIZE);
		
		break;
	}
	case SIDEYP:
	{
		switch(type)
		{
		case 1:
			glColor3f(YPC,YPC,YPC);
			break;
		case 2:
			glColor3f(TC2,TC2,TC2);
			break;
		case 3:
			glColor3f(TC3,TC3,TC3);
			break;
		case 4:
			glColor3f(TC4,TC4,TC4);
			break;
		case 5:
			glColor3f(TC5,TC5,TC5);
			break;
		}
		
		glTexCoord2f(0,0);				glVertex3f(x,y+BLOCKSIZE*t1,z);
		glTexCoord2f(0,1);		glVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		glTexCoord2f(1,1);glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
		glTexCoord2f(1,0);		glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		
		break;
	}
	case SIDEZN:
	{
		glColor3f(ZNC,ZNC,ZNC);
		glTexCoord2f(1,0);		glVertex3f(x,y,z);
		glTexCoord2f(1,t1);glVertex3f(x,y+BLOCKSIZE*t1,z);
		glTexCoord2f(0,t4);		glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		glTexCoord2f(0,0);				glVertex3f(x+BLOCKSIZE,y,z);
		
		break;
	}
	
	case SIDEZP:
	{
		glColor3f(ZPC,ZPC,ZPC);
		glTexCoord2f(0,0);				glVertex3f(x,y,z+BLOCKSIZE);
		glTexCoord2f(1,0);		glVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		glTexCoord2f(1,t3);glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
		glTexCoord2f(0,t2);		glVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		
		break;
	}	
	}
}

//the shade of a face of the build preview: grey, or red where it can't be built
static inline void BlockColor(float c,char red)
{
	if(red) glColor3f(.5f+c*.5f,c*.15f,c*.15f);
	else glColor3f(c,c,c);
}

//draws an individual block (the wireframe of the build preview)

void DrawBlock(int x,int y,int z,char ds,char type,int &px,int &py,int &pz,char red)
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
		glTexCoord2f(xtexoffset,ytexoffset);				glVertex3f(x,y,z);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x,y,z+BLOCKSIZE);
		glTexCoord2f(xtexoffset,BTEXMULT*t1+ytexoffset);		glVertex3f(x,y+BLOCKSIZE*t1,z);
		glTexCoord2f(xtexoffset,BTEXMULT*t1+ytexoffset);		glVertex3f(x,y+BLOCKSIZE*t1,z);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x,y,z+BLOCKSIZE);
		glTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT+ytexoffset);glVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
	}
	 if(x<px&&ds&SIDEXP)
	{
		BlockColor(XPC,red);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);				glVertex3f(x+BLOCKSIZE,y,z);
		glTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t4+ytexoffset);		glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		glTexCoord2f(xtexoffset,ytexoffset);		glVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		glTexCoord2f(xtexoffset,ytexoffset);		glVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		glTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t4+ytexoffset);		glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		glTexCoord2f(xtexoffset,BTEXMULT*t3+ytexoffset);glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
	}
	xtexoffset=1,ytexoffset=0;
	xtexoffset=xtexoffset/4+stdxoff;
	ytexoffset=ytexoffset/4+stdyoff;
	if(y>py&&ds&SIDEYN)
	{
		BlockColor(YNC,red);
		glTexCoord2f(xtexoffset,ytexoffset);				glVertex3f(x,y,z);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x+BLOCKSIZE,y,z);
		glTexCoord2f(xtexoffset,BTEXMULT+ytexoffset);		glVertex3f(x,y,z+BLOCKSIZE);
		glTexCoord2f(xtexoffset,BTEXMULT+ytexoffset);		glVertex3f(x,y,z+BLOCKSIZE);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x+BLOCKSIZE,y,z);
		glTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT+ytexoffset);glVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
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
		
		glTexCoord2f(xtexoffset,ytexoffset);				glVertex3f(x,y+BLOCKSIZE*t1,z);
		glTexCoord2f(xtexoffset,BTEXMULT+ytexoffset);		glVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
		glTexCoord2f(xtexoffset,BTEXMULT+ytexoffset);		glVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		glTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT+ytexoffset);glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
	}
	xtexoffset=0,ytexoffset=0;
	xtexoffset=xtexoffset/4+stdxoff;
	ytexoffset=ytexoffset/4+stdyoff;
	if(z>pz&&ds&SIDEZN)
	{
		BlockColor(ZNC,red);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x,y,z);
		glTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t1+ytexoffset);glVertex3f(x,y+BLOCKSIZE*t1,z);
		glTexCoord2f(xtexoffset,ytexoffset);				glVertex3f(x+BLOCKSIZE,y,z);
		glTexCoord2f(xtexoffset,ytexoffset);				glVertex3f(x+BLOCKSIZE,y,z);
		glTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t1+ytexoffset);glVertex3f(x,y+BLOCKSIZE*t1,z);
		glTexCoord2f(xtexoffset,BTEXMULT*t4+ytexoffset);		glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t4,z);
	}
	if(z<pz&&ds&SIDEZP)
	{
		BlockColor(ZPC,red);
		glTexCoord2f(xtexoffset,ytexoffset);				glVertex3f(x,y,z+BLOCKSIZE);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		glTexCoord2f(xtexoffset,BTEXMULT*t2+ytexoffset);		glVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		glTexCoord2f(xtexoffset,BTEXMULT*t2+ytexoffset);		glVertex3f(x,y+BLOCKSIZE*t2,z+BLOCKSIZE);
		glTexCoord2f(BTEXMULT+xtexoffset,ytexoffset);		glVertex3f(x+BLOCKSIZE,y,z+BLOCKSIZE);
		glTexCoord2f(BTEXMULT+xtexoffset,BTEXMULT*t3+ytexoffset);glVertex3f(x+BLOCKSIZE,y+BLOCKSIZE*t3,z+BLOCKSIZE);
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
	//glDisable(GL_CULL_FACE);
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
		glBegin(GL_QUADS);
		for(int i2=0;i2<numblockbuf[i];i2++)
		{
			DrawBlockSide(&blockbuf[i][i2]);
		}
		glEnd();
	}
	glPushAttrib(GL_ENABLE_BIT|GL_FOG_BIT|GL_COLOR_BUFFER_BIT|GL_POLYGON_BIT);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);	
	glBlendFunc(GL_ONE,GL_ONE);
	glBegin(GL_LINES);
	if(menu==BUILDING) PlaceStructure(0);
	glEnd();
	glPopAttrib();
	
}

//draws 2d text at x,y

float DrawText(float x,float y,const char *text)
{
	
	TexBind(TEX_FONT);
	glBegin(GL_QUADS);
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
		glTexCoord2f(xtp+toffset,ytp);
		glVertex2f(x,y+.07);
		glTexCoord2f(xtp+toffset,ytp+tysize);
		glVertex2f(x,y);
		glTexCoord2f(xtp+tsize+toffset,ytp+tysize);
		glVertex2f(x+(.040),y);
		glTexCoord2f(xtp+tsize+toffset,ytp);
		glVertex2f(x+(.040),y+.07);
		x+=.040;
		sp++;
	}
	
	glEnd();
	return 1;
}

//game mode 0: where each team may build (BUILDZONE around its water pump, see
//CanBuild), on the radar: centered on the local player, turned with its view, 250
//units to the edge; the circles are cut at the edge of the radar with the scissor
static void DrawRadarBuildZones(float cx,float cy,float w,float h)
{
	if(sv.gamemode!=0) return;
	const float scale=1.f/250;
	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT,vp);
	glEnable(GL_SCISSOR_TEST);
	glScissor(vp[0]+(GLint)((cx-w+1)/2*vp[2]),vp[1]+(GLint)((cy-h+1)/2*vp[3]),
		(GLsizei)(w*vp[2]),(GLsizei)(h*vp[3]));
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
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
		glColor4f(red,green,blue,.22f*a);
		glBegin(GL_TRIANGLE_FAN);
		glVertex2f(x,y);
		for(int k=0;k<=32;k++)
			glVertex2f(x+rx*cosf(k*PI/16),y+ry*sinf(k*PI/16));
		glEnd();
		glLineWidth(HKPixelScale());
		glColor4f(red,green,blue,.7f*a);
		glBegin(GL_LINE_LOOP);
		for(int k=0;k<32;k++)
			glVertex2f(x+rx*cosf(k*PI/16),y+ry*sinf(k*PI/16));
		glEnd();
	}
	glDisable(GL_SCISSOR_TEST);
}

//draw text in 3d space using the 2d DrawText function

void DrawText3D(Vector facing,Vector pos,float scale,char *text)
{
	glCullFace(GL_BACK);
	glBlendFunc(GL_COLOR,GL_ZERO);
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
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glMultMatrixf((float *)&mx);

	DrawText(-(float)strlen(text)*.040/2,0,text);
	glPopMatrix();
	glCullFace(GL_FRONT);
}

//draws the water where it needs to be drawn (the isdrawing variable is used to restart the triangle strip,
// since having a huge strip was causing problems on some graphics cards)

void DrawWater(int size,float cx,float cy)
{
	glDisable(GL_CULL_FACE);
	
	cx/=WATERBLOCKSIZE;
	cy/=WATERBLOCKSIZE;
	int isdrawing;
	int isvisible;
	static float wox;
	static float woy;
	woy+=.0009;
	wox+=.0003;
	if(wox>4) wox-=4;
	if(woy>4) woy-=4;
	glDisable(GL_CULL_FACE);
	int belowwater=GetLocalPlayer()->pos.y<WATERHEIGHT;
	TexBind(TEX_CLOUDS);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
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
				glBegin(GL_TRIANGLE_STRIP);
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
				glColor4f(light,light,light,.7);
				glTexCoord2f(reflectoffx,reflectoffy);
				glVertex3f(x*WATERBLOCKSIZE,WATERHEIGHT+heightoffy+heightoffx,y*WATERBLOCKSIZE);
				light=*GetWaterLight(x+1,y);
				light+=lightoff;
				if(belowwater) light=.3;
				glColor4f(light,light,light,.7);
				glTexCoord2f(reflectoffx1,reflectoffy);
				glVertex3f((x+1)*WATERBLOCKSIZE,WATERHEIGHT+heightoffy+heightoffx1,y*WATERBLOCKSIZE);
				ysincerestart++;
				if(ysincerestart>12) 
				{
					glEnd();
					glBegin(GL_TRIANGLE_STRIP);
					y--;
					ysincerestart=0;
				}
				
			}
			if(!isvisible&&isdrawing)
			{
				isdrawing=0;
				glEnd();
			}
		}
		if(isdrawing)
		{
			isdrawing=0;
			glEnd();
		}
	}
	TexBind(TEX_LILLYPAD);
	glColor4f(1,1,1,1);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); 
	glBegin(GL_QUADS);
	int underwater=GetLocalPlayer()->pos.y<WATERHEIGHT;
	float yoffset=.1;
	if(underwater) 
	{
		glColor4f(.6,.6,.6,1);
		yoffset=-.1;
	}
	for(int i=0;i<numlillypad;i++)
	if(MakeClose(&lillypad[i].pos))
	{
		float x=lillypad[i].pos.x;
		float z=lillypad[i].pos.z;
		float y=WATERHEIGHT+sin((float)(x/WATERBLOCKSIZE)*3.141592653/4+woy*3.141592653*8)/6+yoffset;
		float size=lillypad[i].size;
		glTexCoord2f(0,0);glVertex3f(x-size,y,z-size);
		glTexCoord2f(0,1);glVertex3f(x-size,y,z+size);
		glTexCoord2f(1,1);glVertex3f(x+size,y,z+size);
		glTexCoord2f(1,0);glVertex3f(x+size,y,z-size);
	}
	glEnd();
	glEnable(GL_CULL_FACE); 
	
}

//draws the sun and a flare around it

void DrawSun()
{
	glCullFace(GL_FRONT);
	Player *cur=GetLocalPlayer();
	glDisable(GL_FOG);
	//glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE );
	TexBind(TEX_SUNBACK);
	glDisable(GL_DEPTH_TEST);
	glBegin(GL_TRIANGLE_STRIP);
	glColor4f(1,1,1,GetSkySettings()->suneyelight*sunflarebright);
	glTexCoord2f(0,0);
	glVertex3f(cur->pos.x-50,cur->pos.y+60*GetSkySettings()->sunheight+50,cur->pos.z+60);
	glTexCoord2f(1,0);
	glVertex3f(cur->pos.x-50,cur->pos.y+60*GetSkySettings()->sunheight-50,cur->pos.z+60);
	glTexCoord2f(0,1);
	glVertex3f(cur->pos.x+50,cur->pos.y+60*GetSkySettings()->sunheight+50,cur->pos.z+60);
	glTexCoord2f(1,1);
	glVertex3f(cur->pos.x+50,cur->pos.y+60*GetSkySettings()->sunheight-50,cur->pos.z+60);
	glEnd();

	TexBind(TEX_SUN);
	glEnable(GL_DEPTH_TEST);
	glBegin(GL_TRIANGLE_STRIP);
	glColor4f(1,1,1,1);
	glTexCoord2f(0,0);
	float dist=160;
	float size=dist/60*2;
	glVertex3f(cur->pos.x-size,cur->pos.y+dist*GetSkySettings()->sunheight+size,cur->pos.z+dist);
	glTexCoord2f(1,0);
	glVertex3f(cur->pos.x-size,cur->pos.y+dist*GetSkySettings()->sunheight-size,cur->pos.z+dist);
	glTexCoord2f(0,1);
	glVertex3f(cur->pos.x+size,cur->pos.y+dist*GetSkySettings()->sunheight+size,cur->pos.z+dist);
	glTexCoord2f(1,1);
	glVertex3f(cur->pos.x+size,cur->pos.y+dist*GetSkySettings()->sunheight-size,cur->pos.z+dist);
	glEnd();
}

//draws the explosion coming out from the gun

void DrawGunExplosions()
{
	Vector pos,front,top,right;
	for(int i=0;i<GetNumPlayers();i++)
	{
		
		Player *pl=GetPlayer(i);
	
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
			
			glEnable(GL_DEPTH_TEST);
			glDisable(GL_FOG);
			glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA );
			TexBind(TEX_EXPLOSION0+pl->weapon*2);

			glBegin(GL_TRIANGLE_STRIP);
			glColor4f(1,1,1,(float)(10-timesincefire)/5);
			glTexCoord2f(1,1);
			glVertex3fv((float*)&epos[0]);
			glTexCoord2f(1,0);
			glVertex3fv((float*)&epos[1]);
			glTexCoord2f(0,1);
			glVertex3fv((float*)&epos[2]);
			glTexCoord2f(0,0);
			glVertex3fv((float*)&epos[3]);
			
			glEnd();
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
	glPushAttrib(GL_ENABLE_BIT|GL_FOG_BIT|GL_COLOR_BUFFER_BIT|GL_POLYGON_BIT);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glDisable(GL_CULL_FACE);
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
	glEnable(GL_TEXTURE_2D);
	TexBind(TEX_GUNNAMES);
	glBegin(GL_TRIANGLE_STRIP);
	
	
	for(int i=0;i<64;i++)
	{
		float y=sin((float)i/64*pi*2+rot)/2;
		float z=cos((float)i/64*pi*2+rot)/1.6;
		glColor4f(1,1,1,(-z/1.5+.4)*bright);
		z+=2;
		float offx=.2;
		float offy=-.4;
		glTexCoord2f(0,(float)i/64);
		glVertex2f(offx-.8/z,offy+y/z);
		glTexCoord2f(1,(float)i/64);
		glVertex2f(offx+.8/z,offy+y/z);
	}
	glEnd();
	glPopMatrix();
	glPopAttrib();
}

//draws the console alone on a black screen (loading, connecting); in game it is drawn
//over the world (DrawConsolePanel, src/ui/console.cpp)

void DrawConsole()
{
#ifndef HK_WIN32_NATIVE
	glClearColor(0,0,0,0);
	glClear(GL_COLOR_BUFFER_BIT);
	DrawConsolePanel(1);
	SwapBuffers(GetGlHDC());
#else
	glColor3f(1,1,1);
	glEnable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA,GL_ZERO );
	glEnable(GL_BLEND);
	glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
	glClearColor(0,0,0,0);
	glClear(GL_COLOR_BUFFER_BIT);
	for(int i=0;i<15;i++)
	{
		DrawText(- 1,-.7+i*.1,consoletext[i]);
	}
	DrawText(-1,-.8,consolecommand);
	DrawText(-1+(.040*strlen(consolecommand)),-.8,"_");
	//InvalidateRect(GetHWND(),0,0);
	SwapBuffers(GetGlHDC());
#endif
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
		glGetIntegerv(GL_VIEWPORT,vp);
		int planesize=(vp[2]-vp[0])*(vp[3]-vp[1]);
		int bufsize=planesize*3+sizeof(BMoPHdr);
		char *filebuf=new char[bufsize];
		/*glReadPixels(vp[0],vp[1],vp[2],vp[3],GL_RED,GL_UNSIGNED_BYTE,&filebuf[128]);
		glReadPixels(vp[0],vp[1],vp[2],vp[3],GL_GREEN,GL_UNSIGNED_BYTE,&filebuf[128+planesize]);
		glReadPixels(vp[0],vp[1],vp[2],vp[3],GL_BLUE,GL_UNSIGNED_BYTE,&filebuf[128+planesize*2]);*/
		glReadPixels(vp[0],vp[1],vp[2],vp[3],GL_BGR_EXT,GL_UNSIGNED_BYTE,&filebuf[sizeof(BMoPHdr)]);
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
		itoa(RND*1000,&filename[strlen(filename)],16);
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
	SwapBuffers(GetGlHDC());
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	
	Player *cur=GetLocalPlayer();
	float zoom=powf(1.1,cur->fzoom);
	int viewport[4];
	glGetIntegerv(GL_VIEWPORT,(int *)viewport);
	
	float yscale=(float)(viewport[2]-viewport[0])/(float)(viewport[3]-viewport[1]);
	float xscale=(float)(viewport[3]-viewport[1])/(float)(viewport[2]-viewport[0]);
	int pvp[4]={0,0,1,1};
	if (xscale>yscale)xscale=1; else yscale=1;
	
	float xyscalemx[4][4]=
	{
		{xscale*zoom,0,0,0},
		{0,yscale*zoom,0,0},
		{0,0,1,0},
		{0,0,0,1}
	};
	glMatrixMode(GL_PROJECTION); 
	glMultMatrixf((float *)xyscalemx);
	cloudposx+=GetSkySettings()->cloudvelx;
	cloudposy+=GetSkySettings()->cloudvely;
	cloudmistposx+=GetSkySettings()->cloudmistvelx;
	cloudmistposy+=GetSkySettings()->cloudmistvely;

	blockfaces=0;
	landfaces=0;
	
#ifdef HK_WIN32_NATIVE
	if(consolevisible)
	{
		DrawConsole();
		return;
	}
	else
#endif
	{
		glClear(GL_COLOR_BUFFER_BIT);
	
		if(graphicsprefs.stereoseparation==0)
			Draw(0);
		else
		{
			glViewport(viewport[0],viewport[1],viewport[2]/2,viewport[3]);	
			Draw(-graphicsprefs.stereoseparation);
			glViewport(viewport[2]/2,viewport[1],viewport[2]/2,viewport[3]);	
			Draw(graphicsprefs.stereoseparation);
			glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);	
		}
	}
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
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
	static LARGE_INTEGER lasttime;
	LARGE_INTEGER tmptime;
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&tmptime);
	framefps=freq.QuadPart/(tmptime.QuadPart-lasttime.QuadPart);
	curfps=(curfps*5+framefps)/6;
	lasttime=tmptime;
	char tmptext[40];
	itoa(curfps,tmptext,10);
	if(!consolevisible)
	{
		UIText(-.97f,.88f,tmptext);
		if(menu) UITextLines(-.6f,.5f,menutext);
	}
#ifdef HK_WIN32_NATIVE  // original score table, replaced by DrawScoreboard (src/ui/scoreboard.cpp)
	if(GetLocalPlayer()->scores||rgd.gamestate==2)
	{
		DrawText(-.8,.74,"Name");
		DrawText(-.1,.74,"Ping");
		DrawText(.1,.74,"Drop");
		DrawText(.3,.74,"Kills");
		DrawText(.5,.74,"Deaths");
		for(int i=0;i<numplayers;i++)
		{
			Player *cur=GetPlayer(i);
			if(cur==GetLocalPlayer())
			{
				glColor3f(1,1,1);
				DrawText(-.86,.60-i*.1,">");
			}
			//else
			{
				if(cur->team==0)glColor3f(1,1,1);
				if(cur->team==1)glColor3f(1,0,0);
				if(cur->team==2)glColor3f(0,0,1);
			}
			DrawText(-.8,.60-i*.1,cur->name);
			itoa(cur->ping,tmptext,10);
			DrawText(-.1,.60-i*.1,tmptext);
			itoa((cur->packetloss*100)/30000,tmptext,10);
			strcat(tmptext,"%");
			DrawText(.1,.60-i*.1,tmptext);
			itoa(cur->kills,tmptext,10);
			if(GetPlayer(i)->playstate>=10)DrawText(.3,.60-i*.1,tmptext);
			else
				if(i==0)
					DrawText(.3,.60-i*.1,"Server");
				else
					DrawText(.3,.60-i*.1,"Connecting");
			itoa(cur->deaths,tmptext,10);
			if(GetPlayer(i)->playstate>=10)DrawText(.5,.60-i*.1,tmptext);
		}
		glColor3f(1,1,1);
	}
#endif
	itoa(curtime&999,tmptext,10);
	int i;
#ifdef HK_WIN32_NATIVE
	for(i=0;i<consolepartvisible;i++)
	{
		DrawText(- 1,1-(consolepartvisible-i)*.1,consoletext[i]);
	}
#else
	if(!consolevisible) DrawConsoleNotify();   //src/ui/console.cpp
#endif
#ifdef HK_WIN32_NATIVE
	if(GetLocalPlayer()->talking)
	{
		DrawText(-.65,0,"Say:");
		DrawText(-.5,0,talktext);
	}
#else
	DrawChat();   //src/ui/chat.cpp
	if(GetLocalPlayer()->fzoom>.5f&&GetLocalPlayer()->playstate>=10)
	{
		char zoomtext[32];
		snprintf(zoomtext,sizeof(zoomtext),"Zoom x%.1f",powf(1.1f,GetLocalPlayer()->fzoom));
		glColor3f(0,0,0);   //shadow: readable on snow too
		UIText(.555f,-.905f,zoomtext,.06f);
		glColor3f(1,1,.6f);
		UIText(.55f,-.9f,zoomtext,.06f);
		glColor3f(1,1,1);
	}
	DrawVoiceHUD();
#endif
	
	itoa(cursendrate,tmptext,10);
	UIText(.5f,.88f,tmptext);
	itoa(currecvrate,tmptext,10);
	UIText(.7f,.88f,tmptext);
	char faceinfo[100];
	
	if(datasendthisframe)
	{
		sendbright=1;
	}
	sendbright/=1.3;
	glDisable(GL_TEXTURE_2D);
	glColor4f(1,0,0,sendbright);
	glBegin(GL_QUADS);
	glVertex2f(.5,.75);
	glVertex2f(.6,.75);
	glVertex2f(.6,.82);
	glVertex2f(.5,.82);
	
	glEnd();
	glEnable(GL_TEXTURE_2D);
	glColor3f(1,1,1);
	if(datarecvthisframe)
	{
		recvbright=1;
	}

	recvbright/=1.3;
	glDisable(GL_TEXTURE_2D);
	glColor4f(0,0,1,recvbright);
	glBegin(GL_QUADS);
	glVertex2f(.7,.75);
	glVertex2f(.8,.75);
	glVertex2f(.8,.82);
	glVertex2f(.7,.82);
	
	glEnd();
	glEnable(GL_TEXTURE_2D);
	glColor3f(1,1,1);

	datasendthisframe=0;
	datarecvthisframe=0;

	itoa(GetLocalNum(),tmptext,10);
	UIText(.3f,.88f,tmptext);
	itoa(GetLocalPlayer()->ammoreserves[GetLocalPlayer()->weapon],tmptext,10);
	//DrawText(.3,-.95,tmptext);
	float offset=-1.2;
	itoa(GetLocalPlayer()->ammoingun[GetLocalPlayer()->weapon],tmptext,10);
	UIText(.37f+offset,-.94f,tmptext,.065f);
	itoa(GetLocalPlayer()->life,tmptext,10);
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
	glColor4f(bf/2+.5,bf/2+.5,bf/2+.5,bf/2+.5);
	glBegin(GL_QUADS);
	glTexCoord2f(0,1);glVertex2f(.35+offset,-.95);
	glTexCoord2f(0,0);glVertex2f(.35+offset,-.85);
	glTexCoord2f(1,0);glVertex2f(.28+offset,-.85);
	glTexCoord2f(1,1);glVertex2f(.28+offset,-.95);
	glEnd();
	
	TexBind(TEX_HEALTHICON);
	bf=(float)GetLocalPlayer()->life/100;
	glColor4f(bf/2+.5,bf/2+.5,bf/2+.5,bf/2+.5);
	glBegin(GL_QUADS);
	glTexCoord2f(0,1);glVertex2f(.55+offset,-.95);
	glTexCoord2f(0,0);glVertex2f(.55+offset,-.85);
	glTexCoord2f(1,0);glVertex2f(.48+offset,-.85);
	glTexCoord2f(1,1);glVertex2f(.48+offset,-.95);
	glEnd();

	for(int curcredit=0;curcredit<10;curcredit++)
	{
		int credits=GetLocalPlayer()->credits[curcredit];
		if(credits)
		{
			TexBind(TEX_CREDIT0+curcredit);
			float yoffset=(float)curcredit*.15-.25;
			glColor4f(1,1,1,1);
			glBegin(GL_QUADS);
			glTexCoord2f(1,1);glVertex2f(.35+offset,yoffset-.10);
			glTexCoord2f(1,0);glVertex2f(.35+offset,yoffset);
			glTexCoord2f(0,0);glVertex2f(.28+offset,yoffset);
			glTexCoord2f(0,1);glVertex2f(.28+offset,yoffset-.10);
			glEnd();
			if(credits>1)
			{
				char tmptxt[50];
				itoa(credits,tmptxt,10);
				UIText(.36f+offset,yoffset-.09f,tmptxt,.06f);
			}
		}
	}


	glDisable(GL_TEXTURE_2D);
	if(sv.radarenable)
	{
		glColor4f(0,0,0,.4);
		float radarcenterx=-.77,radarcentery=-.60,radarwidth=.15,radarheight=.2;
		glBegin(GL_QUADS);
		glVertex2f(radarcenterx-radarwidth,radarcentery-radarheight);
		glVertex2f(radarcenterx+radarwidth,radarcentery-radarheight);
		glVertex2f(radarcenterx+radarwidth,radarcentery+radarheight);
		glVertex2f(radarcenterx-radarwidth,radarcentery+radarheight);
		glEnd();
		DrawRadarBuildZones(radarcenterx,radarcentery,radarwidth,radarheight);
		glPointSize(2*HKPixelScale());
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		glBegin(GL_POINTS);
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
				glColor4f(1,0,0,bright);
				if(fabs(theypos.x)<1&&fabs(theypos.z)<1)
				glVertex2f(radarcenterx-theypos.z*radarwidth,radarcentery+theypos.x*radarheight);
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
				glColor4f(1,0,0,bright);
				if(fabs(theypos.x)<1&&fabs(theypos.z)<1)
				glVertex2f(radarcenterx-theypos.z*radarwidth,radarcentery+theypos.x*radarheight);
			}
		}
		glEnd();
	}
	DrawGunSelection();
	
	if(sv.thrustrechargerate>0)
	{
		glBegin(GL_QUADS);
		glColor4f(1,0,0,.3+GetLocalPlayer()->thrustfuel*.4);
		glVertex2f(-.2,.95);
		glVertex2f(-.2,.9);
		glColor4f(1,0,0,.2+GetLocalPlayer()->thrustfuel*.4);
		glVertex2f(-.2+.4*GetLocalPlayer()->thrustfuel,.9);
		glVertex2f(-.2+.4*GetLocalPlayer()->thrustfuel,.95);
		
		glEnd();
	}
	//over the rest of the HUD (radar, console lines), under the console only
	if(GetLocalPlayer()->scores||rgd.gamestate==2)
		DrawScoreboard();
	DrawBuildMenu();
#ifndef HK_WIN32_NATIVE
	DrawSpawnScreen();
	if(consolevisible) DrawConsolePanel(0);
#endif
	ScreenShot();	
}


//draws everything 3d


void Draw(float offset)
{
	Player *cur=GetLocalPlayer();
	
	
	static int frame;

	frame++;
	
	int viewport[4];
	glGetIntegerv(GL_VIEWPORT,(int *)viewport);
	glBlendFunc(GL_SRC_ALPHA,GL_ZERO );
	glEnable(GL_BLEND);
	//glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
	glFogi(GL_FOG_MODE,GL_LINEAR);
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
	glFogf(GL_FOG_END,fogend);
	glFogf(GL_FOG_START,fogbegin*fogend);
	glFogf(GL_FOG_DENSITY,.04);
	
	glFogfv(GL_FOG_COLOR,color);
	
	glClearColor(color[0]*1.0115,color[1]*1.0115,color[2]*1.0110,1);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT,GL_FASTEST);
	//glHint(GL_FOG_HINT,GL_NICEST);
	
	glPushMatrix();
	
	RECT winrect;
	glLoadIdentity();
	
	//the zoom narrows the field of view. Redraw multiplied it into the projection
	//before calling Draw, but the glLoadIdentity above (projection is the current
	//matrix here) wiped it: the camera never zoomed, even in the original game.
	glMatrixMode(GL_PROJECTION);
	float zoom=powf(1.1f,cur->fzoom);
	glScalef(zoom,zoom,1);
	gluPerspective(70,1.2,.4,1700); 
	glMatrixMode(GL_MODELVIEW);
	glRotatef(GetLocalPlayer()->roll*2,0,0,1);
	camup=GetRight(VectMult(cur->tilt,cur->fwd,Vector(0,1,0),CrossProduct(cur->dir,Vector(0,1,0))),Vector(0,1,0));
	gluLookAt(cur->pos.x,cur->pos.y,cur->pos.z,cur->pos.x+cur->dir.x,cur->pos.y+cur->dir.y,cur->pos.z+cur->dir.z,camup.x,camup.y,camup.z);
	
	glEnable(GL_FOG);
	glFogf(GL_FOG_END,400);
	glFogf(GL_FOG_START,fogbegin*400);

	DrawCloudBox(cur->pos);
	if(GetSkySettings()->lightning&&curtime>nextlightning)
	{
		float c=.5+RND*.5;
		glPushAttrib(GL_ENABLE_BIT|GL_FOG_BIT|GL_COLOR_BUFFER_BIT|GL_POLYGON_BIT);
		Player *cur=GetLocalPlayer();
		glDisable(GL_FOG);
		glDisable(GL_DEPTH_TEST);
		glBlendFunc(GL_SRC_ALPHA,GL_ONE );
		TexBind(TEX_SUNBACK);
		glBegin(GL_TRIANGLE_STRIP);
		float color=RND*.5+.1;
		glColor4f(1,1,1,color);
		glTexCoord2f(0,0);

		glVertex3f(lightningpos[0].x-200,lightningpos[0].y,lightningpos[0].z+200);
		glTexCoord2f(1,0);
		glVertex3f(lightningpos[0].x-200,lightningpos[0].y,lightningpos[0].z-200);
		glTexCoord2f(0,1);
		glVertex3f(lightningpos[0].x+200,lightningpos[0].y,lightningpos[0].z+200);
		glTexCoord2f(1,1);
		glVertex3f(lightningpos[0].x+200,lightningpos[0].y,lightningpos[0].z-200);
		glEnd();
		glDisable(GL_TEXTURE_2D);
		glLineWidth(3*HKPixelScale());
		glBlendFunc(GL_ONE,GL_ZERO);
		glBegin(GL_LINES);

		glColor3f(.8,.8,.8);
		for(int i=0;i<numlightning;i++)
		{
			glVertex3fv((float *)&lightningpos[i]);
			glVertex3fv((float *)&lightningend[i]);
		}
		glEnd();
		glPopAttrib();
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
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

	glFogf(GL_FOG_END,fogend);
	glFogf(GL_FOG_START,fogbegin*fogend);
	glEnable(GL_FOG);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	
	glClear(GL_DEPTH_BUFFER_BIT);	
	glPopMatrix();
	glPushMatrix();
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glScalef(zoom,zoom,1);   //same zoom as the sky projection above
	gluPerspective(70,1.2,.15,fogend);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	//glLoadIdentity();
	/*float xyoffsetmx[4][4]=
	{
		{1,0,0,0},
		{0,1,0,0},
		{0,0,1,0},
		{offset,0,0,1}
	};
	
	glMultMatrixf((float *)xyoffsetmx);*/
	glRotatef(GetLocalPlayer()->roll*2,0,0,1);
	eyepos=cur->pos+offset*Normalize(CrossProduct(cur->dir,Vector(0,1,0)));

	gluLookAt(eyepos.x,eyepos.y,eyepos.z,eyepos.x+cur->dir.x,eyepos.y+cur->dir.y,eyepos.z+cur->dir.z,camup.x,camup.y,camup.z);
	
	
	Vector drawpos=eyepos+cur->dir*(17*2-5);
	
	glDisable(GL_BLEND);

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
	glEnable(GL_BLEND);
	osize=0;
	
	Vector pos,front,top,right;
	Vector gunup,partup,partright,partfront;
	int i;
	for(i=0;i<GetNumPlayers();i++)  //draws all the players
	{
		Player *cur=GetPlayer(i);
		if(cur->playstate==10)
		{
			if(cur==GetLocalPlayer())
			{
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
				if(cur->team==0)glColor3f(1,1,1);
				if(cur->team==1)glColor3f(1,0,0);
				if(cur->team==2)glColor3f(0,0,1);
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
				glColor3f(1,1,1);
				char *teamtext;

				if(sobject[i].type==1)
				{
					if(sobject[i].team==0){teamtext="White";glColor3f(1,1,1);}
					if(sobject[i].team==1){teamtext="Red";glColor3f(1,0,0);}
					if(sobject[i].team==2){teamtext="Blue";glColor3f(0,0,1);}
					DrawText3D(sobject[i].dir,sobject[i].pos+sobject[i].dir*.2+Vector(0,.5,0),1,teamtext);
					DrawText3D(sobject[i].dir*-1,sobject[i].pos+sobject[i].dir*-.2+Vector(0,.5,0),1,teamtext);
				}
			}
		}
	}
	
	double modelview[4][4];
	double projection[4][4];
	
	double cposx,cposy,cposz;
	double sposx,sposy,sposz;
	glGetDoublev(GL_PROJECTION_MATRIX,(double *)projection);
	glGetDoublev(GL_MODELVIEW_MATRIX,(double *)modelview);
	cposx=cur->pos.x;
	cposy=cur->pos.y+50*GetSkySettings()->sunheight;  //find the position of the sun
	cposz=cur->pos.z+50;
	int success=gluProject(cposx,cposy,cposz,(double *)modelview,(double *)projection,(int *)viewport,&sposx,&sposy,&sposz);
			//find the screen coordinates of the sun

	
	glEnable(GL_DEPTH_TEST);
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
	
	float depth;
	
	glReadPixels(sposx,sposy,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);  
	//reads the depth of where the sun is going to be, if there is nothing there, depth=1
	
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE);
	glBegin(GL_TRIANGLES);
	
	for(i=0;i<MAXPARTICLES;i+=1)
	if(particle[i].type)
	if(MakeClose(&particle[i].pos))  //draws particles if they're close enough
	{

		if(curtime>particle[i].timeout)
			particle[i].type=0;
		particle[i].pos+=particle[i].vel;
		if(particle[i].type==1)
		{
			glColor4f(.5,.5,.9,.2);
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(.05,-.5,0)));
			glVertex3fv((float *)&(particle[i].pos+Vector(-.05,-.5,0)));
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.5,-.05)));
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.5,.05)));
		}
		if(particle[i].type==2)
		{

			glColor4f(.9,.9,.9,.3);
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(.1,-.1,0)));
			glVertex3fv((float *)&(particle[i].pos+Vector(-.1,-.1,0)));
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.1,-.1)));
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.1,.1)));
		}
		if(particle[i].type==3)
		{
			if(particle[i].pos.y<*GetRepeat(particle[i].pos.x,particle[i].pos.z)+.2||particle[i].pos.y<WATERHEIGHT||
				GetBlock(particle[i].pos.x/BLOCKSIZE,particle[i].pos.y/BLOCKSIZE,particle[i].pos.z/BLOCKSIZE)->type)  
			{
				particle[i].vel.y*=-.4;
			}
			int timeleft=particle[i].timeout-curtime;
			if(timeleft<1000) glColor4f(.9,.6,.1,.1);
			else if(timeleft<1500) glColor4f(.9,.6,.6,.3);
			else if(timeleft<2000) glColor4f(.9,.4,.1,.6);
			else if(timeleft<2500) glColor4f(.9,.3,.1,.8);
			else glColor4f(.9,.0,.0,.6);
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(.02,-.2,0)));
			glVertex3fv((float *)&(particle[i].pos+Vector(-.02,-.2,0)));
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.2,-.02)));
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.2,.02)));
		}
		if(particle[i].type==4)
		{
			int timeleft=particle[i].timeout-curtime;
			if(timeleft<200) glColor4f(.9,.6,.1,.1);
			else if(timeleft<400) glColor4f(.9,.6,.6,.3);
			else if(timeleft<600) glColor4f(.9,.4,.1,.6);
			else if(timeleft<850) glColor4f(.9,.3,.1,.8);
			else glColor4f(.9,.0,.0,.6);
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(.1,-.1,0)+particle[i].vel*3));
			glVertex3fv((float *)&(particle[i].pos+Vector(-.1,-.1,0)+particle[i].vel*3));
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.1,-.1)+particle[i].vel*3));
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.1,.1)+particle[i].vel*3));
		}
		if(particle[i].type==5)
		{
			glEnd();
			glEnable(GL_TEXTURE_2D);
			TexBind(TEX_SUN);
			glDisable(GL_FOG);
			glBlendFunc(GL_SRC_ALPHA,GL_ONE);
			glBegin(GL_TRIANGLE_STRIP);
			
			int timeleft=particle[i].timeout-curtime;
			glColor4f(.8,.8,.8,(float)timeleft/200);//(float)timeleft/200);
			Vector fwd=GetLocalPlayer()->pos-particle[i].pos;
			Vector up=GetRight(fwd,Vector(0,1,0));
			Vector right=Normalize(CrossProduct(fwd,up));
			Vector pos=(GetLocalPlayer()->pos+particle[i].pos*10)/11;
			float size=6;
			glTexCoord2f(0,0);
			glVertex3fv((float *)&(pos+up*size+right*size));
			glTexCoord2f(0,1);
			glVertex3fv((float *)&(pos+up*size-right*size));
			glTexCoord2f(1,0);
			glVertex3fv((float *)&(pos-up*size+right*size));
			glTexCoord2f(1,1);
			glVertex3fv((float *)&(pos-up*size-right*size));
			glEnd();
			glDisable(GL_TEXTURE_2D);
			glEnable(GL_FOG);
			glBegin(GL_TRIANGLES);
		}
	}
	glEnd();
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glBegin(GL_TRIANGLES);
	
	for(i=0;i<MAXPARTICLES;i+=1)
	if(particle[i].type)
	if(MakeClose(&particle[i].pos))
	{

		if(particle[i].type==6)
		{
			int timeleft=particle[i].timeout-curtime;
			glColor4f(.3,.15,.1,1);
			
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(.07,-.07,0)+particle[i].vel*3));
			glVertex3fv((float *)&(particle[i].pos+Vector(-.07,-.07,0)+particle[i].vel*3));
			glVertex3fv((float *)&particle[i].pos);
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.07,-.07)+particle[i].vel*3));
			glVertex3fv((float *)&(particle[i].pos+Vector(0,-.07,.07)+particle[i].vel*3));
		}
	}
	glEnd();
	glPopAttrib();
	glCullFace(GL_FRONT);
	DrawWater(50,drawpos.x,drawpos.z);
	
	
	glDisable(GL_TEXTURE_2D);
	
	
	glEnable(GL_LINE_SMOOTH);
	glLineWidth(3*HKPixelScale());
	
	glBegin(GL_LINES);
	
	
	for(i=0;i<30;i++)
	{
		glColor4f(1,0,0,0);
		glVertex3fv((float *)&spark[i]);
		glColor4f(1,1,0,sparkbright[i]);
		glVertex3fv((float *)&(spark[i]+sparkvel[i]*sparklen[i]));
		spark[i]+=sparkvel[i];
		sparkbright[i]/=1.1;
		if(spark[i].y<WATERHEIGHT) sparkbright[i]/=3.0;
	}
	glEnd();
	glEnable(GL_TEXTURE_2D);
	DrawGunExplosions();
	
	
	
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
		glPushAttrib(GL_ENABLE_BIT|GL_FOG_BIT|GL_COLOR_BUFFER_BIT|GL_POLYGON_BIT);
		DrawSun();
		glPopAttrib();
	
	}
	
	//crosshair
	glCullFace(GL_BACK);
	
	glBlendFunc(GL_SRC_ALPHA,GL_ONE);
	TexBind(TEX_CROSSHAIR);
	glPopMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glTranslatef(offset*-.5,0,0);
	glBegin(GL_TRIANGLE_STRIP);
	glColor4f(1,1,1,1);
	glTexCoord2f(0,0);
	
	glVertex2f(-.06,-.06);
	glTexCoord2f(0,1);
	glVertex2f(.06,-.06);
	glTexCoord2f(1,0);
	glVertex2f(-.06,.06);
	glTexCoord2f(1,1);
	glVertex2f(.06,.06);
	glEnd();
	glEnable(GL_FOG);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1,1,1,1);

	
}

//draw the splash screen

void RedrawSplash(int texnum,char *text,int drawlogo)
{
	glMatrixMode(GL_MODELVIEW);
	texnum=TEX_SPLASH;   //always the splash, whatever the caller passes
	glEnable(GL_TEXTURE_2D);
	TexBind(texnum);
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE,GL_ZERO);
	glBegin(GL_QUADS);
	glColor4f(1,1,1,0);
	glTexCoord2f(1,0); glVertex2i(1,1);
	glTexCoord2f(0,0); glVertex2i(-1,1);
	glTexCoord2f(0,.75); glVertex2i(-1,-1);
	glTexCoord2f(1,.75); glVertex2i(1,-1);
	glEnd();
	glColor4f(1,1,1,1);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA );

	if(text)
		UIText(-.97f,-.97f,text);
	PAINTSTRUCT ps;
	HWND hWnd=GetHWND();
	HDC hdc = BeginPaint(hWnd, &ps);
	EndPaint(hWnd, &ps);
	
}


//just a wrapper

void RedrawSplash(int texnum,char *text)
{
	RedrawSplash(texnum,text,0);
}
