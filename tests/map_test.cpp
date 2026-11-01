// Unit test of the map save files (src/game/mapfile.h, roadmap M6): format 2
// round trip and exact header, refusal of corrupted, truncated and oversized
// files, sanitized land settings, and original saves (legacy format) built by
// hand from the original struct layout (git show 998563e:land.h, MSVC 32 bits).
//   map_test               runs the checks
//   map_test --write MAPDIR LEGACYDIR   also writes seeds for tests/fuzz/fuzz_map
//                                       and fuzz_map_legacy into these folders
#include "win32compat.h"   //DWORD
#include "vector.h"
#include "land.h"
#include "wire.h"
#include "landwire.h"
#include "mapfile.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

static size_t Cell(int x,int y,int z)
{
	return ((size_t)x*BLOCKGRIDHEIGHT+y)*BLOCKGRIDSIZE+z;
}

static void AllocMap(MapData *m)
{
	memset(m,0,sizeof(*m));
	m->blocks=(Block *)calloc(MAPNUMBLOCKS,sizeof(Block));
	m->nextblock=(char *)calloc(MAPNUMBLOCKS,1);
}

static void FreeMap(MapData *m)
{
	free(m->blocks);
	free(m->nextblock);
}

//a land like RandomLand/LoadSky make, an empty grid like CreateLand, and a few buildings
static void SampleMap(MapData *m,int withblocks)
{
	SkySettings &k=m->sky;
	k.sunheight=.7f; k.fogr=.5f; k.fogg=.6f; k.fogb=.9f;
	k.cloudvelx=.01f; k.cloudvely=-.02f; k.cloudmistvelx=.03f; k.cloudmistvely=.04f;
	k.sunskylight=1.5f; k.suneyelight=.8f; k.fogbegin=20;
	k.uselandclouds=1; k.lightning=0; k.usemist=1; k.precipitation=2;
	k.mistopacity=.4f; k.cloudheight=60;
	strcpy(k.cloudtex,"clouds2.bmp");
	strcpy(k.misttex,"mist.raw");
	strcpy(k.sound,"wind.wav");
	k.numlillypad=12;
	LandDefs &d=m->defs;
	d.numdefs=NUMDEF-1;
	for(int i=0;i<NUMDEF;i++)
	{
		LandDeformation &df=d.df[i];
		df.type=i%3;
		df.sx=(i*37)%LANDGRIDSIZE;
		df.sy=(i*91)%LANDGRIDSIZE;
		df.sh=10+i%30;
		df.sw=(3+i%14)*(3+i%14);
		df.sc=.1f+i*.001f;
		df.sd=3.7f+i*.01f;
		df.sf=.3f*(i%5);
	}
	d.numgroundtex=3;
	strcpy(d.groundtex[0],"grass.bmp");
	strcpy(d.groundtex[1],"rock.bmp");
	strcpy(d.groundtex[2],"sand.bmp");
	d.randomseed=0xDEADBEEF;
	d.numscenery=10;
	d.numblotch=150;
	for(int x=0;x<BLOCKGRIDSIZE;x++)
	for(int y=0;y<BLOCKGRIDHEIGHT;y++)
	for(int z=0;z<BLOCKGRIDSIZE;z++)
	{
		Block &b=m->blocks[Cell(x,y,z)];
		memset(&b,0,sizeof(b));
		b.nextblock=BLOCKGRIDHEIGHT-y;
	}
	if(!withblocks) return;
	for(int i=0;i<50;i++)   //a wall, a few ramps, faces and skip counts
	{
		Block &b=m->blocks[Cell(100+i%10,10+i/10,200)];
		b.type=1+i%5;
		b.top=i%10;
		b.side=(i*3)%10;
		b.bottom=9-i%10;
		b.drawside=(i*7)%64;
		b.nextblock=1+i%4;
	}
	m->blocks[Cell(255,119,255)].type=1;
	m->nextblock[Cell(3,4,5)]=7;
	m->nextblock[Cell(255,0,0)]=BLOCKGRIDHEIGHT;
}

static int SameSky(const SkySettings &a,const SkySettings &b)
{
	unsigned char x[SKYSETTINGSSIZE],y[SKYSETTINGSSIZE];
	SkySettings ca=a,cb=b;
	ByteWriter wa(x,sizeof(x)),wb(y,sizeof(y));
	SyncSkySettings(wa,ca);
	SyncSkySettings(wb,cb);
	return wa.Ok()&&wb.Ok()&&!memcmp(x,y,sizeof(x));
}

