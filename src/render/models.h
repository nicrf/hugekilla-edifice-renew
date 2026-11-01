//the game's 3D models (.cob), loaded by textures.cpp (LoadGuns, LoadScenery...).
//Apart from redrawing.h: land.cpp has its own static "scenery" (the placed objects).

#ifndef MODELSH
#define MODELSH

#include "ase.h"
#include "redrawing.h"   //MAXSCENERY, GunDrawInfo

//redrawing.cpp
extern int numscenery;
extern AseObj scenery[MAXSCENERY];
extern AseObj plobj[4];
extern AseObj projectile[10];   //0 rocket, 1 round, 2 grenade
extern AseObj servercobs[50][3];   //[server object type][team]
extern AseObj hangglider;
extern GunDrawInfo gun[10];
//textures.cpp
extern AseObj torso[2][10];
extern AseObj leg[5];

#endif
