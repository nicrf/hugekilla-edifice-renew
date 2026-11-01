// PLAYERNAMES (ReadPlayerNames, netmsg.h): at most MAXPLAYERS entries, names
// of at most 31 characters, NUL-terminated, never read past the message.
#include "fuzz.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>BIGBUFSIZE) return 0;
	PlayerNameEntry entries[MAXPLAYERS];
	int n=ReadPlayerNames(data,(int)size,entries,MAXPLAYERS);
	FUZZ_CHECK(n>=0&&n<=MAXPLAYERS);
	FUZZ_CHECK(n==0||size>=2+(size_t)n*11);
	for(int i=0;i<n;i++)
	{
		FUZZ_CHECK(strlen(entries[i].name)<=31);
		FUZZ_CHECK(entries[i].num>=0&&entries[i].num<=255);
	}
	PlayerNameEntry few[3];
	FUZZ_CHECK(ReadPlayerNames(data,(int)size,few,3)<=3);
	return 0;
}
