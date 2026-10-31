//includes all the tools for creating a random landscape, creating
//buildings, and various other things


#include "stdafx.h"
#include "math.h"
#include "land.h"
#include "cfgfile.h"
#include "Redrawing.h"
#include "time.h"
#include "gl/gl.h"
#include "vector.h"
#include "hugekilla.h"
#include "playing.h"
#include "mmsystem.h"
#include "sound.h"

#define RND ((float)rand()/RAND_MAX)
char clouds[1024*1024];

char landtex[LANDTEXGRIDSIZE][LANDTEXGRIDSIZE];
float land[LANDGRIDSIZE][LANDGRIDSIZE];
float landmip[6][LANDGRIDSIZE][LANDGRIDSIZE];
float lighting[LANDGRIDSIZE][LANDGRIDSIZE];
char drawwater[WATERGRIDSIZE][WATERGRIDSIZE];
float waterlight[WATERGRIDSIZE][WATERGRIDSIZE];
//float landblocklighting[LANDGRIDSIZE][LANDGRIDSIZE];
int numsceneries;
static Scenery scenery[5000];  // distinct from redrawing.cpp's AseObj scenery (MSVC kept them apart by type)

extern int numlillypad;
extern LillyPad lillypad[200];
extern ServerObject sobject[MAXSERVEROBJS];

UINT cloudsize;


static int cloudposx,cloudposy;
Block blocks[BLOCKGRIDSIZE][BLOCKGRIDHEIGHT][BLOCKGRIDSIZE];
char nextblock[BLOCKGRIDSIZE][BLOCKGRIDHEIGHT][BLOCKGRIDSIZE];
SkySettings skysettings;
void SmoothLight();
void SmoothWaterLight();

//distance

float Dist(float x1,float y1,float x2,float y2)
{
	return (float)sqrt(powf(x2-x1,2)+powf(y2-y1,2));
}

//resets the land

void FlattenLand(float height)
{
	for(int x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			land[x][y]=height;
		}
	}
}

//gets the height of the land (so that it repeats)

float *GetRepeat(int x,int y)
{
	x&=LANDGRIDSIZE-1;
	y&=LANDGRIDSIZE-1;
	return &land[x][y];
}

//called to make a deformation to the land

void AddRidge(LandDeformation *df, LandSettings *ls)
{
	float numsteps=1;
	float curh,curw,curf,curx,cury,curc,curd;
	float f,fi;
	
	if(df->type==0)
	{
		df->sw=70;
	}

	for(int i=0;i<numsteps;i++)
	{
		f=(float)i/numsteps;
		fi=1-f;
		curx=df->sx;
		cury=df->sy;
		curh=df->sh*5;
		curw=df->sw;//*(curh/180);
		curf=df->sf;
		curc=df->sc;
		curd=df->sd;
		int minx,miny,maxx,maxy;
		minx=curx-curw;
		miny=cury-curw;
		maxx=curx+curw;
		maxy=cury+curw;
		
		for(int xr=minx;xr<maxx;xr++)
		{
			for(int yr=miny;yr<maxy;yr++)
			{
				float *cgp=GetRepeat(xr,yr);
				float dst=Dist(xr,yr,curx,cury);
				float div=1-(dst/curw);
				div=div>0?div:0;
				div=div*div;
				float np=(((curw)/((curw-dst)+curf)))*div;
				if(np>curc)np/=1+(np-curc)*curd;
				np*=curh;
				if(df->type==0)
				{
					*cgp+=np/3;
					
				}
				else
				if(df->type==1)
				{
					*cgp=np>*cgp?np:*cgp;
				}
				else
				if(df->type==2)
				{
					if(np>*cgp)
					{
						*cgp-=np/10;//(np-*cgp)*.3;
					}
				}
			}
		}
	}
}

//sets the cloud to a position

void SetCloudShift(float x,float y)
{
	cloudposx=x*LANDGRIDSIZE*8;
	cloudposy=y*LANDGRIDSIZE*8;
}

//gets the lighting for a point of land

