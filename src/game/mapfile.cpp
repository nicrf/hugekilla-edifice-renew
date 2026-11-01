//map save files: format 2 writer and reader, legacy reader (see mapfile.h)

#include "hktypes.h"   //DWORD
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "vector.h"
#include "land.h"
#include "version.h"
#include "wire.h"
#include "landwire.h"
#include "mapfile.h"

uint32_t MapCrc32(const void *data,size_t size)
{
	static uint32_t table[256];
	if(!table[1])
		for(uint32_t i=0;i<256;i++)
		{
			uint32_t c=i;
			for(int k=0;k<8;k++) c=c&1?0xEDB88320u^(c>>1):c>>1;
			table[i]=c;
		}
	const unsigned char *p=(const unsigned char *)data;
	uint32_t crc=0xFFFFFFFFu;
	for(size_t i=0;i<size;i++) crc=table[(crc^p[i])&0xff]^(crc>>8);
	return crc^0xFFFFFFFFu;
}

//index of the n-th cell in file order (y, then x, then z)
static inline size_t CellIndex(size_t n)
{
	size_t z=n%BLOCKGRIDSIZE;
	size_t x=n/BLOCKGRIDSIZE%BLOCKGRIDSIZE;
	size_t y=n/((size_t)BLOCKGRIDSIZE*BLOCKGRIDSIZE);
	return (x*BLOCKGRIDHEIGHT+y)*BLOCKGRIDSIZE+z;
}

static inline int SameBlock(const Block &a,const Block &b)
{
	return a.drawside==b.drawside&&a.type==b.type&&a.top==b.top&&
		a.side==b.side&&a.bottom==b.bottom&&a.nextblock==b.nextblock;
}

//calls run(count,first cell index) for each run of equal blocks (grid 0) or
//nextblock values (grid 1), in file order; returns the number of runs
template<class F> static size_t ForEachRun(const MapData *map,int grid,F run)
{
	size_t runs=0;
	size_t n=0;
	while(n<MAPNUMBLOCKS)
	{
		size_t first=CellIndex(n);
		size_t len=1;
		while(n+len<MAPNUMBLOCKS&&len<0xFFFFFFFFu)
		{
			size_t c=CellIndex(n+len);
			if(grid==0?!SameBlock(map->blocks[c],map->blocks[first]):map->nextblock[c]!=map->nextblock[first])
				break;
			len++;
		}
		run(len,first);
		runs++;
		n+=len;
	}
	return runs;
}

struct RunCounter
{
	void operator()(size_t,size_t) {}
};

struct BlockRunWriter
{
	ByteWriter *w;
	const MapData *map;
	void operator()(size_t len,size_t first)
	{
		const Block &b=map->blocks[first];
		w->U32((uint32_t)len);
		w->U8((unsigned char)b.drawside);
		w->U8((unsigned char)b.type);
		w->U8((unsigned char)b.top);
		w->U8((unsigned char)b.side);
		w->U8((unsigned char)b.bottom);
		w->U8((unsigned char)b.nextblock);
	}
};

struct NextRunWriter
{
	ByteWriter *w;
	const MapData *map;
	void operator()(size_t len,size_t first)
	{
		w->U32((uint32_t)len);
		w->U8((unsigned char)map->nextblock[first]);
	}
};

unsigned char *WriteMapFile(const MapData *map,size_t *size)
{
	*size=0;
	size_t blockruns=ForEachRun(map,0,RunCounter());
	size_t nextruns=ForEachRun(map,1,RunCounter());
	size_t total=MAPHEADERSIZE+SKYSETTINGSSIZE+LANDDEFSSIZE+blockruns*10+nextruns*5+4;
	unsigned char *buf=(unsigned char *)malloc(total);
	if(!buf) return 0;
	ByteWriter w(buf,(int)total);
	w.Bytes(MAPMAGIC,MAPMAGICSIZE);
	w.U16(MAPFORMATVERSION);
	w.U16(EDIFICE_PROTOCOL_VERSION);
	w.U16(BLOCKGRIDSIZE);
	w.U16(BLOCKGRIDHEIGHT);
	w.U16(NUMDEF);
	//the Sync templates take references to non-const fields
	SkySettings sky=map->sky;
	LandDefs defs=map->defs;
	SyncSkySettings(w,sky);
	SyncLandDefs(w,defs);
	BlockRunWriter bw={&w,map};
	ForEachRun(map,0,bw);
	NextRunWriter nw={&w,map};
	ForEachRun(map,1,nw);
	w.U32(MapCrc32(buf,w.Size()));
	if(!w.Ok()||(size_t)w.Size()!=total)
	{
		free(buf);
		return 0;
	}
	*size=total;
	return buf;
}

