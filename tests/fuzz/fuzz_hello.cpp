// REQUESTENTER and INFO (ReadHelloName, netmsg.h; ReadVersionTrailer and
// VersionCompatible, src/net/version.cpp): type, uint8 n, name[n], NUL,
// version trailer. The name and version always end up NUL-terminated and
// within their buffers, whatever the lengths say.
#include "fuzz.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>BIGBUFSIZE) return 0;
	const char *msg=(const char *)data;
	int len=(int)size;
	char name[32];
	int trailer=ReadHelloName(msg,len,name,sizeof(name));
	FUZZ_CHECK(strlen(name)<sizeof(name));
	FUZZ_CHECK(trailer>=3&&trailer<=2+255+1);
	PeerVersion peer;
	//as the server does: the trailer may start past the end (then len-trailer<0)
	ReadVersionTrailer(trailer<=len?msg+trailer:msg,len-trailer,&peer);
	FUZZ_CHECK(strlen(peer.version)<sizeof(peer.version));
	for(const char *c=peer.version;*c;c++) FUZZ_CHECK(*c>=32&&*c<127);
	FUZZ_CHECK(peer.present||peer.protocol==1);
	//capabilities and join cookie after the trailer (M11 phase 2)
	HelloCaps caps;
	ReadHelloCaps(trailer<=len?msg+trailer:msg,len-trailer,&caps);
	FUZZ_CHECK(caps.caps>=0&&caps.caps<256);
	FUZZ_CHECK(peer.present||caps.caps==0);
	char reason[120];
	int ok=VersionCompatible(&peer,reason,sizeof(reason));
	FUZZ_CHECK(ok==(peer.present&&peer.protocol==EDIFICE_PROTOCOL_VERSION));
	FUZZ_CHECK(strlen(reason)<sizeof(reason));
	return 0;
}
