#ifndef REDRAWINGH
#define REDRAWINGH

#include "hktypes.h"
#include "vector.h"

#define MAXSCENERY 100
#define RND ((float)rand()/RAND_MAX)
#define MAXPARTICLES 3000

#include "textures.h"

void LoadTextures();
void Redraw();
int GetTexID(const char *name);
void LoadRandom(const char *dir,int texnum);
void LoadGuns();
void RedrawSplash(int texnum,char *text);
void LoadSpriteTextures();
void LoadBlockTextures();
void LoadHighTextures();
void LoadLogoTextures();
void RedrawSplash(int texnum,char *text,int drawlogo);
void GetRandom(const char *dir,char name[255]);
int GetNumScenery();
void DrawBlock(int x,int y,int z,unsigned char ds,char type,int &px,int &py,int &pz,char red);
void DrawConsole();
void LoadScenery();
void MoveUnnecessaryTextures();
float DrawText(float,float,const char *);
void Draw(float offset);

#include "ase.h"

#define pi 3.14159265358979326846264

struct GunDrawInfo
{
	Vector explosionoffset;
	float explosionsize;
	int explosionnorotate;
	Vector offset;
	Vector partoffset[2];
	Vector partup[2],partright[2];
	AseObj part[2],main;
	AseObj playermodel;
};

struct GraphicsPrefs
{
	float drawdistance;
	int mipdist[5];
	int minmips;
	float stereoseparation;
};

//visual effects of M13.5, client side only, each switchable in Options (control.ini)
struct FxPrefs
{
	int viewmodel;        //the gun in its own pass: never inside walls or terrain
};

struct Particle
{
	char type;
	Vector pos,vel;
	DWORD timeout;
};

struct LillyPad
{
	Vector pos;
	float size;
	char type;
};

//redrawing.cpp (the 3D models: models.h)
extern GraphicsPrefs graphicsprefs;
extern FxPrefs fxprefs;
extern Particle particle[MAXPARTICLES];
extern int numlillypad;
extern LillyPad lillypad[200];


#endif