static int SameDefs(const LandDefs &a,const LandDefs &b)
{
	static unsigned char x[LANDDEFSSIZE],y[LANDDEFSSIZE];
	LandDefs ca=a,cb=b;
	ByteWriter wa(x,sizeof(x)),wb(y,sizeof(y));
	SyncLandDefs(wa,ca);
	SyncLandDefs(wb,cb);
	return wa.Ok()&&wb.Ok()&&!memcmp(x,y,sizeof(x));
}

static int SameGrids(const MapData &a,const MapData &b)
{
	return !memcmp(a.blocks,b.blocks,sizeof(Block)*(size_t)MAPNUMBLOCKS)&&
		!memcmp(a.nextblock,b.nextblock,MAPNUMBLOCKS);
}

static int Read(const unsigned char *data,size_t size,MapData *out,int flags=0)
{
	char err[200];
	int ok=ReadMapFile(data,size,out,err,sizeof(err),flags);
	if(!ok&&!err[0]) { printf("FAIL: refused without a reason\n"); failures++; }
	return ok;
}

//rewrites the CRC after an edit, so that only the structure check can refuse it
static void FixCrc(unsigned char *data,size_t size)
{
	WirePutU32(data+size-4,MapCrc32(data,size-4));
}

static void SaveSample(const char *dir,const char *name,const void *data,size_t size)
{
	char path[1000];
	snprintf(path,sizeof(path),"%s/%s",dir,name);
	FILE *f=fopen(path,"wb");
	if(!f||fwrite(data,1,size,f)!=size) { printf("FAIL: can't write %s\n",path); failures++; }
	if(f) fclose(f);
}

//---- original saves: raw MSVC 32-bit structs (natural alignment, or /Zp1) ----

static unsigned char *LegacyFile(const MapData *m,int packed,size_t *size)
{
	*size=packed?LEGACYMAPSIZEPACKED:LEGACYMAPSIZE;
	unsigned char *buf=(unsigned char *)malloc(*size);
	memset(buf,0xCC,*size);   //what MSVC Debug leaves in the padding
	unsigned char *p=buf;
	memcpy(p,"edifice",7); p+=7;
	const SkySettings &k=m->sky;
	//SkySettings: 11 floats, 4 ints, 2 floats at 0..67, names at 68/128/188, int at 248
	const float f1[11]={k.sunheight,k.fogr,k.fogg,k.fogb,k.cloudvelx,k.cloudvely,
		k.cloudmistvelx,k.cloudmistvely,k.sunskylight,k.suneyelight,k.fogbegin};
	for(int i=0;i<11;i++) { uint32_t u; memcpy(&u,&f1[i],4); WirePutU32(p+4*i,u); }
	WirePutU32(p+44,k.uselandclouds);
	WirePutU32(p+48,k.lightning);
	WirePutU32(p+52,k.usemist);
	WirePutU32(p+56,k.precipitation);
	uint32_t u;
	memcpy(&u,&k.mistopacity,4); WirePutU32(p+60,u);
	memcpy(&u,&k.cloudheight,4); WirePutU32(p+64,u);
	memset(p+68,0,180);
	strcpy((char *)p+68,k.cloudtex);
	strcpy((char *)p+128,k.misttex);
	strcpy((char *)p+188,k.sound);
	WirePutU32(p+248,k.numlillypad);
	p+=252;
	//LandDefs: numdefs, df[NUMDEF] (32 or 29 bytes), groundtex[9][60], 4 x 4 bytes
	const LandDefs &d=m->defs;
	int dfsize=packed?29:32;
	WirePutU32(p,d.numdefs);
	for(int i=0;i<NUMDEF;i++)
	{
		unsigned char *q=p+4+i*dfsize;
		const LandDeformation &df=d.df[i];
		q[0]=df.type;
		q+=packed?1:4;
		WirePutU32(q,df.sx);
		WirePutU32(q+4,df.sy);
		WirePutU32(q+8,df.sh);
		WirePutU32(q+12,df.sw);
		memcpy(&u,&df.sc,4); WirePutU32(q+16,u);
		memcpy(&u,&df.sd,4); WirePutU32(q+20,u);
		memcpy(&u,&df.sf,4); WirePutU32(q+24,u);
	}
	unsigned char *t=p+4+NUMDEF*dfsize;
	memset(t,0,540);
	for(int i=0;i<9;i++) strcpy((char *)t+60*i,d.groundtex[i]);
	WirePutU32(t+540,d.numgroundtex);
	WirePutU32(t+544,d.randomseed);
	WirePutU32(t+548,d.numscenery);
	WirePutU32(t+552,d.numblotch);
	p=t+556;
	CHECK((size_t)(p-buf)==(packed?7+252+5200:7+252+5680));
	//Block {char drawside,type,top,side,bottom,nextblock}[256][120][256], then nextblock[256][120][256]
	for(size_t i=0;i<MAPNUMBLOCKS;i++)
	{
		const Block &b=m->blocks[i];
		p[0]=b.drawside; p[1]=b.type; p[2]=b.top; p[3]=b.side; p[4]=b.bottom; p[5]=b.nextblock;
		p+=6;
	}
	memcpy(p,m->nextblock,MAPNUMBLOCKS);
	p+=MAPNUMBLOCKS;
	CHECK((size_t)(p-buf)==*size);
	return buf;
}

