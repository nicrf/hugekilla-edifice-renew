// One harness per message of src/net/netmsg.h made with WIREPAIR, chosen at
// build time (CMakeLists.txt): FUZZ_NAME is the Read/Write suffix, FUZZ_TYPE
// the decoded struct. Read never reads out of the message; what it decodes
// is re-encoded in no more bytes than it read, and that encoding is stable.
#include "fuzz.h"

#define FUZZCAT2(a,b) a##b
#define FUZZCAT(a,b) FUZZCAT2(a,b)
#define FUZZREAD FUZZCAT(Read,FUZZ_NAME)
#define FUZZWRITE FUZZCAT(Write,FUZZ_NAME)

static FUZZ_TYPE m1,m2;
static unsigned char out1[BIGBUFSIZE],out2[BIGBUFSIZE];

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>BIGBUFSIZE) return 0;   //Sock::Receive never returns more
	if(!FUZZREAD(data,(int)size,&m1)) return 0;
#ifdef FUZZ_LANDINFO
	//what the client does before building the land (multiplay.cpp, LANDINFO)
	SanitizeLandInfo(&m1.ldfs,&m1.skysets);
	FUZZ_CHECK(m1.ldfs.numdefs>=0&&m1.ldfs.numdefs<NUMDEF);
	FUZZ_CHECK(m1.ldfs.numgroundtex>=0&&m1.ldfs.numgroundtex<=9);
	for(int t=0;t<9;t++) FUZZ_CHECK(memchr(m1.ldfs.groundtex[t],0,60)&&!strstr(m1.ldfs.groundtex[t],".."));
	for(int i=0;i<NUMDEF;i++) FUZZ_CHECK(m1.ldfs.df[i].sw>=0&&m1.ldfs.df[i].sw<=LANDGRIDSIZE);
	FUZZ_CHECK(!strchr(m1.skysets.cloudtex,'/')&&!strchr(m1.skysets.misttex,'\\')&&!strchr(m1.skysets.sound,':'));
#endif
	int n1=FUZZWRITE(out1,sizeof(out1),&m1);
	FUZZ_CHECK(n1>0&&(size_t)n1<=size);
	FUZZ_CHECK(FUZZREAD(out1,n1,&m2));
	int n2=FUZZWRITE(out2,sizeof(out2),&m2);
	FUZZ_CHECK(n2==n1&&!memcmp(out1,out2,n1));
	FUZZ_CHECK(!FUZZREAD(out1,n1-1,&m2));   //one byte short is refused
	return 0;
}
