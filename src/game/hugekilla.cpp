//the land definitions, MakeClose and the map files (the entry point is main(), src/platform/platform_sdl.cpp)

#include "platform.h"
#include "fileio.h"
#include "hugekilla.h"
#include "gameplay.h"
#include "land.h"
#include "redrawing.h"
#include "vector.h"
#include "playing.h"
#include "multiplay.h"
#include "sound.h"
#include "ui.h"
#include "wire.h"
#include "landwire.h"
#include "mapfile.h"
#include <time.h>


LandDefs ldfs;

//wrappers

LandDefs *GetDefs()
{
	return &ldfs;
}

//general function used to make and object as close as possible to the player
//(since the world is repeating), and returns 1 if it is close enough to draw

int MakeClose(Vector *pos)
{
	Vector ppos=GetLocalPlayer()->pos;
	Vector pdir=GetLocalPlayer()->dir;
	if(pos->x<ppos.x-LANDGRIDSIZE/2)
		pos->x+=LANDGRIDSIZE;
	else
		if(pos->x>ppos.x+LANDGRIDSIZE/2)
			pos->x-=LANDGRIDSIZE;
	if(pos->z<ppos.z-LANDGRIDSIZE/2)
		pos->z+=LANDGRIDSIZE;
	else
		if(pos->z>ppos.z+LANDGRIDSIZE/2)
			pos->z-=LANDGRIDSIZE;
	if(fabs(pos->x-ppos.x)<graphicsprefs.drawdistance)
	if(fabs(pos->z-ppos.z)<graphicsprefs.drawdistance)
	{
		if(DotProduct(*pos-(ppos-pdir*2),pdir)>0)
			return 1;
	}
	return 0;
}


//---- map save files (roadmap M6, format in src/game/mapfile.h) ----

static char servermappath[1200];   //map the server plays (reloaded at each new round), "" = random land
static MapData *pendingmap;         //read by LoadMapPath, installed by StartServer

static void FreeMapData(MapData *map)
{
	if(!map) return;
	free(map->blocks);
	free(map->nextblock);
	free(map);
}

static MapData *NewMapData()
{
	MapData *map=(MapData *)calloc(1,sizeof(MapData));
	if(!map) return 0;
	map->blocks=(Block *)malloc(sizeof(Block)*(size_t)MAPNUMBLOCKS);
	map->nextblock=(char *)malloc(MAPNUMBLOCKS);
	if(!map->blocks||!map->nextblock)
	{
		FreeMapData(map);
		return 0;
	}
	return map;
}

int ValidMapName(const char *name)
{
	int len=0;
	for(;name[len];len++)
	{
		char c=name[len];
		if(!(c>='A'&&c<='Z'||c>='a'&&c<='z'||c>='0'&&c<='9'||c=='_'||c=='-')) return 0;
		if(len>=MAXMAPNAME) return 0;
	}
	return len>0;
}

//maps/<name>.map in the user folder
const char *MapPath(const char *name)
{
	static char path[1300];
	char rel[64];
	snprintf(rel,sizeof(rel),"maps/%s.map",name);
	snprintf(path,sizeof(path),"%s",UserFile(rel));
	return path;
}

//names (without .map) of the maps in the user maps folder, sorted
int ListSavedMaps(char (*names)[MAXMAPNAME+1],int max)
{
	std::vector<HKDirEntry> files=HKListDir(UserFile("maps"));
	int n=0;
	for(size_t i=0;i<files.size()&&n<max;i++)
	{
		std::string name=files[i].name;
		if(files[i].isdir||name.size()<5||!HKHasExt(name.c_str(),".map")) continue;
		name.resize(name.size()-4);
		if(!ValidMapName(name.c_str())) continue;
		strcpy(names[n++],name.c_str());
	}
	for(int i=1;i<n;i++)   //few files: insertion sort
		for(int j=i;j>0&&strcasecmp(names[j-1],names[j])>0;j--)
		{
			char tmp[MAXMAPNAME+1];
			strcpy(tmp,names[j]);
			strcpy(names[j],names[j-1]);
			strcpy(names[j-1],tmp);
		}
	return n;
}

