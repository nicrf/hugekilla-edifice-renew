//hugekilla.cpp: the land of the game and the map files (the window and input are in
//src/platform/platform.h)

#ifndef HUGEKILLAH
#define HUGEKILLAH

#include "vector.h"
#include "land.h"

extern LandDefs ldfs;       //the current land (sent in LANDINFO, saved in map files)
LandDefs *GetDefs();
int MakeClose(Vector *pos);

//map save files (maps/<name>.map in the user folder, format in mapfile.h)
#define MAXMAPNAME 32
int ValidMapName(const char *name);   //[A-Za-z0-9_-]{1,32}
const char *MapPath(const char *name);
int ListSavedMaps(char (*names)[MAXMAPNAME+1],int max);
int SaveMapPath(const char *path,char *err,int errsize);
int LoadMapPath(const char *path,char *err,int errsize);
int StartServerMap();
int ReloadServerMap();

#endif
