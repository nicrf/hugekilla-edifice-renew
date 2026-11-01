//used to load various textures from .bmp and .pxr files  (pxr files can store an alpha channel)

#include "StdAfx.h"
#include "redrawing.h"
#include "textures.h"
#include "stdio.h"

#define MAXSPRITETEX 100


extern GunDrawInfo gun[10];
extern AseObj scenery[MAXSCENERY];
extern AseObj plobj[MAXSCENERY];
extern AseObj projectile[10];
extern int numscenery;
extern AseObj servercobs[50][3];
extern AseObj hangglider;

AseObj torso[2][10];
AseObj leg[5];

char texname[MAXSPRITETEX][400];
char texused[400];
int reflecttexnum;
int numtex;

int numpost;

int LoadTexture(char *filename,unsigned num)
{
#ifdef HKDS 
	return 0;
#endif
	if(stricmp(&filename[strlen(filename)-4],".pxr")==0)
	{
		return LoadRaw(filename,num);;
	}
	
	//stb_image gives RGB with the top row first, as the old BMP reader did by
	//turning the bottom-up BGR rows of the DIB around
	int width,height;
	unsigned char *pixels=LoadImagePixels(filename,&width,&height,3);
	if(!pixels) return 0;
	TexUpload(num,width,height,3,pixels,TEXF_MIPNEAREST);
	FreeImagePixels(pixels);
#ifndef PLMODEL
	char consolemessage[5000];
	strcpy(consolemessage,"Loaded ");
	strcat(consolemessage,filename);
	ConsoleDisplayText(consolemessage);
#endif
	return 1;
}

int LoadRaw(char *filename,unsigned num)
{
	//.pxr: a 1024-byte header, then square 8-bit pixels; the size tells the channels.
	//stb_image doesn't know it, so it keeps its own reader.
	long filelen;
	char *file=HKReadFile(filename,&filelen);
	if(!file) return 0;
	if(filelen<=1024)
	{
		delete []file;
		return 0;
	}
	char *bits=&file[1024];
	filelen-=1024;

	int channels=filelen;
	int size=1;
	do
	{
		channels/=4;
		size*=2;
	}while(channels>4);
	TexUpload(num,size,size,channels,bits,TEXF_MIPLINEAR);
	delete []file;
#ifndef PLMODEL
	char consolemessage[5000];
	strcpy(consolemessage,"Loaded ");
	strcat(consolemessage,filename);
	ConsoleDisplayText(consolemessage);
	return 1;
#endif
}

void LoadBlockTextures()
{
	int filevalid=0;
	int tnum=0;
	int fnum=0;
	char tname[100];
	do
	{
		strcpy(tname,"textures/block/");
		strcat(tname,"wall");
		itoa(fnum,&tname[strlen(tname)],10);
		strcat(tname,".bmp");
		filevalid=LoadTexture(tname,TEX_BLOCK0+tnum);
		tnum++;
		fnum++;
	}
	while(filevalid);
}
void LoadSpriteTextures()
{
	std::vector<HKDirEntry> files=HKListDir("textures/sprite");
	int curtex=TEX_SPRITE0;
	reflecttexnum=-1;
	for(size_t f=0;f<files.size();f++)
	{
		const char *name=files[f].name.c_str();
		if(files[f].isdir||!HKHasExt(name,".bmp")) continue;
		char fullfilename[300]="textures/sprite/";
		strcpy(texname[curtex-TEX_SPRITE0],name);
		strcat(fullfilename,name);
		char rawfullfilename[300];
		strcpy(rawfullfilename,fullfilename);
		strcpy(&rawfullfilename[strlen(rawfullfilename)-3],"pxr");   //a .pxr next to it wins
		if(!LoadRaw(rawfullfilename,curtex))
			LoadTexture(fullfilename,curtex);
		texused[curtex-TEX_SPRITE0]=0;
		curtex++;
	}
	numtex=curtex-TEX_SPRITE0;
}
int GetTexID(LPCTSTR name)
{
	if(stricmp("reflect.bmp",name)==0)
		return TEX_LAND;
	for(int i=0;i<numtex;i++)
	{
		if(stricmp(name,texname[i])==0)
		{
			texused[i]=1;
			return i+TEX_SPRITE0;
				
		}
	}
	return 0;
}

#ifndef PLMODEL

