//proximity push-to-talk voice chat (voice.cpp): capture, opus coding, jitter
//buffer and playback. The network side is the VOICE message (src/net/voicemsg.h,
//multiplay.cpp); the server relays a voice only to players within VOICE_RANGE.
//This header has no game dependency, so tests/voice_test.cpp can use it.

#ifndef VOICEH
#define VOICEH

#include <math.h>

#define VOICE_RATE 48000           //Hz, capture, opus and the mixer (sound_sdl.cpp)
#define VOICE_FRAMESAMPLES 960     //20 ms frames
#define VOICE_BITRATE 24000        //bit/s
#define VOICE_FULLRANGE 8.0f       //world units: full volume closer than this
#define VOICE_RANGE 40.0f          //silent (and not relayed) from here on; draw distance is 57
#define VOICE_WORLDSIZE 512.0f     //LANDGRIDSIZE: the world wraps around in x and z
#define VOICE_SPEAKINGTIME 300     //ms a name stays in the HUD after its last packet

//distance between two points of the wrapping world (x and z wrap, y doesn't)
inline float VoiceWrap(float d)
{
	if(!(d>-1e6f&&d<1e6f)) return 1e6f;   //NaN or huge (positions come from the network): far
	d=fmodf(d,VOICE_WORLDSIZE);
	if(d>VOICE_WORLDSIZE/2) d-=VOICE_WORLDSIZE;
	else if(d<-VOICE_WORLDSIZE/2) d+=VOICE_WORLDSIZE;
	return d;
}
inline float VoiceDistance(float ax,float ay,float az,float bx,float by,float bz)
{
	float dx=VoiceWrap(bx-ax),dy=by-ay,dz=VoiceWrap(bz-az);
	return sqrtf(dx*dx+dy*dy+dz*dz);
}
//volume for a distance: 1 up to VOICE_FULLRANGE, then linear down to 0 at VOICE_RANGE
inline float VoiceGain(float dist)
{
	if(!(dist<VOICE_RANGE)) return 0;
	if(dist<=VOICE_FULLRANGE) return 1;
	return (VOICE_RANGE-dist)/(VOICE_RANGE-VOICE_FULLRANGE);
}

void VoiceInit();
void VoiceShutdown();
void VoicePushToTalk(int down);             //push-to-talk key held / released
void VoiceUpdate();                         //every frame: capture, send, decode, position
void VoiceReceive(int player,int seq,const unsigned char *data,int len);
void VoiceReset();                          //players were renumbered or the game ended
void VoiceForget(int player);               //player left, the others kept their numbers
int VoiceTransmitting();
int VoiceIsSpeaking(int player);            //heard in the last VOICE_SPEAKINGTIME ms
void VoiceSetVolume(float volume);          //0..1, playback of other players
float VoiceGetVolume();
void VoiceSetMic(int on);                   //0: push-to-talk does nothing
int VoiceGetMic();

#endif
