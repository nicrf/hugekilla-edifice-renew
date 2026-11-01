// CHATMESSAGE (ReadChat, netmsg.h): the text is at most CHATMAXTEXT bytes,
// never more than what was received, and NUL-terminated.
#include "fuzz.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>BIGBUFSIZE) return 0;
	ChatMessage chat;
	if(!ReadChat(data,(int)size,&chat)) return 0;
	FUZZ_CHECK(size>=CHATHEADERSIZE);
	FUZZ_CHECK(chat.len>=0&&chat.len<=CHATMAXTEXT&&(size_t)chat.len<=size-CHATHEADERSIZE);
	FUZZ_CHECK(chat.text[chat.len]==0);
	FUZZ_CHECK(!memcmp(chat.text,data+CHATHEADERSIZE,chat.len));
	FUZZ_CHECK(chat.sender>=0&&chat.sender<=255);
	return 0;
}