float *GetLandLight(int x,int y)
{
	x&=LANDGRIDSIZE-1;
	y&=LANDGRIDSIZE-1;
	return &lighting[x][y];
}

//gets the height of the land, but interpolated

float GetLandSmooth(float fx,float fy)
{
	int x,y,x1,y1;
	x=fx,y=fy;
	fx-=x,fy-=y;
	x&=LANDGRIDSIZE-1;
	y&=LANDGRIDSIZE-1;
	x1=(x+1)&(LANDGRIDSIZE-1);
	y1=(y+1)&(LANDGRIDSIZE-1);
	return (land[x][y]*(1-(fx))+land[x1][y]*(fx))*(1-(fy))+
		(land[x][y1]*(1-(fx))+land[x1][y1]*(fx))*((fy));
}

//calculates lighting, shadows for the land

void LightLand(float ang)
{
	Vector lightdir(0,skysettings.sunheight,1);
	lightdir=Normalize(lightdir);
	
	
	int x;
	for(x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			Vector normal;
			Vector center(x,*GetRepeat(x,y),y);
			
			normal=Normalize(CrossProduct(Vector(x-1,*GetRepeat(x-1,y),y)-center,Vector(x,*GetRepeat(x,y-1),y-1)-center));
			float color=(-DotProduct(lightdir,normal));
			if(color<.2)color=.2;
			lighting[x][y]=color*skysettings.sunskylight*3;//.5+(*GetRepeat(x,y)-*GetRepeat(x,y+1))/4;
			if(lighting[x][y]<.3)lighting[x][y]=.3;
			
		}
	}
	float waterconstantlight=(DotProduct(lightdir,Vector(0,1,0)));
	for(x=0;x<WATERGRIDSIZE;x++)
	{
		for(int y=0;y<WATERGRIDSIZE;y++)
		{
			waterlight[x][y]=waterconstantlight;
		}
	}
	float curlightheight;
	int nx,ny;
	float nl;
	for(x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			curlightheight=land[x][y]-ang;
			
			nx=x;
			ny=y-1;
			while(curlightheight>*GetRepeat(nx,ny))
			{
				nl=.5+(*GetRepeat(nx,ny)-curlightheight)/8;
				if(nl<.3)nl=.3;
				if(nl>.8)nl=.8;
				if(lighting[nx][ny]>nl)lighting[nx][ny]=nl;
				curlightheight-=ang;
				ny--;
				if(ny<0) ny=LANDGRIDSIZE-1;
			}
			if(land[x][y]>WATERHEIGHT)
			{
				*GetWaterLight(x/WATERBLOCKSIZE,(y-(int)((land[x][y]-WATERHEIGHT)/ang))/WATERBLOCKSIZE)=.3;
			}
			
		}
	}
	for(x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			if(land[x][y]<WATERHEIGHT)
				lighting[x][y]=.3;
		}
	}
	
	int i;
	for(i=0;i<1;i++)
	{
		SmoothWaterLight();
	}
	for(i=0;i<7;i++)
	{
		SmoothLight();
	}
}

//reads a config

void LoadLandSettings(LandSettings *ls)
{

	CfgFile settings;
	settings.Load("land.ini");
	ls->hrmin=settings.GetValuef("hrmin");
	ls->hrnum=settings.GetValuef("hrnum");
	ls->wrmin=settings.GetValuef("wrmin");
	ls->wrnum=settings.GetValuef("wrnum");
	ls->frmin=settings.GetValuef("frmin");
	ls->frnum=settings.GetValuef("frnum");
	ls->crmin=settings.GetValuef("crmin");
	ls->crnum=settings.GetValuef("crnum");
	ls->drmin=settings.GetValuef("drmin");
	ls->drnum=settings.GetValuef("drnum");
	
	ls->maxridgelen=settings.GetValuef("maxridgelen");
	ls->defsmin=settings.GetValuei("defsmin");
	ls->defsnum=settings.GetValuei("defsnum");
	ls->numscenery=settings.GetValuei("numscenery");
	ls->numblotch=settings.GetValuei("numblotch");
}

//loads a sky texture

