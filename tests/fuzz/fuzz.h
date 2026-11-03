// Shared by the libFuzzer harnesses of tests/fuzz (roadmap M6). Each harness
// feeds one parser of network or file input with arbitrary bytes; a crash, an
// AddressSanitizer/UBSan report or a failed FUZZ_CHECK is a bug. Built with
// -DEDIFICE_FUZZ=ON (see BUILD.md); without libFuzzer, fuzz_main.cpp runs the
// harness over files or folders (the seed corpora in tests/fuzz/corpus).
#ifndef FUZZH
#define FUZZH

#include "multiplay.h"   //the game's types (DWORD, Vector, LandDefs, ServerVars...)
#include "multiplaystruct.h"
#include "netmsg.h"
#include "netsync.h"     //M16: INPUTCMD, SNAPSHOT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CHECK(c) do { if(!(c)) { fprintf(stderr,"%s:%d: fuzz check failed: %s\n",__FILE__,__LINE__,#c); abort(); } } while(0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);

//M16 phase 4: the base the deltas of fuzz_playersnapshot are read on (its seeds,
//make_seeds.cpp, are written on it): players 0, 1, 5 and 31
inline void FuzzSnapBase(SnapPlayers *base)
{
	memset(base,0,sizeof(*base));
	SnapState st;
	memset(&st,0,sizeof(st));
	st.playstate=10;
	const int players[4]={0,1,5,31};
	for(int k=0;k<4;k++)
	{
		st.pos[0]=100.f*k+.5f;
		st.pos[1]=30;
		st.pos[2]=511.9f;
		st.vel[1]=-1.5f;
		st.yaw=(float)k;
		st.keys=MOVEKEY_UP;
		st.weapon=(unsigned char)k;
		SnapPack(st,&base->p[players[k]]);
	}
}

#endif