//selects a random texture from a directory
void GetRandom(LPCTSTR dir,char name[255])
{
	std::vector<HKDirEntry> all=HKListDir(dir),bmps;
	for(size_t i=0;i<all.size();i++)
		if(HKHasExt(all[i].name.c_str(),".bmp")) bmps.push_back(all[i]);
	if(bmps.empty()) return;
	int pick=bmps.size()*RND;
	if(pick>=(int)bmps.size()) pick=bmps.size()-1;
	strcpy(name,bmps[pick].name.c_str());
}
void LoadRandom(LPCTSTR dir,int texnum)
{
	char fullfilename[400];
	char filename[255];
	GetRandom(dir,filename);
	strcpy(fullfilename,dir);
	strcat(fullfilename,filename);
	LoadTexture(fullfilename,texnum);
}
void LoadGuns()
{
	CfgFile objcfg;
	objcfg.Load("object.ini");
	char fullfilename[300];
	char val[300];
	char *wp;
	int numguns=objcfg.GetValuei("numguns");
	for(int i=0;i<numguns;i++)
	{
		strcpy(val,"gun");
		itoa(i,&val[3],10);
		wp=&val[strlen(val)];
		strcpy(wp,"main");
		strcpy(fullfilename,"objects/");
		strcat(fullfilename,objcfg.GetValues(val));
		gun[i].main.Load(fullfilename);
		strcpy(wp,"part0");
		strcpy(fullfilename,"objects/");
		const char *sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			gun[i].part[0].Load(fullfilename);
		}
		strcpy(wp,"part1");
		strcpy(fullfilename,"objects/");
		sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			gun[i].part[1].Load(fullfilename);
		}
		strcpy(wp,"playermodel");
		strcpy(fullfilename,"objects/");
		sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			gun[i].playermodel.Load(fullfilename);
		}
		strcpy(wp,"explosion0");
		strcpy(fullfilename,"textures/explosions/");
		sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			LoadRaw(fullfilename,TEX_EXPLOSION0+i*2);
		}
		strcpy(wp,"explosionside0");
		strcpy(fullfilename,"textures/explosions/");
		sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			LoadRaw(fullfilename,TEX_EXPLOSION0+i*2+1);
		}
		
		strcpy(wp,"offset");
		gun[i].offset=objcfg.GetValuev(val)/2;
		strcpy(wp,"explosionoffset");
		gun[i].explosionoffset=objcfg.GetValuev(val)/2;
		strcpy(wp,"explosionsize");
		gun[i].explosionsize=objcfg.GetValuef(val)/2;
		strcpy(wp,"partoffset0");
		gun[i].partoffset[0]=objcfg.GetValuev(val)/2;
		strcpy(wp,"partoffset1");
		gun[i].partoffset[1]=objcfg.GetValuev(val)/2;
		strcpy(wp,"partup0");
		gun[i].partup[0]=objcfg.GetValuev(val);
		strcpy(wp,"partup1");
		gun[i].partup[1]=objcfg.GetValuev(val);
		strcpy(wp,"partright0");
		gun[i].partright[0]=objcfg.GetValuev(val);
		strcpy(wp,"partright1");
		gun[i].partright[1]=objcfg.GetValuev(val);
		gun[i].partup[0]=Normalize(gun[i].partup[0]);
		gun[i].partup[1]=Normalize(gun[i].partup[1]);
		gun[i].partright[0]=GetRight(gun[i].partup[0],gun[i].partright[0]);
		gun[i].partright[1]=GetRight(gun[i].partup[1],gun[i].partright[1]);
		strcpy(wp,"explosionnorotate");
		gun[i].explosionnorotate=objcfg.GetValuei(val);
		
	}
	
	torso[0][0].Load("objects/torso_red_gat.cob");
	torso[0][1].Load("objects/torso_red_m16.cob");
	torso[0][2].Load("objects/torso_red_acr.cob");
	torso[0][3].Load("objects/torso_red_ump.cob");
	torso[0][4].Load("objects/torso_red_nade.cob");
	torso[0][5].Load("objects/torso_red_beretta.cob");
	torso[0][6].Load("objects/torso_red_whiterifle.cob");

	torso[1][0].Load("objects/torso_blue_gat.cob");
	torso[1][1].Load("objects/torso_blue_m16.cob");
	torso[1][2].Load("objects/torso_blue_acr.cob");
	torso[1][3].Load("objects/torso_blue_ump.cob");
	torso[1][4].Load("objects/torso_blue_nade.cob");
	torso[1][5].Load("objects/torso_blue_beretta.cob");
	torso[1][6].Load("objects/torso_blue_whiterifle.cob");

	leg[0].Load("objects/run_rightback.cob");
	leg[1].Load("objects/run_rightback2.cob");
	leg[2].Load("objects/run_neutral.cob");
	leg[3].Load("objects/run_leftback2.cob");
	leg[4].Load("objects/run_leftback.cob");
	LoadScenery();
}