void LoadSky()
{
	CfgFile skysets;
	skysets.Load("skies.ini");
	int numskies=skysets.GetValuei("numskies");
	int sky=RND*numskies;
	char title[300];
	strcpy(title,"sky");
	itoa(sky,&title[strlen(title)],10);
	char *subtitle=&title[strlen(title)];
#define LoadSkyVal(val,fval) \
	strcpy(subtitle,fval);\
	skysettings.val=skysets.GetValuef(title);
	LoadSkyVal(sunheight,"sunheight");
	LoadSkyVal(fogr,"fogr");
	LoadSkyVal(fogg,"fogg");
	LoadSkyVal(fogb,"fogb");
	LoadSkyVal(cloudvelx,"cloudvelx");
	skysettings.cloudvelx/=4;
	LoadSkyVal(cloudvely,"cloudvely");
	skysettings.cloudvely/=4;
	LoadSkyVal(cloudmistvelx,"cloudmistvelx");
	skysettings.cloudmistvelx/=4;
	LoadSkyVal(cloudmistvely,"cloudmistvely");
	skysettings.cloudmistvely/=4;
	
	LoadSkyVal(sunskylight,"sunskylight");
	LoadSkyVal(suneyelight,"suneyelight");
	LoadSkyVal(fogbegin,"fogbegin");
	LoadSkyVal(uselandclouds,"uselandclouds");
	LoadSkyVal(mistopacity,"mistopacity");
	LoadSkyVal(cloudheight,"cloudheight");
	strcpy(subtitle,"cloudtex");
	char fullfilename[300];
	strcpy(fullfilename,"textures\\clouds\\");
	strcat(fullfilename,skysets.GetValues(title));
	strcpy(skysettings.cloudtex,skysets.GetValues(title));
	LoadTexture(fullfilename,2);
	strcpy(subtitle,"cloudmist");
	char *tmp=skysets.GetValues(title);
	if(tmp)
	{
		strcpy(skysettings.misttex,tmp);
		strcpy(fullfilename,"textures\\clouds\\");
		strcat(fullfilename,tmp);
		LoadRaw(fullfilename,6);
		skysettings.usemist=1;
	}else skysettings.usemist=0;
	strcpy(subtitle,"sound");
	char *sound=skysets.GetValues(title);
	if(sound)
	{
		strcpy(skysettings.sound,sound);
		strcpy(fullfilename,"sound\\");
		strcat(fullfilename,sound);
		LoadSound(fullfilename,0,0);
		PlaySound(0,1);
	}
	GetDefs()->numgroundtex=0;
	do
	{
		strcpy(subtitle,"groundtex");
		itoa(GetDefs()->numgroundtex,&subtitle[9],10);
		char *groundtex;
		if(groundtex=skysets.GetValues(title))
		{
			strcpy(GetDefs()->groundtex[GetDefs()->numgroundtex],groundtex);
			GetDefs()->numgroundtex++;
		}
		else
		{
			break;
		}
	}while(1);
	
	LoadSkyVal(precipitation,"precipitation");
	LoadSkyVal(lightning,"lightning");
	LoadSkyVal(numlillypad,"numlillypad");
}

//wrapper

SkySettings *GetSkySettings()
{
	return &skysettings;
}

//creates a set of parameters to create a landscape

void RandomLand(LandDefs &ldfs)
{
	srand( (unsigned)time( NULL ) );
	LandSettings ls;
	LoadLandSettings(&ls);
	ldfs.numdefs=ls.defsmin+ls.defsnum*RND;
	if(ldfs.numdefs>=NUMDEF) ldfs.numdefs=NUMDEF-1;
	for(int i=0;i<ldfs.numdefs;i++)
	{
		ldfs.df[i].type=RND*3;
		ldfs.df[i].sx=RND*LANDGRIDSIZE;
		ldfs.df[i].sy=RND*LANDGRIDSIZE;
		ldfs.df[i].sh=(ls.hrmin+RND*ls.hrnum)/5;
		ldfs.df[i].sw=ls.wrmin+RND*ls.wrnum;
		ldfs.df[i].sw*=ldfs.df[i].sw;
		ldfs.df[i].sf=ls.frmin+RND*ls.frnum;
		ldfs.df[i].sf*=ldfs.df[i].sf;
		ldfs.df[i].sc=ls.crmin+RND*ls.crnum;
		ldfs.df[i].sd=ls.drmin+RND*ls.drnum;
	}	
	ldfs.randomseed=rand();
	ldfs.numscenery=ls.numscenery;
	ldfs.numblotch=ls.numblotch;
}