int main(int argc,char **argv)
{
	const char *seeddir=argc>3&&!strcmp(argv[1],"--write")?argv[2]:0;
	const char *legacydir=seeddir?argv[3]:0;
	MapData a,b;
	AllocMap(&a);
	AllocMap(&b);
	if(argc==3&&!strcmp(argv[1],"--check"))   //map_test --check FILE: is FILE a valid map?
	{
		FILE *f=fopen(argv[2],"rb");
		if(!f) { printf("can't open %s\n",argv[2]); return 1; }
		fseek(f,0,SEEK_END);
		size_t n=ftell(f);
		fseek(f,0,SEEK_SET);
		unsigned char *d=(unsigned char *)malloc(n+1);
		n=fread(d,1,n,f);
		fclose(f);
		char err[200];
		int ok=ReadMapFile(d,n,&a,err,sizeof(err));
		size_t built=0;
		for(size_t i=0;i<MAPNUMBLOCKS;i++) built+=a.blocks[i].type!=0;
		if(ok) printf("%s: valid map, format %d, protocol %d, %lu block(s)\n",argv[2],a.format,a.protocol,(unsigned long)built);
		else printf("%s: %s\n",argv[2],err);
		return ok?0:1;
	}

	// empty world: small file, exact header
	SampleMap(&a,0);
	size_t size;
	unsigned char *data=WriteMapFile(&a,&size);
	CHECK(data!=0);
	CHECK(size<MAPHEADERSIZE+SKYSETTINGSSIZE+LANDDEFSSIZE+BLOCKGRIDHEIGHT*10+5+4+1);
	CHECK(!memcmp(data,"EDIFICEMAP",10));
	CHECK(WireGetU16(data+10)==2);
	CHECK(WireGetU16(data+12)==EDIFICE_PROTOCOL_VERSION);
	CHECK(WireGetU16(data+14)==256&&WireGetU16(data+16)==120&&WireGetU16(data+18)==160);
	CHECK(WireGetU32(data+size-4)==MapCrc32(data,size-4));
	CHECK(Read(data,size,&b));
	CHECK(b.format==2&&b.protocol==EDIFICE_PROTOCOL_VERSION);
	CHECK(SameSky(a.sky,b.sky)&&SameDefs(a.defs,b.defs)&&SameGrids(a,b));
	if(seeddir) SaveSample(seeddir,"empty.map",data,size);
	free(data);

	// with buildings
	SampleMap(&a,1);
	data=WriteMapFile(&a,&size);
	CHECK(data!=0);
	memset(b.blocks,0x55,sizeof(Block)*(size_t)MAPNUMBLOCKS);
	CHECK(Read(data,size,&b));
	CHECK(SameSky(a.sky,b.sky)&&SameDefs(a.defs,b.defs)&&SameGrids(a,b));
	CHECK(b.blocks[Cell(100,10,200)].type==1&&b.blocks[Cell(255,119,255)].type==1);
	if(seeddir) SaveSample(seeddir,"buildings.map",data,size);
	printf("format 2: empty and built maps round trip (%lu bytes with buildings)\n",(unsigned long)size);

	// refused: every truncation, one byte more, any flipped byte (CRC), other magic and version
	unsigned char *copy=(unsigned char *)malloc(size+16);
	int refused=1;
	for(size_t n=0;n<size;n+=(n<64||n>size-64?1:97))
		if(Read(data,n,&b)) { refused=0; printf("FAIL: truncated to %lu accepted\n",(unsigned long)n); }
	CHECK(refused);
	memcpy(copy,data,size);
	copy[size]=0;
	CHECK(!Read(copy,size+1,&b));
	refused=1;
	for(size_t n=0;n<size;n+=(n<6000?7:31))
	{
		memcpy(copy,data,size);
		copy[n]^=0x20;
		if(Read(copy,size,&b)) { refused=0; printf("FAIL: byte %lu flipped accepted\n",(unsigned long)n); }
	}
	CHECK(refused);
	memcpy(copy,data,size);
	copy[0]='X';
	CHECK(!Read(copy,size,&b));
	memcpy(copy,data,size);
	WirePutU16(copy+10,3);   //a newer format
	FixCrc(copy,size);
	char err[200];
	CHECK(!ReadMapFile(copy,size,&b,err,sizeof(err)));
	CHECK(strstr(err,"newer")!=0);
	memcpy(copy,data,size);
	WirePutU16(copy+16,121);   //another world size
	FixCrc(copy,size);
	CHECK(!Read(copy,size,&b));

	// refused even with a right CRC: runs that don't add up, invalid fields
	size_t runs=MAPHEADERSIZE+SKYSETTINGSSIZE+LANDDEFSSIZE;
	memcpy(copy,data,size);
	WirePutU32(copy+runs,WireGetU32(copy+runs)+1);   //one block too many
	FixCrc(copy,size);
	CHECK(!Read(copy,size,&b));
	memcpy(copy,data,size);
	WirePutU32(copy+runs,0);   //empty run
	FixCrc(copy,size);
	CHECK(!Read(copy,size,&b));
	memcpy(copy,data,size);
	copy[runs+4+1]=6;   //type 6
	FixCrc(copy,size);
	CHECK(!Read(copy,size,&b));
	memcpy(copy,data,size);
	copy[runs+4+2]=MAXBLOCKTEX;   //texture out of blockbuf
	FixCrc(copy,size);
	CHECK(!Read(copy,size,&b));
	memcpy(copy,data,size);
	copy[runs+4+5]=BLOCKGRIDHEIGHT+1;   //nextblock past the top
	FixCrc(copy,size);
	CHECK(!Read(copy,size,&b));
	CHECK(Read(copy,size,&b,MAPREAD_NOCRC)==0);

	// oversized: refused before anything is read
	unsigned char *huge=(unsigned char *)calloc(MAPMAXSIZE+1,1);
	CHECK(huge!=0);
	if(huge)
	{
		memcpy(huge,data,MAPHEADERSIZE);
		CHECK(!ReadMapFile(huge,MAPMAXSIZE+1,&b,err,sizeof(err)));
		CHECK(strstr(err,"too large")!=0);
		free(huge);
	}
	free(data);

	// land settings bounded like LANDINFO (SanitizeLandInfo), names kept in textures/
	SampleMap(&a,0);
	a.defs.numdefs=100000;
	a.defs.numgroundtex=-3;
	a.defs.numblotch=1u<<30;
	a.defs.df[5].sw=1<<30;
	strcpy(a.defs.groundtex[1],"../../secret");
	strcpy(a.sky.cloudtex,"c:\\windows\\x.bmp");
	data=WriteMapFile(&a,&size);
	CHECK(Read(data,size,&b));
	CHECK(b.defs.numdefs==NUMDEF-1&&b.defs.numgroundtex==0&&b.defs.numblotch==2000&&b.defs.df[5].sw==LANDGRIDSIZE);
	CHECK(b.defs.groundtex[1][0]==0&&b.sky.cloudtex[0]==0&&!strcmp(b.defs.groundtex[0],"grass.bmp"));
	free(data);

	// original saves, natural layout (Release) and /Zp1 (Debug)
	SampleMap(&a,1);
	for(int packed=0;packed<2;packed++)
	{
		unsigned char *legacy=LegacyFile(&a,packed,&size);
		memset(b.blocks,0x55,sizeof(Block)*(size_t)MAPNUMBLOCKS);
		CHECK(Read(legacy,size,&b));
		CHECK(b.format==1&&b.protocol==0);
		CHECK(SameSky(a.sky,b.sky)&&SameDefs(a.defs,b.defs)&&SameGrids(a,b));
		CHECK(!Read(legacy,size-1,&b));   //size must match exactly
		legacy[size-1]=(char)-1;          //nextblock -1
		CHECK(!Read(legacy,size,&b));
		legacy[size-1]=0;
		legacy[7+252+(packed?5200:5680)+6*Cell(100,10,200)+1]=9;   //type 9
		CHECK(!Read(legacy,size,&b));
		if(legacydir)   //fuzz_map_legacy.cpp: layout byte, then the start of the file
		{
			unsigned char *head=legacy+6;
			head[0]=packed;
			SaveSample(legacydir,packed?"packed-head.bin":"natural-head.bin",head,1+252+(packed?5200:5680)+6*1000);
		}
		free(legacy);
	}
	printf("legacy: natural and packed layouts read, wrong sizes and fields refused\n");

	free(copy);
	FreeMap(&a);
	FreeMap(&b);
	if(failures) printf("%d failure(s)\n",failures);
	else printf("all map tests passed\n");
	return failures?1:0;
}
