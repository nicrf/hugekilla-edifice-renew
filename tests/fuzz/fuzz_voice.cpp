// VOICE (ReadVoice, src/net/voicemsg.h): an accepted message is exactly the
// header and its frame, and re-encodes to the same bytes.
#include "voicemsg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CHECK(c) do { if(!(c)) { fprintf(stderr,"%s:%d: fuzz check failed: %s\n",__FILE__,__LINE__,#c); abort(); } } while(0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>100000) return 0;
	VoiceMessage v;
	if(!ReadVoice(data,(int)size,&v)) return 0;
	FUZZ_CHECK(v.len>=1&&v.len<=VOICEMAXFRAME&&size==(size_t)VOICEHEADERSIZE+v.len);
	unsigned char out[VOICEMSGSIZE];
	int n=WriteVoice(out,sizeof(out),&v);
	FUZZ_CHECK(n==(int)size&&!memcmp(out,data,size));
	return 0;
}