//called a few times to blur the light

void SmoothLight()
{
	float nl[LANDGRIDSIZE][LANDGRIDSIZE];
	for(int x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			nl[x][y]=((*GetLandLight(x-1,y-1)+*GetLandLight(x+1,y+1)+*GetLandLight(x+1,y-1)+*GetLandLight(x-1,y+1))/4)*.3+
				((*GetLandLight(x-1,y)+*GetLandLight(x+1,y)+*GetLandLight(x,y-1)+*GetLandLight(x,y+1))/4)*.6+*GetLandLight(x,y)*.1;
		}
	}
	CopyMemory(lighting,nl,LANDGRIDSIZE*LANDGRIDSIZE*4);
}

//does the same for water

void SmoothWaterLight()
{
	float nl[WATERGRIDSIZE][WATERGRIDSIZE];
	for(int x=0;x<WATERGRIDSIZE;x++)
	{
		for(int y=0;y<WATERGRIDSIZE;y++)
		{
			nl[x][y]=((*GetWaterLight(x-1,y-1)+*GetWaterLight(x+1,y+1)+*GetWaterLight(x+1,y-1)+*GetWaterLight(x-1,y+1))/4)*.3+
				((*GetWaterLight(x-1,y)+*GetWaterLight(x+1,y)+*GetWaterLight(x,y-1)+*GetWaterLight(x,y+1))/4)*.6+*GetWaterLight(x,y)*.1;
		}
	}
	CopyMemory(waterlight,nl,WATERGRIDSIZE*WATERGRIDSIZE*4);
}

//GR(x,y) is used instead of GetRepeat(x,y) for about a 200% or 300% speed improvement

#define GR(x,y) land[x&LANDGRIDSIZE-1][y&LANDGRIDSIZE-1]

//makes the land smoother

void SmoothLand()
{
	float nl[LANDGRIDSIZE][LANDGRIDSIZE];
	
	for(int x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			nl[x][y]=((GR(x-1,y-1)+GR(x+1,y+1)+GR(x+1,y-1)+GR(x-1,y+1))/4)*.8+
				((GR(x-1,y)+GR(x+1,y)+GR(x,y-1)+GR(x,y+1))/4)*.2;//+GR(x,y)*0;
		}
	}
	CopyMemory(land,nl,LANDGRIDSIZE*LANDGRIDSIZE*4);
}

//figures out where the water will need to be drawn from the height of the land

void CalculateWater()
{
	for(int x=0;x<WATERGRIDSIZE;x++)
	{
		for(int y=0;y<WATERGRIDSIZE;y++)
		{
			drawwater[x][y]=0;
			for(int x2=x*WATERBLOCKSIZE;x2<=(x+1)*WATERBLOCKSIZE;x2++)
			{
				for(int y2=y*WATERBLOCKSIZE;y2<=(y+1)*WATERBLOCKSIZE;y2++)
				{
					if(*GetRepeat(x2,y2)<(WATERHEIGHT+1))
					{
						drawwater[x][y]=1;
						break;
					}
				}
				if(drawwater[x][y])
					break;
			}
		}
	}
}

//places blotches (textures that can replace the standard repeating texture and still fit in)
//on the land

