// SNAPSHOT with the other players (M16 phase 4, src/net/netsync.h), as a client
// reads it: complete (basetick 0) or as a delta on the snapshot it names, here
// always the same base of four players (FuzzSnapBase, fuzz.h). What it decodes is
// canonical (a player not in it is all zeros), is re-encoded on the same base in no
// more bytes than it read, reads back the same, and that encoding is stable; written complete it
// reads back the same too. Then the client draws the others from it (SnapSample):
// never a number that isn't one, never out of the ring.
#include "fuzz.h"
#include <math.h>

static SnapshotMessage m1,m2;
static SnapPlayers base,p1,p2,p3;
static unsigned char out1[BIGBUFSIZE],out2[BIGBUFSIZE];
static SnapRing ring;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>BIGBUFSIZE) return 0;   //Sock::Receive never returns more
	static int made;
	if(!made) FuzzSnapBase(&base);
	made=1;
	SnapshotMessage head;
	if(!ReadSnapshotSelf(data,(int)size,&head)) return 0;
	const SnapPlayers *b=head.basetick?&base:0;   //the base it names, kept by the client
	if(!ReadPlayerSnapshot(data,(int)size,&m1,&p1,b)) return 0;
	static const SnapPlayer none={};
	for(int i=0;i<SNAPMAXPLAYERS;i++)
		FUZZ_CHECK(p1.p[i].in==1||!memcmp(&p1.p[i],&none,sizeof(none)));
	int n1=WritePlayerSnapshot(out1,sizeof(out1),&m1,p1,b);
	FUZZ_CHECK(n1>0&&(size_t)n1<=size);
	FUZZ_CHECK(ReadPlayerSnapshot(out1,n1,&m2,&p2,b));
	FUZZ_CHECK(!memcmp(&p1,&p2,sizeof(p1)));
	int n2=WritePlayerSnapshot(out2,sizeof(out2),&m2,p2,b);
	FUZZ_CHECK(n2==n1&&!memcmp(out1,out2,n1));
	FUZZ_CHECK(!ReadPlayerSnapshot(out1,n1-1,&m2,&p2,b));   //one byte short is refused
	// complete, on no base
	m2=m1;
	m2.basetick=0;
	int n3=WritePlayerSnapshot(out2,sizeof(out2),&m2,p1,0);
	FUZZ_CHECK(n3>0&&ReadPlayerSnapshot(out2,n3,&m2,&p3,0)&&!memcmp(&p1,&p3,sizeof(p1)));
	// drawn from: the base, then this one two ticks later
	memset(&ring,0,sizeof(ring));
	SnapStore(&ring,m1.tick-2,base);
	SnapStore(&ring,m1.tick,p1);
	const float at[5]={-3,-1.5f,0,.5f,20};
	for(int i=0;i<SNAPMAXPLAYERS;i++)
		for(int k=0;k<5;k++)
		{
			float t=at[k];
			float whole=floorf(t);
			SnapState st;
			int kind=SnapSample(&ring,i,m1.tick+(uint32_t)(int32_t)whole,t-whole,&st);
			FUZZ_CHECK(kind>=SNAPSAMPLE_NONE&&kind<=SNAPSAMPLE_EXTRAP);
			if(kind==SNAPSAMPLE_NONE) continue;
			for(int c=0;c<3;c++) FUZZ_CHECK(isfinite(st.pos[c])&&isfinite(st.vel[c]));
			FUZZ_CHECK(isfinite(st.yaw)&&isfinite(st.pitch)&&isfinite(st.roll));
		}
	return 0;
}
