// BLOCKS (ReadBlocksCount, ReadBlockEdit, ValidBlockEdit: netmsg.h): the count
// is bounded by the bytes received, and every block that passes ValidBlockEdit
// can index the renderer's face lists (redrawing.cpp, blockbuf[32]).
#include "fuzz.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>BIGBUFSIZE) return 0;
	int n=ReadBlocksCount(data,(int)size);
	FUZZ_CHECK(n>=0&&(n==0||BLOCKSHEADERSIZE+(size_t)n*BLOCKEDITSIZE<=size));
	for(int i=0;i<n;i++)
	{
		BlockEdit b;
		ReadBlockEdit(data,i,&b);
		if(!ValidBlockEdit(&b)||b.del) continue;
		FUZZ_CHECK(b.type>=0&&b.type<=MAXBLOCKTYPE);
		FUZZ_CHECK(b.top>=0&&b.top<MAXBLOCKTEX&&b.side>=0&&b.side<MAXBLOCKTEX&&b.bottom>=0&&b.bottom<MAXBLOCKTEX);
	}
	return 0;
}