void Blotchify(LandDefs &ldfs)
{
	for(int x=0;x<LANDTEXGRIDSIZE;x++)
	for(int y=0;y<LANDTEXGRIDSIZE;y++)
	{
		landtex[x][y]=0;
	}
	for(int i=0;i<ldfs.numblotch;i++)
	{
		int x;
		int y;
		int numtries=0;
reposition:
		numtries++;
		x=RND*LANDTEXGRIDSIZE;
		y=RND*LANDTEXGRIDSIZE;
		if((landtex[x&LANDTEXGRIDSIZE-1][y&LANDTEXGRIDSIZE-1]||
			landtex[x-1&LANDTEXGRIDSIZE-1][y&LANDTEXGRIDSIZE-1]||
			landtex[x+1&LANDTEXGRIDSIZE-1][y&LANDTEXGRIDSIZE-1]||
			landtex[x&LANDTEXGRIDSIZE-1][y-1&LANDTEXGRIDSIZE-1]||
			landtex[x&LANDTEXGRIDSIZE-1][y+1&LANDTEXGRIDSIZE-1])&numtries<60) goto reposition;
		landtex[x][y]=RND*(ldfs.numgroundtex-1)+1;
	}
}

//uses a LandDefs structure to create a landscape

void CreateLand(LandDefs &ldfs)
{
	ConsoleDisplayText("Creating land");
	GReset();
	LandSettings ls;
	LoadLandSettings(&ls);
	FlattenLand(0);
	
	if(GetLocalPlayer()->playstate==1)LoadTexture("textures\\startup.bmp",1);
	int x;
	for(x=0;x<BLOCKGRIDSIZE;x++)
	for(int y=0;y<BLOCKGRIDHEIGHT;y++)
	for(int z=0;z<BLOCKGRIDSIZE;z++)
	{
		blocks[x][y][z].type=0;
		blocks[x][y][z].nextblock=BLOCKGRIDHEIGHT-y;
	}
	char tmptext[500];
	int i;
	for(i=0;i<ldfs.numdefs;i++)
	{
		AddRidge(&ldfs.df[i],&ls);
		strcpy(tmptext,"Creating land: ");
		itoa((float)i/ldfs.numdefs*100,&tmptext[strlen(tmptext)],10);
		strcat(tmptext,"% completed");
		RedrawSplash(800,tmptext);
		////Redraw(hdc);
	}
	for(i=0;i<20;i++)
	{
		SmoothLand();
		strcpy(tmptext,"Smoothing land (pass 1): ");
		itoa((float)(i+1)/20*100,&tmptext[strlen(tmptext)],10);
		strcat(tmptext,"% completed");
		RedrawSplash(800,tmptext);
		
		////Redraw(hdc);
	}
	RedrawSplash(800,"Lighting land");
	
	float lowest=land[0][0];
	for(x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			if(land[x][y]<lowest)lowest=land[x][y];
		}
	}
	lowest-=6;
	for(x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			land[x][y]-=lowest;
		}
	}
	for(x=0;x<LANDGRIDSIZE;x++)
	{
		for(int y=0;y<LANDGRIDSIZE;y++)
		{
			if(land[x][y]>WATERHEIGHT)
			{
				
				if(land[x][y]<WATERHEIGHT+10) 
				{
					land[x][y]-=(land[x][y]-WATERHEIGHT)*.3;
				}
				else
				{
					land[x][y]-=10*(.3);
				}
				land[x][y]+=.9;
			}
			else land[x][y]=(land[x][y]-WATERHEIGHT)/2+WATERHEIGHT-.7;

		}
	}
	for(i=0;i<1;i++)
	{
		SmoothLand();
		strcpy(tmptext,"Smoothing land (pass 2): ");
		itoa((float)(i+1)/1*100,&tmptext[strlen(tmptext)],10);
		strcat(tmptext,"% completed");
		RedrawSplash(800,tmptext);
		
		////Redraw(hdc);
	}
	for(i=0;i<6;i++)
	{
		int size=1<<(i<3?i:3);
		int smallpart=(size-1);
		int rounder=~smallpart&(LANDGRIDSIZE-1);
		float fx,fy;
		for(x=0;x<LANDGRIDSIZE;x++)
		{
			for(int y=0;y<LANDGRIDSIZE;y++)
			{
				fx=(float)(x&smallpart)/size;
				fy=(float)(y&smallpart)/size;
				landmip[i][x][y]=(land[x&rounder][y&rounder]*(1-fx)+land[x+size&rounder][y&rounder]*fx)*(1-fy)+
					(land[x&rounder][y+size&rounder]*(1-fx)+land[x+size&rounder][y+size&rounder]*fx)*fy;
								
			}
		}
	}

	LightLand(skysettings.sunheight);
	RedrawSplash(800,"Loading ground texture");
	char fullfilename[400];
	for(i=0;i<ldfs.numgroundtex;i++)
	{
		strcpy(fullfilename,"textures\\ground\\");
		strcat(fullfilename,ldfs.groundtex[i]);
		LoadTexture(fullfilename,800+i);
	}
	CalculateWater();
	
	void *himage=LoadImage(NULL,"textures\\clouds\\landcloud.bmp",IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION|LR_LOADFROMFILE);
	
	BITMAP jonx;
	if(!GetObject(himage,sizeof(jonx),&jonx)) 
	{
		MessageBox(GetHWND(),"textures\\clouds\\landcloud.bmp","Texture not found",MB_OK);
		return;
	}
	if(jonx.bmPlanes!=1)
	{
		MessageBox(GetHWND(),"Ground clouds not grayscale","Texture error",MB_OK);
		return;
	}
	cloudsize=jonx.bmWidth;
	CopyMemory(clouds,jonx.bmBits,jonx.bmWidth*jonx.bmHeight);
	DeleteObject(himage);
	numsceneries=ldfs.numscenery;
	srand(ldfs.randomseed);
	int numscenery=GetNumScenery();
	int clumpsize=0;
	int tries;
	for(i=0;i<numsceneries;i++)
	{
		if(clumpsize==0)clumpsize=RND*8;
		clumpsize--;
		tries=0;
reposition:
		tries++;
		if(clumpsize&&i>0)
			scenery[i].pos=scenery[i].pos+Vector(RND*6-3,0,RND*6-3);
		else
			scenery[i].pos=Vector(RND*LANDGRIDSIZE,0,RND*LANDGRIDSIZE);
		scenery[i].pos.y=GetLandSmooth(scenery[i].pos.x,scenery[i].pos.z);
		float xn=*GetRepeat(scenery[i].pos.x-2,scenery[i].pos.z);
		float xp=*GetRepeat(scenery[i].pos.x+2,scenery[i].pos.z);
		float yn=*GetRepeat(scenery[i].pos.x,scenery[i].pos.z-2);
		float yp=*GetRepeat(scenery[i].pos.x,scenery[i].pos.z+2);
		if(tries<30)
		{
			if(fabs(scenery[i].pos.y-xn)+fabs(scenery[i].pos.y-xp)+fabs(scenery[i].pos.y-yn)+fabs(scenery[i].pos.y-yp)>4) goto reposition;
			if(scenery[i].pos.y<WATERHEIGHT||scenery[i].pos.y>WATERHEIGHT+7) goto reposition;
		}
		
		scenery[i].size=RND*5+.5;
		scenery[i].top=Normalize(Vector(RND*.4-.2,1,RND*.4-.2));
		scenery[i].side=GetRight(scenery[i].top,Vector(RND-.5,0,RND-.5));
		scenery[i].type=RND*numscenery;
		
	}
	Blotchify(ldfs);
	numlillypad=GetSkySettings()->numlillypad;
	for(i=0;i<numlillypad;i++)
	{
		tries=0;
repos:
		tries++;
		lillypad[i].pos=Vector(RND*(LANDGRIDSIZE-8),0,RND*(LANDGRIDSIZE-8));
		lillypad[i].size=RND*.5+.5;
		lillypad[i].type=0;
		float height=*GetRepeat(lillypad[i].pos.x,lillypad[i].pos.z);
		for(int i2=0;i2<i;i2++)
		{
			if(fabs(lillypad[i2].pos.x-lillypad[i].pos.x)+fabs(lillypad[i2].pos.z-lillypad[i].pos.z)<8&&tries<30) goto repos;
		}
		if(height>WATERHEIGHT-2||height<WATERHEIGHT-8&&tries<30) goto repos;
	}
	ConsoleDisplayText("Done");
}