void LoadScenery()
{
	CfgFile objcfg;
	objcfg.Load("object.ini");
	char prop[50];
	char *filename;
	char fullfilename[200];
	numscenery=0;
	do
	{
		strcpy(prop,"scenery");
		itoa(numscenery,&prop[strlen(prop)],10);
		filename=objcfg.GetValues(prop);
		if(filename)
		{
			strcpy(fullfilename,"objects/");
			strcat(fullfilename,filename);
			scenery[numscenery].Load(fullfilename);
			numscenery++;
		}
	} while(filename);
	
	plobj[0].Load("objects/best.cob");
	projectile[0].Load("objects/rocket.cob");
	projectile[1].Load("objects/roundz.cob");
	projectile[2].Load("objects\\nade.cob");
	servercobs[1][0].Load("objects/dome.cob");
	servercobs[2][0].Load("objects/waterpump.cob");
	servercobs[2][1].Load("objects/station_red.cob");
	servercobs[2][2].Load("objects/station_blue.cob");
	servercobs[3][0].Load("objects/flag_blue.cob");
	servercobs[3][1].Load("objects/flag_red.cob");
	servercobs[3][2].Load("objects/flag_blue.cob");
	hangglider.Load("objects/hangglider.cob");

}
int GetNumScenery()
{
	return numscenery;
}
void MoveUnnecessaryTextures()
{
	ConsoleDisplayText("Moving unnecessary textures");
	int numdisplayed=0;
	for(int i=0;i<numtex;i++)
	{
		if(!texused[i])
		{
			char oldfilename[5000],newfilename[5000];
			strcpy(oldfilename,"textures/sprite/");
			strcat(oldfilename,texname[i]);
			strcpy(newfilename,"textures/sprite/unused/");
			strcat(newfilename,texname[i]);
			char message[10000];
			sprintf(message,"Moving %s to %s",oldfilename,newfilename);
			ConsoleDisplayText(message);
			HKRenameFile(oldfilename,newfilename);
		}
	}
}

void LoadHighTextures()
{
	LoadSpriteTextures();
	LoadBlockTextures();
	LoadGuns();
	
	LoadTexture("textures/mainmenu.pxr",TEX_MENU0);
	int numtexused=0;
	for(int i=0;i<numtex;i++)
	{
		numtexused+=texused[i];
	}
	char consolemessage[1000];

	sprintf(consolemessage,"Sprite textures: %i Used: %i Percent: %i",numtex,numtexused,(int)((float)numtexused/numtex*100));
	ConsoleDisplayText(consolemessage);	

	
}
//font and splash, which the console and loading screens need before anything else
void LoadStartupTextures()
{
	LoadTexture("textures/fonts/courier.pxr",TEX_FONT);
	LoadSplashTexture();
}
void LoadSplashTexture()
{
	LoadTexture("textures/startup.bmp",TEX_SPLASH);
}
void LoadTextures()
{
	glEnable(GL_TEXTURE_2D);
	LoadTexture("textures/ammoicon.bmp",TEX_AMMOICON);
	LoadTexture("textures/healthicon.bmp",TEX_HEALTHICON);
	
	LoadTexture("textures/buildicon.bmp",TEX_CREDIT0);
	LoadTexture("textures/stationmoveicon.bmp",TEX_CREDIT0+1);


	LoadTexture("textures/sun.bmp",TEX_SUN);
	LoadTexture("textures/sunback.bmp",TEX_SUNBACK);
	LoadTexture("textures/crosshair.bmp",TEX_CROSSHAIR);
	LoadTexture("textures/water/seamlesswater.bmp",TEX_WATER);
	LoadTexture("textures/lillypad.pxr",TEX_LILLYPAD);
	LoadTexture("textures/gunnames.pxr",TEX_GUNNAMES);
}
#endif