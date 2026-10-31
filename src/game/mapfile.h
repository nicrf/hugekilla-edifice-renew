//map save files (roadmap M6): the land (SkySettings, LandDefs) and every block.
//Pure functions on memory buffers, without the game, so that they can be unit
//tested (tests/map_test.cpp) and fuzzed (tests/fuzz/). The game side (console
//savemap/loadmap, Server Setup "Map") is in hugekilla.cpp.
//
//Format 2, little-endian, written field by field (wire.h, landwire.h):
//	char magic[10] "EDIFICEMAP" | uint16 format (2) | uint16 protocol at save
//	time (informative) | uint16 BLOCKGRIDSIZE | uint16 BLOCKGRIDHEIGHT | uint16 NUMDEF
//	SkySettings (SyncSkySettings, 252 bytes) | LandDefs (SyncLandDefs, 5200 bytes)
//	block grid: runs of { uint32 count>=1 | uint8 drawside type top side bottom nextblock }
//	nextblock grid: runs of { uint32 count>=1 | uint8 value }
//	uint32 CRC-32 (zlib's) of every byte before it, then the end of the file.
//Both grids are visited layer by layer: y (0..BLOCKGRIDHEIGHT-1), then x, then z
//(fastest), and their runs add up to exactly MAPNUMBLOCKS. An empty world is a
//few hundred runs.
//
//Legacy format (the original game's Save, Windows 2000 build): "edifice" then the
//raw structs SkySettings, LandDefs, blocks[256][120][256] and nextblock[256][120][256]
//as MSVC 32-bit laid them out (natural alignment: LandDeformation is 32 bytes;
//the Debug build's /Zp1 packed it to 29), so the file has one of two exact sizes.

#ifndef MAPFILEH
#define MAPFILEH

#include <stddef.h>
#include <stdint.h>
#include "landwire.h"   //needs land.h before

#define MAPMAGIC "EDIFICEMAP"
#define MAPMAGICSIZE 10
#define MAPFORMATVERSION 2
#define MAPNUMBLOCKS (BLOCKGRIDSIZE*BLOCKGRIDHEIGHT*BLOCKGRIDSIZE)
#define MAPHEADERSIZE (MAPMAGICSIZE+5*2)
//largest valid format 2 file: one run per block in both grids
#define MAPMAXSIZE ((size_t)MAPHEADERSIZE+SKYSETTINGSSIZE+LANDDEFSSIZE+(size_t)MAPNUMBLOCKS*(4+6+4+1)+4)
#define LEGACYMAPSIZE ((size_t)7+252+5680+(size_t)MAPNUMBLOCKS*7)
#define LEGACYMAPSIZEPACKED ((size_t)7+252+5200+(size_t)MAPNUMBLOCKS*7)

//a map in memory; blocks and nextblock point to MAPNUMBLOCKS elements laid out
//like land.cpp's blocks[x][y][z] (index (x*BLOCKGRIDHEIGHT+y)*BLOCKGRIDSIZE+z)
struct MapData
{
	SkySettings sky;
	LandDefs defs;
	Block *blocks;
	char *nextblock;
	int format;     //set by ReadMapFile: 2, or 1 for an original save
	int protocol;   //EDIFICE_PROTOCOL_VERSION of the build that saved it (0: original)
};

//format 2 file of the map: a malloc'd buffer (free() it) and its size, 0 if out of memory
unsigned char *WriteMapFile(const MapData *map,size_t *size);

#define MAPREAD_NOCRC 1   //fuzzing only: don't check the CRC
//reads a format 2 or legacy file into map (whose blocks/nextblock must be
//allocated): 1 if valid, else 0 and a readable reason in err. Everything is
//checked: magic, version, exact size, grid sizes, run counts, block fields
//(ValidBlock, landwire.h); land counts and file names are bounded like a
//LANDINFO message (SanitizeLandInfo). On failure the grids hold garbage.
int ReadMapFile(const void *data,size_t size,MapData *map,char *err,int errsize,int flags=0);

uint32_t MapCrc32(const void *data,size_t size);

#endif
