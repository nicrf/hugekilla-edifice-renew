//Rocks and trees that block (docs/PROPS.md): the props.
//
//Placed from the land alone, identically on the server and every client (no
//message carries them): a hash of the land's seed (LandDefs randomseed) and of each
//cell picks the spots, the heightmap says which are valid (no water, no steep
//slope). A saved map rebuilds its land from its LandDefs, so it gets the same props
//without anything in the map file. Every machine then tests the same cylinders:
//players walk around the trunks and onto the rocks, bullets (the server's too, M11
//phase 3a) and grenades stop on them, blocks can't be placed inside.
//
//props.cpp: the pure part (placement, hash, collision math), unit tested by
//tests/props_test.cpp. propsgame.cpp: the game's props (built by CreateLand, which
//ones are there now, the queries of the physics). The server says whether they are
//on (sv.ini props, PROPSMODE, HELLOCAP_PROPS in version.h).

#ifndef PROPSH
#define PROPSH

#include <vector>

#define PROP_CONIFER 0
#define PROP_TREE 1      //broadleaf
#define PROP_ROCK 2

struct Prop
{
	float x,y,z;         //trees: the land under the trunk; rocks: the middle of their base
	float radius;        //trunk, or rock
	float height;        //above y: the trunk's top, or the rock's
	float scale;         //of the drawn mesh
	unsigned seed;       //drawing details (turn, tint, sway) from it
	unsigned char kind;  //PROP_*
	unsigned char variant;   //rocks: one of 3 shapes
};

//drawing details from the prop's seed, the same in Classic and Ultra: its turn
//about the vertical (radians), a tint and a sway phase, 0..1
inline float PropDetail(const Prop &p,int which)
{
	unsigned s=p.seed^(0x9e3779b9u*(unsigned)(which+1));
	s=s*1664525u+1013904223u;
	s^=s>>15;
	s*=0x2c1b3c6du;
	s^=s>>12;
	return (s>>8)/16777216.0f;
}
#define PROPTURN 0
#define PROPTINT 1
#define PROPPHASE 2

//every prop of a land of size x size heights (land[x*size+z], repeating every size;
//size a power of two), water level water, seed the land's random seed. avoid: x, z
//pairs (the map's scenery) kept free by PROPAVOID
#define PROPAVOID 3.0f
void PropsGenerate(const float *land,int size,float water,unsigned seed,const float *avoid,int numavoid,
	std::vector<Prop> &out);

//what the server and a client compare (PROPSMODE): the spots and kinds, from the
//hashes alone (not the heights, which a last-bit difference between two compilers
//could change without changing a single prop)
unsigned PropsHash(const std::vector<Prop> &props);

//a horizontal offset d of a coordinate in a world repeating every size, into [-size/2, size/2)
inline float PropWrap(float d,int size)
{
	float h=size*.5f;
	while(d>=h) d-=size;
	while(d<-h) d+=size;
	return d;
}

//the top of a prop at dx, dz (squared distance d2) from its axis, or below its base
//(y - 1) outside it. Rocks: a half ellipsoid (players walk onto the low ones);
//trees: their trunk, a cylinder
float PropTopAt(const Prop &p,float d2);

//does the segment a -> b (relative to the prop: a[0] = ax - p.x...) enter it? Then
//1 and *t, the fraction of the way where it does (0 if a is already inside)
int PropSegmentHit(const Prop &p,const float a[3],const float b[3],float *t);

//a vertical circle (radius r, from y0 to y1, at dx, dz from the prop's axis) inside
//the prop? 1 and the way out (*nx, *nz, horizontal unit vector away from the axis;
//*depth, how far to move)
int PropCircleOverlap(const Prop &p,float dx,float dz,float r,float y0,float y1,float *nx,float *nz,float *depth);

//a box (relative to the prop's axis on x and z) inside the prop's cylinder?
int PropBoxOverlap(const Prop &p,float x0,float y0,float z0,float x1,float y1,float z1);

//the props near a point: a grid of cells, each with the props that reach it
struct PropGrid
{
	int size,cells;
	float cell;
	std::vector<std::vector<int> > lists;
	void Build(const std::vector<Prop> &props,int worldsize);
	//the props within r of x, z (wrapped), at most max into out; returns their number
	int Near(float x,float z,float r,int *out,int max) const;
};

//---- the game's props (propsgame.cpp) ----

#include "vector.h"

void PropsBuild(unsigned seed);    //after CreateLand made the land and its scenery (seed: LandDefs randomseed)
void PropsSetEnabled(int on);      //the server's say (propsgame.cpp asks multiplay.cpp)
int PropsEnabled();
int PropsCount();
unsigned PropsLandHash();          //PropsHash of the land's props
const std::vector<Prop> &PropsList();
int PropThere(int i);              //on, and no block nor station on it
void PropsRefresh();               //blocks and stations moved: which props are there
float PropsHeight(const Vector &pos);   //the rocks' tops (GetHeight)
//a player (radius, feet to head) at pos moving by vel: pushed out of the trunks and
//rocks it walks into, its speed towards them removed. 1 if it touched one
int PropsPushPlayer(Vector &pos,Vector &vel,float radius,float feet,float head);
//a bullet going from a to b: 1 and *hit, where it stops, if a prop is in the way
int PropsSegmentHit(const Vector &a,const Vector &b,Vector *hit);
//a grenade that went from prev to pos into a trunk bounces off it (1)
int PropsBounce(Vector &pos,const Vector &prev,Vector &vel);
//the world box (min, max) of a block meets a prop
int PropsBoxBlocked(float x0,float y0,float z0,float x1,float y1,float z1);
//a spawn point at x, z keeps clear of the props
int PropsSpawnFree(float x,float z);

#endif