//returns the block in this place, repeating

Block *GetBlock(int x,int y,int z)
{
	x&=BLOCKGRIDSIZE-1;
	z&=BLOCKGRIDSIZE-1;
	y+=1;
	if(y<0) y=0;
	if(y>=BLOCKGRIDHEIGHT)y=BLOCKGRIDHEIGHT-1;
	return &blocks[x][y][z];
}

//calculates which faces of a block will need to be drawn

void FaceBlock(int x,int y,int z)
{
	Block *cb=GetBlock(x,y,z);
	cb->drawside=0;
	Block *tb;
	tb=GetBlock(x-1,y,z); if(tb->type!=1&&tb->type!=4&&!((cb->type==3&&tb->type==3)||(cb->type==5&&tb->type==5))) cb->drawside|=SIDEXN;
	tb=GetBlock(x+1,y,z); if(tb->type!=1&&tb->type!=2&&!((cb->type==3&&tb->type==3)||(cb->type==5&&tb->type==5))) cb->drawside|=SIDEXP;
	tb=GetBlock(x,y-1,z); if(tb->type!=1) cb->drawside|=SIDEYN;
	tb=GetBlock(x,y+1,z); if(tb->type!=1&&!(cb->type==1&&tb->type)||cb->type!=1) cb->drawside|=SIDEYP;
	tb=GetBlock(x,y,z-1); if(tb->type!=1&&tb->type!=3&&!((cb->type==2&&tb->type==2)||(cb->type==4&&tb->type==4))) cb->drawside|=SIDEZN;
	tb=GetBlock(x,y,z+1); if(tb->type!=1&&tb->type!=5&&!((cb->type==2&&tb->type==2)||(cb->type==4&&tb->type==4))) cb->drawside|=SIDEZP;
	if(cb->drawside)
	{
		int nb=0;
		Block *tb;
		do
		{
			y--;
			nb++;
			tb=GetBlock(x,y,z);
			tb->nextblock=nb;
		}
		while(!(tb->type&&tb->drawside)&&y>0);
	}
}