static int Fail(char *err,int errsize,const char *text)
{
	if(errsize>0) snprintf(err,errsize,"%s",text);
	return 0;
}

static int ValidCell(int drawside,int type,int top,int side,int bottom,int nextblock)
{
	return drawside>=0&&drawside<64&&nextblock>=0&&nextblock<=BLOCKGRIDHEIGHT&&
		ValidBlock(type,top,side,bottom);
}

static int ReadMapV2(ByteReader &r,MapData *map,char *err,int errsize)
{
	if(r.U16()!=BLOCKGRIDSIZE||r.U16()!=BLOCKGRIDHEIGHT||r.U16()!=NUMDEF)
		return Fail(err,errsize,"Map made for another world size");
	SyncSkySettings(r,map->sky);
	SyncLandDefs(r,map->defs);
	size_t n=0;
	while(n<MAPNUMBLOCKS)
	{
		uint32_t len=r.U32();
		int drawside=r.U8(),type=r.U8(),top=r.U8(),side=r.U8(),bottom=r.U8(),next=r.U8();
		if(!r.Ok()) return Fail(err,errsize,"Truncated map file");
		if(len==0||len>MAPNUMBLOCKS-n) return Fail(err,errsize,"Corrupted map file (block count)");
		if(!ValidCell(drawside,type,top,side,bottom,next)) return Fail(err,errsize,"Corrupted map file (invalid block)");
		Block b;
		b.drawside=(char)drawside;
		b.type=(char)type;
		b.top=(char)top;
		b.side=(char)side;
		b.bottom=(char)bottom;
		b.nextblock=(char)next;
		for(uint32_t i=0;i<len;i++) map->blocks[CellIndex(n+i)]=b;
		n+=len;
	}
	n=0;
	while(n<MAPNUMBLOCKS)
	{
		uint32_t len=r.U32();
		int v=r.U8();
		if(!r.Ok()) return Fail(err,errsize,"Truncated map file");
		if(len==0||len>MAPNUMBLOCKS-n) return Fail(err,errsize,"Corrupted map file (nextblock count)");
		if(v>BLOCKGRIDHEIGHT) return Fail(err,errsize,"Corrupted map file (invalid nextblock)");
		for(uint32_t i=0;i<len;i++) map->nextblock[CellIndex(n+i)]=(char)v;
		n+=len;
	}
	r.U32();   //CRC, checked by the caller
	if(!r.Ok()) return Fail(err,errsize,"Truncated map file");
	if(r.Left()) return Fail(err,errsize,"Corrupted map file (data after the end)");
	return 1;
}

//LandDefs as the original build wrote it: packed (/Zp1, = SyncLandDefs) or
//natural alignment (3 bytes after LandDeformation::type)
static void ReadLegacyLandDefs(ByteReader &r,LandDefs &d,int packed)
{
	if(packed)
	{
		SyncLandDefs(r,d);
		return;
	}
	d.numdefs=r.I32();
	for(int i=0;i<NUMDEF;i++)
	{
		LandDeformation &df=d.df[i];
		df.type=(char)r.U8();
		r.Skip(3);
		df.sx=r.I32();
		df.sy=r.I32();
		df.sh=r.I32();
		df.sw=r.I32();
		df.sc=r.F32();
		df.sd=r.F32();
		df.sf=r.F32();
	}
	for(int t=0;t<9;t++) r.Str(d.groundtex[t],sizeof(d.groundtex[t]));
	d.numgroundtex=r.I32();
	d.randomseed=r.U32();
	d.numscenery=r.U32();
	d.numblotch=r.U32();
}

