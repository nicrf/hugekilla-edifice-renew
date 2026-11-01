// Original saves (ReadMapFile, legacy format, src/game/mapfile.cpp). They have
// one of two exact sizes (~55 MB), more than a fuzzer input: the input is
// uint8 layout (bit 0: /Zp1 packed, bit 1: one byte short) then the start of
// the file after "edifice" (SkySettings, LandDefs, first blocks); the rest of
// the file is zero (empty blocks).
#include "windows.h"
#include "vector.h"
#include "land.h"
#include "mapfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CHECK(c) do { if(!(c)) { fprintf(stderr,"%s:%d: fuzz check failed: %s\n",__FILE__,__LINE__,#c); abort(); } } while(0)

static MapData map;
static unsigned char *file;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(!file)
	{
		file=(unsigned char *)calloc(LEGACYMAPSIZE,1);
		map.blocks=(Block *)malloc(sizeof(Block)*(size_t)MAPNUMBLOCKS);
		map.nextblock=(char *)malloc(MAPNUMBLOCKS);
	}
	if(size<1) return 0;
	int packed=data[0]&1;
	size_t filesize=(packed?LEGACYMAPSIZEPACKED:LEGACYMAPSIZE)-(data[0]&2?1:0);
	size_t n=size-1;
	if(n>filesize-7) n=filesize-7;
	memcpy(file,"edifice",7);
	memcpy(file+7,data+1,n);
	char err[200];
	int ok=ReadMapFile(file,filesize,&map,err,sizeof(err));
	FUZZ_CHECK(ok||err[0]);
	if(data[0]&2) FUZZ_CHECK(!ok);   //wrong size
	if(ok)
	{
		FUZZ_CHECK(map.format==1);
		FUZZ_CHECK(map.defs.numdefs>=0&&map.defs.numdefs<NUMDEF);
		FUZZ_CHECK(!strstr(map.sky.cloudtex,"..")&&!strchr(map.defs.groundtex[0],'/'));
	}
	memset(file+7,0,n);
	return 0;
}
