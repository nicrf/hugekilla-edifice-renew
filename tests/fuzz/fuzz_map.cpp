// Map files (ReadMapFile, src/game/mapfile.cpp), any format: the input is
// the whole file. The CRC of format 2 is checked on a first read, then
// skipped (MAPREAD_NOCRC) so that the fuzzer reaches the structure checks.
// A map that is accepted saves again into a file that reads back the same.
// Original saves have a fixed size of ~55 MB: fuzz_map_legacy.cpp covers them.
#include "hktypes.h"   //DWORD
#include "vector.h"
#include "land.h"
#include "mapfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CHECK(c) do { if(!(c)) { fprintf(stderr,"%s:%d: fuzz check failed: %s\n",__FILE__,__LINE__,#c); abort(); } } while(0)

static MapData a,b;

static void Alloc(MapData *m)
{
	m->blocks=(Block *)malloc(sizeof(Block)*(size_t)MAPNUMBLOCKS);
	m->nextblock=(char *)malloc(MAPNUMBLOCKS);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(!a.blocks) { Alloc(&a); Alloc(&b); }
	char err[200];
	int withcrc=ReadMapFile(data,size,&a,err,sizeof(err));
	FUZZ_CHECK(withcrc||err[0]);
	if(!ReadMapFile(data,size,&a,err,sizeof(err),MAPREAD_NOCRC))
	{
		FUZZ_CHECK(!withcrc&&err[0]);
		return 0;
	}
	//what the game relies on (land.cpp, redrawing.cpp): checked and bounded
	FUZZ_CHECK(a.defs.numdefs>=0&&a.defs.numdefs<NUMDEF&&a.defs.numgroundtex>=0&&a.defs.numgroundtex<=9);
	for(size_t i=0;i<MAPNUMBLOCKS;i+=4099)
	{
		const Block &k=a.blocks[i];
		FUZZ_CHECK(k.type>=0&&k.type<=MAXBLOCKTYPE&&k.top>=0&&k.top<MAXBLOCKTEX&&k.nextblock>=0&&k.nextblock<=BLOCKGRIDHEIGHT);
	}
	size_t outsize;
	unsigned char *out=WriteMapFile(&a,&outsize);
	FUZZ_CHECK(out&&outsize<=MAPMAXSIZE);
	FUZZ_CHECK(ReadMapFile(out,outsize,&b,err,sizeof(err)));
	FUZZ_CHECK(!memcmp(a.blocks,b.blocks,sizeof(Block)*(size_t)MAPNUMBLOCKS));
	FUZZ_CHECK(!memcmp(a.nextblock,b.nextblock,MAPNUMBLOCKS));
	free(out);
	return 0;
}