static int ReadMapLegacy(ByteReader &r,MapData *map,int packed,char *err,int errsize)
{
	SyncSkySettings(r,map->sky);   //no padding in SkySettings
	ReadLegacyLandDefs(r,map->defs,packed);
	if(!r.Ok()||(size_t)r.Left()!=(size_t)MAPNUMBLOCKS*7)
		return Fail(err,errsize,"Truncated map file");
	//blocks[x][y][z] then nextblock[x][y][z], in memory order
	const unsigned char *p=r.Here();
	for(size_t i=0;i<MAPNUMBLOCKS;i++,p+=6)
	{
		if(!ValidCell(p[0],(signed char)p[1],(signed char)p[2],(signed char)p[3],(signed char)p[4],(signed char)p[5]))
			return Fail(err,errsize,"Corrupted map file (invalid block)");
		Block &b=map->blocks[i];
		b.drawside=(char)p[0];
		b.type=(char)p[1];
		b.top=(char)p[2];
		b.side=(char)p[3];
		b.bottom=(char)p[4];
		b.nextblock=(char)p[5];
	}
	for(size_t i=0;i<MAPNUMBLOCKS;i++,p++)
	{
		if((signed char)*p<0||*p>BLOCKGRIDHEIGHT)
			return Fail(err,errsize,"Corrupted map file (invalid nextblock)");
		map->nextblock[i]=(char)*p;
	}
	return 1;
}

int ReadMapFile(const void *data,size_t size,MapData *map,char *err,int errsize,int flags)
{
	if(errsize>0) err[0]=0;
	map->format=0;
	map->protocol=0;
	const unsigned char *p=(const unsigned char *)data;
	int ok;
	if(size>=MAPMAGICSIZE&&!memcmp(p,MAPMAGIC,MAPMAGICSIZE))
	{
		if(size<MAPHEADERSIZE) return Fail(err,errsize,"Truncated map file");
		int format=WireGetU16(p+MAPMAGICSIZE);
		if(format!=MAPFORMATVERSION)
		{
			char text[100];
			snprintf(text,sizeof(text),format>MAPFORMATVERSION?"Map format %d: made by a newer version":"Unknown map format %d",format);
			return Fail(err,errsize,text);
		}
		if(size>MAPMAXSIZE) return Fail(err,errsize,"Map file too large");
		if(size<MAPHEADERSIZE+SKYSETTINGSSIZE+LANDDEFSSIZE+10+5+4) return Fail(err,errsize,"Truncated map file");
		if(!(flags&MAPREAD_NOCRC)&&MapCrc32(p,size-4)!=WireGetU32(p+size-4))
			return Fail(err,errsize,"Corrupted map file (checksum)");
		ByteReader r(p,(int)size);
		r.Skip(MAPMAGICSIZE+2);
		map->protocol=r.U16();
		ok=ReadMapV2(r,map,err,errsize);
		map->format=MAPFORMATVERSION;
	}
	else if(size>=7&&!memcmp(p,"edifice",7))
	{
		if(size!=LEGACYMAPSIZE&&size!=LEGACYMAPSIZEPACKED)
		{
			char text[100];
			snprintf(text,sizeof(text),"Original save of unexpected size (%lu bytes, expected %lu)",
				(unsigned long)size,(unsigned long)LEGACYMAPSIZE);
			return Fail(err,errsize,text);
		}
		ByteReader r(p,(int)size);
		r.Skip(7);
		ok=ReadMapLegacy(r,map,size==LEGACYMAPSIZEPACKED,err,errsize);
		map->format=1;
	}
	else
		return Fail(err,errsize,"Not an Edifice map");
	if(!ok) return 0;
	SanitizeLandInfo(&map->defs,&map->sky);
	return 1;
}