//adds a block to the world

void AddBlock(int x,int y,int z,char type,char top,char side,char bottom)
{
	if(y>=BLOCKGRIDHEIGHT-1) return;
	Block *ab=GetBlock(x,y,z);
	
	ab->top=top;
	ab->side=side;
	ab->bottom=bottom;
	
	ab->type=type;
	FaceBlock(x,y,z);
	FaceBlock(x+1,y,z);
	FaceBlock(x-1,y,z);
	FaceBlock(x,y+1,z);
	FaceBlock(x,y-1,z);
	FaceBlock(x,y,z+1);
	FaceBlock(x,y,z-1);
	int nb=0;
	Block *tb;
	do
	{
		y--;
		nb++;
		tb=GetBlock(x,y,z);
		tb->nextblock=nb;
	}
	while(!(tb->type&&tb->drawside)&&y>0);
	
}

//deletes a block

void DeleteBlock(int x,int y,int z)
{
	if(y>=BLOCKGRIDHEIGHT-1) return;
	Block *ab=GetBlock(x,y,z);
	ab->type=0;
	//FaceBlock(x,y,z);
	FaceBlock(x+1,y,z);
	FaceBlock(x-1,y,z);
	FaceBlock(x,y+1,z);
	FaceBlock(x,y-1,z);
	FaceBlock(x,y,z+1);
	FaceBlock(x,y,z-1);
	
}

//returns weather a piece of water is visible

char GetWater(int x,int y)
{
	x&=WATERGRIDSIZE-1;
	y&=WATERGRIDSIZE-1;
	return drawwater[x][y];
}

//gets the lighting of a piece of water

float *GetWaterLight(int x,int y)
{
	x&=WATERGRIDSIZE-1;
	y&=WATERGRIDSIZE-1;
	return &waterlight[x][y];
}

//wrapper

int GetNumSceneries()
{
	return numsceneries;
}

//wrapper

Scenery *GetScenery(int num)
{
	return &scenery[num];
}