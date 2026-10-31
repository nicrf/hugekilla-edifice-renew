//VOICE message (push-to-talk voice chat, src/audio/voice.cpp): one 20 ms opus
//frame, sent unreliably (Sock::Send). Kept apart from multiplaystruct.h so the
//unit test (tests/voice_test.cpp) can use it without the game headers.
//On the wire, little-endian (wire.h):
//	uint8 type (VOICE) | uint8 player | uint16 sequence | uint16 n | opus frame[n]
//with 1 <= n <= VOICEMAXFRAME and nothing after the frame.
//A client's player field is never trusted: the server replaces it by the
//client's slot, as for PLAYERPOSITION.

#ifndef VOICEMSGH
#define VOICEMSGH

#include "wire.h"

#define VOICE 16                 //main message code (multiplaystruct.h)
#define VOICEMAXFRAME 400        //bytes of opus data in one message (~24 kbit/s gives ~60)
#define VOICEHEADERSIZE 6
#define VOICEMSGSIZE (VOICEHEADERSIZE+VOICEMAXFRAME)

struct VoiceMessage
{
	unsigned char player;
	uint16_t seq;
	uint16_t len;
	unsigned char data[VOICEMAXFRAME];
};

//returns the size written, 0 if the frame is empty, too big or doesn't fit
inline int WriteVoice(void *out,int size,const VoiceMessage *v)
{
	if(v->len<1||v->len>VOICEMAXFRAME) return 0;
	ByteWriter w(out,size);
	w.U8(VOICE);
	w.U8(v->player);
	w.U16(v->seq);
	w.U16(v->len);
	w.Bytes(v->data,v->len);
	return w.Ok()?w.Size():0;
}

//returns 0 for a malformed message (wrong type, bad length, truncated, trailing bytes)
inline int ReadVoice(const void *in,int msglen,VoiceMessage *v)
{
	ByteReader r(in,msglen);
	if(r.U8()!=VOICE) return 0;
	v->player=(unsigned char)r.U8();
	v->seq=(uint16_t)r.U16();
	v->len=(uint16_t)r.U16();
	if(!r.Ok()||v->len<1||v->len>VOICEMAXFRAME||v->len!=r.Left()) return 0;
	r.Bytes(v->data,v->len);
	return r.Ok();
}

#endif