//saves the land and the blocks; 1 if done, else 0 and a reason
int SaveMapPath(const char *path,char *err,int errsize)
{
	MapData map;
	map.sky=*GetSkySettings();
	map.defs=ldfs;
	map.blocks=&blocks[0][0][0];
	map.nextblock=&nextblock[0][0][0];
	size_t size;
	unsigned char *data=WriteMapFile(&map,&size);
	if(!data)
	{
		snprintf(err,errsize,"Out of memory");
		return 0;
	}
	//"wb" truncates: the original OPEN_ALWAYS left the end of a longer old file
	FILE *file=HKFileOpen(path,"wb");
	int ok=file&&fwrite(data,1,size,file)==size;
	if(file&&fclose(file)) ok=0;
	free(data);
	if(!ok) snprintf(err,errsize,"Can't write %s",path);
	return ok;
}

//reads and checks a map file: the map, or 0 and a reason
static MapData *ReadMapPath(const char *path,char *err,int errsize)
{
	FILE *file=HKFileOpen(path,"rb");
	if(!file)
	{
		snprintf(err,errsize,"No map file %s",path);
		return 0;
	}
	//size first: never allocate for a file no map can be
	long size=-1;
	if(!fseek(file,0,SEEK_END)) size=ftell(file);
	if(size<0||(size_t)size>MAPMAXSIZE||fseek(file,0,SEEK_SET))
	{
		fclose(file);
		snprintf(err,errsize,size<0?"Can't read %s":"Map file too large: %s",path);
		return 0;
	}
	unsigned char *data=(unsigned char *)malloc(size?size:1);
	MapData *map=NewMapData();
	int ok=data&&map&&fread(data,1,size,file)==(size_t)size;
	fclose(file);
	if(!ok) snprintf(err,errsize,data&&map?"Can't read %s":"Out of memory",path);
	else ok=ReadMapFile(data,size,map,err,errsize);
	free(data);
	if(!ok)
	{
		FreeMapData(map);
		return 0;
	}
	return map;
}

//replaces the world by the map. Clients get it like any land: LANDINFO, then all
//the blocks once they acknowledge it (multiplay.cpp, RECEIVEDLANDINFO)
static void InstallMap(MapData *map)
{
	ApplyLand(&map->defs,&map->sky);   //CreateLand clears the blocks
	memcpy(blocks,map->blocks,sizeof(blocks));
	memcpy(nextblock,map->nextblock,sizeof(nextblock));
	char text[200];
	snprintf(text,sizeof(text),map->format==1?"Loaded original save %s":"Loaded map %s",servermappath);
	ConsoleDisplayText(text);
}

//loads a map on the server, or starts a server with it, like the original
//Load(); refused while connected to someone else's server
int LoadMapPath(const char *path,char *err,int errsize)
{
	if(!IsServer()&&IsConnected())
	{
		snprintf(err,errsize,"Only the server can load a map");
		return 0;
	}
	MapData *map=ReadMapPath(path,err,errsize);
	if(!map) return 0;
	snprintf(servermappath,sizeof(servermappath),"%s",path);
	if(!IsServer())
	{
		FreeMapData(pendingmap);
		pendingmap=map;
		StartServer();   //installs pendingmap (StartServerMap)
		return 1;
	}
	InstallMap(map);
	FreeMapData(map);
	SendLandInfo(0);
	GRestartPlayers();
	return 1;
}

//StartServer: the map LoadMapPath read, if any (1), else a random land is made (0)
int StartServerMap()
{
	if(!pendingmap)
	{
		servermappath[0]=0;
		return 0;
	}
	InstallMap(pendingmap);
	FreeMapData(pendingmap);
	pendingmap=0;
	return 1;
}

//new round: the server's map again, with its original blocks (1), or 0 for a random land
int ReloadServerMap()
{
	if(!servermappath[0]) return 0;
	char err[300];
	MapData *map=ReadMapPath(servermappath,err,sizeof(err));
	if(!map)
	{
		ConsoleDisplayText(err);
		servermappath[0]=0;
		return 0;
	}
	InstallMap(map);
	FreeMapData(map);
	SendLandInfo(0);
	return 1;
}
