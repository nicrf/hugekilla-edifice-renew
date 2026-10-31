// Unit test of the voice chat pieces that need no game: the VOICE message
// (src/net/voicemsg.h: exact bytes, round trip, malformed messages refused),
// the distance/volume rules (src/audio/voice.h) and an opus encode/decode of
// a few 20 ms frames with the settings of voice.cpp.
#define _USE_MATH_DEFINES   // M_PI with MSVC
#include "voicemsg.h"
#include "voice.h"
#include <opus.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

int main()
{
	// exact bytes
	VoiceMessage v;
	v.player=3;
	v.seq=0x1234;
	v.len=3;
	v.data[0]=0xA0; v.data[1]=0xA1; v.data[2]=0xA2;
	unsigned char buf[VOICEMSGSIZE+16];
	int size=WriteVoice(buf,sizeof(buf),&v);
	CHECK(size==VOICEHEADERSIZE+3);
	const unsigned char expect[]={VOICE,3,0x34,0x12,3,0,0xA0,0xA1,0xA2};
	CHECK(memcmp(buf,expect,sizeof(expect))==0);

	// round trip
	VoiceMessage r;
	CHECK(ReadVoice(buf,size,&r));
	CHECK(r.player==3&&r.seq==0x1234&&r.len==3&&memcmp(r.data,v.data,3)==0);

	// largest frame
	v.len=VOICEMAXFRAME;
	for(int i=0;i<VOICEMAXFRAME;i++) v.data[i]=(unsigned char)i;
	size=WriteVoice(buf,sizeof(buf),&v);
	CHECK(size==VOICEMSGSIZE);
	CHECK(ReadVoice(buf,size,&r)&&r.len==VOICEMAXFRAME&&memcmp(r.data,v.data,VOICEMAXFRAME)==0);
	CHECK(WriteVoice(buf,VOICEMSGSIZE-1,&v)==0);   // doesn't fit

	// empty or oversized frames are never written
	v.len=0;
	CHECK(WriteVoice(buf,sizeof(buf),&v)==0);
	v.len=VOICEMAXFRAME+1;
	CHECK(WriteVoice(buf,sizeof(buf),&v)==0);

	// malformed messages
	v.len=10;
	size=WriteVoice(buf,sizeof(buf),&v);
	CHECK(size==VOICEHEADERSIZE+10);
	for(int n=0;n<size;n++) CHECK(!ReadVoice(buf,n,&r));       // truncated
	CHECK(!ReadVoice(buf,size+1,&r));                          // trailing byte
	buf[0]=VOICE+1;
	CHECK(!ReadVoice(buf,size,&r));                            // other type
	buf[0]=VOICE;
	buf[4]=0; buf[5]=0;
	CHECK(!ReadVoice(buf,VOICEHEADERSIZE,&r));                 // zero length
	unsigned char big[VOICEHEADERSIZE+VOICEMAXFRAME+1];
	memset(big,0,sizeof(big));
	big[0]=VOICE;
	big[4]=(VOICEMAXFRAME+1)&0xff; big[5]=(VOICEMAXFRAME+1)>>8;
	CHECK(!ReadVoice(big,sizeof(big),&r));                     // oversized, consistent length
	big[4]=0xff; big[5]=0xff;
	CHECK(!ReadVoice(big,sizeof(big),&r));                     // length 65535

	// distance with the world wrapping around, and the volume rule
	CHECK(fabsf(VoiceDistance(0,0,0,3,4,0)-5)<1e-4f);
	CHECK(fabsf(VoiceDistance(510,0,10,2,0,10)-4)<1e-3f);      // across the x edge
	CHECK(fabsf(VoiceDistance(10,0,-250,10,0,250)-12)<1e-3f);  // across the z edge
	CHECK(fabsf(VoiceDistance(0,0,0,0,30,0)-30)<1e-4f);        // y doesn't wrap
	CHECK(fabsf(VoiceDistance(0,0,0,1024+3,0,-2048-4)-5)<1e-3f);  // several turns
	CHECK(VoiceGain(VoiceDistance(0,0,0,NAN,0,0))==0);           // garbage positions: silent,
	CHECK(VoiceGain(VoiceDistance(0,0,0,0,INFINITY,0))==0);      // and no endless loop
	CHECK(VoiceGain(0)==1&&VoiceGain(VOICE_FULLRANGE)==1);
	CHECK(fabsf(VoiceGain((VOICE_FULLRANGE+VOICE_RANGE)/2)-.5f)<1e-4f);
	CHECK(VoiceGain(VOICE_RANGE)==0&&VoiceGain(VOICE_RANGE*2)==0);

	// opus: 20 ms frames of a tone, encoded at the voice bitrate, decoded back
	int err;
	OpusEncoder *enc=opus_encoder_create(VOICE_RATE,1,OPUS_APPLICATION_VOIP,&err);
	CHECK(err==OPUS_OK&&enc);
	OpusDecoder *dec=opus_decoder_create(VOICE_RATE,1,&err);
	CHECK(err==OPUS_OK&&dec);
	if(enc&&dec)
	{
		opus_encoder_ctl(enc,OPUS_SET_BITRATE(VOICE_BITRATE));
		float pcm[VOICE_FRAMESAMPLES],out[VOICE_FRAMESAMPLES];
		double phase=0,energy=0;
		int maxlen=0;
		for(int f=0;f<25;f++)
		{
			for(int i=0;i<VOICE_FRAMESAMPLES;i++)
			{
				pcm[i]=.3f*(float)sin(phase);
				phase+=2*M_PI*440/VOICE_RATE;
			}
			unsigned char packet[VOICEMAXFRAME];
			int len=opus_encode_float(enc,pcm,VOICE_FRAMESAMPLES,packet,sizeof(packet));
			CHECK(len>0&&len<=VOICEMAXFRAME);
			if(len>maxlen) maxlen=len;
			int n=opus_decode_float(dec,packet,len,out,VOICE_FRAMESAMPLES,0);
			CHECK(n==VOICE_FRAMESAMPLES);
			if(f>=5) for(int i=0;i<n;i++) energy+=out[i]*out[i];
		}
		// a tone of amplitude .3 has a mean square of .045
		double ms=energy/(20*VOICE_FRAMESAMPLES);
		CHECK(ms>.02&&ms<.08);
		CHECK(maxlen<=200);   // ~24 kbit/s is ~60 bytes per frame
		// packet loss concealment
		CHECK(opus_decode_float(dec,0,0,out,VOICE_FRAMESAMPLES,0)==VOICE_FRAMESAMPLES);
		printf("opus: max frame %d bytes, mean square %.3f\n",maxlen,ms);
	}
	if(enc) opus_encoder_destroy(enc);
	if(dec) opus_decoder_destroy(dec);

	if(failures) { printf("%d failure(s)\n",failures); return 1; }
	printf("voice: all checks passed\n");
	return 0;
}
