// SIMPLE message (ReadSimple, src/net/sock.cpp): 12 bytes, re-encoded the same
// (type2, msg, msg2; the type byte and the 2 padding bytes aren't read).
#include "fuzz.h"
#include "sock.h"

uint32_t HKTicks() { return 0; }   //system_sdl.cpp isn't linked: a fixed clock

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>BIGBUFSIZE) return 0;
	SimpleMessage m;
	if(!ReadSimple(data,(int)size,&m)) return 0;
	FUZZ_CHECK(size>=SIMPLESIZE);
	unsigned char out[SIMPLESIZE];
	FUZZ_CHECK(WriteSimple(out,m.type2,m.msg,m.msg2)==SIMPLESIZE);
	FUZZ_CHECK(out[0]==SIMPLE&&out[1]==data[1]&&!memcmp(out+4,data+4,8));
	return 0;
}
