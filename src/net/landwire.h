//explicit serialization of the land (roadmap M6): LandDefs, SkySettings and
//block fields, shared by the LANDINFO and BLOCKS messages (netmsg.h,
//multiplay.cpp) and the map save files (src/game/mapfile.cpp). Also the
//validation applied to them whether they come from the network or a file:
//both are untrusted.

#ifndef LANDWIREH
#define LANDWIREH

#include "land.h"
#include "wire.h"

//LandDefs: int32 numdefs, NUMDEF deformations of 29 bytes (int8 type, int32
//sx sy sh sw, f32 sc sd sf), 9 texture names of 60 bytes, int32 numgroundtex,
//uint32 randomseed numscenery numblotch
template<class IO> inline void SyncLandDefs(IO &io,LandDefs &d)
{
	io.i32(d.numdefs);
	for(int i=0;i<NUMDEF;i++)
	{
		LandDeformation &df=d.df[i];
		io.i8(df.type);
		io.i32(df.sx);
		io.i32(df.sy);
		io.i32(df.sh);
		io.i32(df.sw);
		io.f32(df.sc);
		io.f32(df.sd);
		io.f32(df.sf);
	}
	for(int t=0;t<9;t++) io.str(d.groundtex[t],sizeof(d.groundtex[t]));
	io.i32(d.numgroundtex);
	io.u32(d.randomseed);
	io.u32(d.numscenery);
	io.u32(d.numblotch);
}
#define LANDDEFSSIZE (4+NUMDEF*29+9*60+4*4)

//SkySettings: the fields in declaration order (17 of 4 bytes, 3 names of 60, int32)
template<class IO> inline void SyncSkySettings(IO &io,SkySettings &k)
{
	io.f32(k.sunheight);
	io.f32(k.fogr);
	io.f32(k.fogg);
	io.f32(k.fogb);
	io.f32(k.cloudvelx);
	io.f32(k.cloudvely);
	io.f32(k.cloudmistvelx);
	io.f32(k.cloudmistvely);
	io.f32(k.sunskylight);
	io.f32(k.suneyelight);
	io.f32(k.fogbegin);
	io.i32(k.uselandclouds);
	io.i32(k.lightning);
	io.i32(k.usemist);
	io.i32(k.precipitation);
	io.f32(k.mistopacity);
	io.f32(k.cloudheight);
	io.str(k.cloudtex,sizeof(k.cloudtex));
	io.str(k.misttex,sizeof(k.misttex));
	io.str(k.sound,sizeof(k.sound));
	io.i32(k.numlillypad);
}
#define SKYSETTINGSSIZE (17*4+3*60+4)
static_assert(sizeof(SkySettings)==SKYSETTINGSSIZE,"SkySettings changed: update SyncSkySettings, bump the protocol and the map format");

//keeps a file name received from the network (or read from a map) inside the textures folder
inline void SanitizeNetFileName(char *name,int size)
{
	name[size-1]=0;
	if(strstr(name,"..")||strchr(name,'/')||strchr(name,'\\')||strchr(name,':'))
		name[0]=0;
}

//a float the land builder can use (finite, not huge), else 0
inline void SanitizeLandFloat(float &f)
{
	if(!(f>-1e6f&&f<1e6f)) f=0;   //also NaN
}

//bounds everything the land builder (CreateLand, AddRidge) uses as a count, a
//size, a position or a file name. Legitimate values (RandomLand, land.ini,
//skies.ini) are never changed.
inline void SanitizeLandInfo(LandDefs *ld,SkySettings *ss)
{
	if(ld->numdefs<0) ld->numdefs=0;
	if(ld->numdefs>NUMDEF-1) ld->numdefs=NUMDEF-1;
	for(int i=0;i<NUMDEF;i++)
	{
		LandDeformation &df=ld->df[i];
		//AddRidge loops over (2*sw)^2 points around (sx,sy)
		if(df.sw<0) df.sw=0;
		if(df.sw>LANDGRIDSIZE) df.sw=LANDGRIDSIZE;
		if(df.sx<0||df.sx>LANDGRIDSIZE) df.sx=0;
		if(df.sy<0||df.sy>LANDGRIDSIZE) df.sy=0;
		if(df.sh<-100000||df.sh>100000) df.sh=0;
		SanitizeLandFloat(df.sc);
		SanitizeLandFloat(df.sd);
		SanitizeLandFloat(df.sf);
	}
	if(ld->numgroundtex<0) ld->numgroundtex=0;
	if(ld->numgroundtex>9) ld->numgroundtex=9;
	for(int t=0;t<9;t++) SanitizeNetFileName(ld->groundtex[t],sizeof(ld->groundtex[t]));
	if(ld->numscenery>1000) ld->numscenery=1000;
	if(ld->numblotch>2000) ld->numblotch=2000;
	SanitizeNetFileName(ss->cloudtex,sizeof(ss->cloudtex));
	SanitizeNetFileName(ss->misttex,sizeof(ss->misttex));
	SanitizeNetFileName(ss->sound,sizeof(ss->sound));
}

//block fields the renderer can use: type 0 (empty) to 5 (the four ramps),
//textures 0-31 (redrawing.cpp sorts faces into blockbuf[32])
#define MAXBLOCKTYPE 5
#define MAXBLOCKTEX 32
inline int ValidBlock(int type,int top,int side,int bottom)
{
	return type>=0&&type<=MAXBLOCKTYPE&&top>=0&&top<MAXBLOCKTEX&&
		side>=0&&side<MAXBLOCKTEX&&bottom>=0&&bottom<MAXBLOCKTEX;
}

#endif
