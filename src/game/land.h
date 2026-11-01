#ifndef LANDH
#define LANDH
#include "hktypes.h"
#include "vector.h"
#define BLOCKSIZE 2
#define WATERBLOCKSIZE 4


#define LANDGRIDSIZE 512
#define LANDTEXGRIDSIZE (LANDGRIDSIZE/16)
#define LANDTEXBLOCKSIZE (LANDGRIDSIZE/LANDTEXGRIDSIZE)

#define BLOCKGRIDSIZE (LANDGRIDSIZE/BLOCKSIZE)
#define WATERGRIDSIZE (LANDGRIDSIZE/WATERBLOCKSIZE)
#define BLOCKGRIDHEIGHT 120
#define WATERHEIGHT 23
#define NUMDEF 160
struct SkySettings
{
	float sunheight;
	float fogr,fogg,fogb;
	float cloudvelx,cloudvely;
	float cloudmistvelx,cloudmistvely;
	float sunskylight,suneyelight;
	float fogbegin;
	int uselandclouds;
	int lightning;
	int usemist;
	int precipitation;
	float mistopacity;
	float cloudheight;
	char cloudtex[60];
	char misttex[60];
	char sound[60];
	int numlillypad;
};

struct LandDeformation
{
	char type;
	int sx,sy,sh,sw;
	float sc,sd,sf;
};

struct LandDefs
{
	int numdefs;
	LandDeformation df[NUMDEF];
	char groundtex[9][60];
	int numgroundtex;
	DWORD randomseed;
	DWORD numscenery;
	DWORD numblotch;
};

struct LandSettings
{
	float hrmin,hrnum;
	float frmin,frnum;
	float wrmin,wrnum;
	float maxridgelen;
	float crmin,crnum;
	float drmin,drnum;
	int defsmin,defsnum;
	int numscenery;
	int numblotch;
};


struct Block
{
	char drawside;
	char type;
	char top;
	char side;
	char bottom;
	char nextblock;
};

struct Scenery
{
	Vector pos,top,side;
	char type;
	float size;
};

#define SIDEXP 1
#define SIDEXN 2
#define SIDEYP 4
#define SIDEYN 8
#define SIDEZP 16
#define SIDEZN 32

void SetLandLight(int x, int y);

void SetCloudShift(float x,float y);
void CreateLand(LandDefs &ldfs);
void RandomLand(LandDefs &ldfs);
float *GetRepeat(int x,int y);
SkySettings *GetSkySettings();
void LoadSky();
void LightLand(float ang);
Block *GetBlock(int x,int y,int z);
void AddBlock(int x,int y,int z,char type,char top,char side,char bottom);
void DeleteBlock(int x,int y,int z);
float GetLandSmooth(float fx,float fy);
char GetWater(int x,int y);
float *GetWaterLight(int x,int y);
Scenery *GetScenery(int num);
int GetNumSceneries();

//land.cpp: the terrain, its lighting and water, the blocks, the sky
extern float land[LANDGRIDSIZE][LANDGRIDSIZE];
extern float landmip[6][LANDGRIDSIZE][LANDGRIDSIZE];
extern float lighting[LANDGRIDSIZE][LANDGRIDSIZE];
extern char landtex[LANDTEXGRIDSIZE][LANDTEXGRIDSIZE];
extern char drawwater[WATERGRIDSIZE][WATERGRIDSIZE];
extern float waterlight[WATERGRIDSIZE][WATERGRIDSIZE];
extern Block blocks[BLOCKGRIDSIZE][BLOCKGRIDHEIGHT][BLOCKGRIDSIZE];
extern char nextblock[BLOCKGRIDSIZE][BLOCKGRIDHEIGHT][BLOCKGRIDSIZE];
extern SkySettings skysettings;
extern char clouds[1024*1024];
//server setup overrides (serversetup.cpp); the defaults keep the original random land
extern int hkforcedsky;         //sky index from skies.ini, -1 = random
extern float hkreliefscale;     //multiplies the hill heights of land.ini
extern int hksceneryoverride;   //number of scenery objects, -1 = land.ini

#endif