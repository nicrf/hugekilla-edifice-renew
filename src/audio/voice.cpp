//proximity push-to-talk voice chat.
//Sending: while the push-to-talk key is held, an SDL capture device (48 kHz mono)
//fills a ring buffer from SDL's audio thread; the main loop (VoiceUpdate) cuts it
//into 20 ms frames, encodes them with opus and sends VOICE messages (multiplay.cpp).
//Receiving: per player, an opus decoder and a small jitter buffer ordered by
//sequence number; decoded speech goes to that player's voice stream in
//sound_sdl.cpp, panned from the player and faded out with the distance.
//No capture device or no microphone permission: nothing is sent (logged once).
//Debug aids: HK_VOICE_TEST_TONE=1 sends a tone instead of the microphone,
//HK_VOICE_AUTOTALK=1 transmits all the time, HK_VOICE_LOOPBACK=1 plays your own
//voice through the receive path, HK_VOICE_LOG=1 logs talk spurts on stderr.

#ifndef HK_WIN32_NATIVE

#include "StdAfx.h"
#include "vector.h"
#include "playing.h"
#include "multiplay.h"
#include "sound.h"
#include "voice.h"
#include "voicemsg.h"
#include <SDL.h>
#include <opus.h>

#define VOICEJITTER 8           //frames held per player, by sequence
#define VOICEPREBUFFER 2        //frames waited for before a talk spurt starts playing
#define VOICEPREBUFFERWAIT 60   //ms: ...or this long after its first packet
#define VOICEQUEUE 3            //frames kept queued in the mixer
#define VOICEIDLE 250           //ms without packets that ends a talk spurt
#define CAPTURESAMPLES VOICE_RATE   //1 s of captured samples at most

struct VoiceFrame
{
	int used;
	uint16_t seq;
	int len;
	unsigned char data[VOICEMAXFRAME];
};

struct VoicePeer
{
	OpusDecoder *dec;
	int active;             //in a talk spurt
	int started;            //prebuffering done
	uint16_t next;          //next sequence to play
	Uint32 lastrecv;        //SDL_GetTicks of the last packet
	Uint32 waitsince;       //prebuffering since
	Uint32 heard;           //same, 0: never (HUD)
	VoiceFrame frame[VOICEJITTER];
	int decoded,concealed,dropped;   //this talk spurt, for HK_VOICE_LOG
};

static VoicePeer peer[MAXPLAYERS];

static int initialized;
static float voicevolume=1;
static int micenabled=1;
static int pttdown;
static int transmitting;
static uint16_t sendseq;
static OpusEncoder *encoder;

static SDL_AudioDeviceID capdev;
static int capfailed;           //no capture device: don't retry every key press
static float capbuf[CAPTURESAMPLES];
static int capread,capcount;

static int testtone,autotalk,loopback,voicelog;
static double tonephase;
static Uint32 tonetime;

//SDL's audio thread: appends captured samples (drops them when the main loop is late)
static void SDLCALL CaptureCallback(void *,Uint8 *stream,int len)
{
	const float *in=(const float *)stream;
	int n=len/sizeof(float);
	for(int i=0;i<n;i++)
	{
		if(capcount==CAPTURESAMPLES)
		{
			capread=(capread+1)%CAPTURESAMPLES;
			capcount--;
		}
		capbuf[(capread+capcount)%CAPTURESAMPLES]=in[i];
		capcount++;
	}
}

static int OpenCapture()
{
	if(capdev) return 1;
	if(capfailed) return 0;
	SDL_AudioSpec want,have;
	SDL_zero(want);
	want.freq=VOICE_RATE;
	want.format=AUDIO_F32SYS;
	want.channels=1;
	want.samples=VOICE_FRAMESAMPLES;
	want.callback=CaptureCallback;
	capdev=SDL_OpenAudioDevice(0,1,&want,&have,0);   //SDL converts to exactly this format
	if(!capdev)
	{
		capfailed=1;
		fprintf(stderr,"voice: no microphone, push-to-talk disabled (%s)\n",SDL_GetError());
		return 0;
	}
	return 1;
}

static void ClearCapture()
{
	if(capdev) SDL_LockAudioDevice(capdev);
	capread=0;
	capcount=0;
	if(capdev) SDL_UnlockAudioDevice(capdev);
}

//takes one frame of captured samples, 0 if there isn't a whole one yet
static int TakeFrame(float *pcm)
{
	if(testtone)
	{
		//a tone instead of the microphone, at the real-time rate
		Uint32 now=SDL_GetTicks();
		if((int)(now-tonetime)<20) return 0;
		tonetime+=20;
		if((int)(now-tonetime)>200) tonetime=now;   //don't catch up after a pause
		for(int i=0;i<VOICE_FRAMESAMPLES;i++)
		{
			float wobble=.5f+.5f*sinf((float)(tonephase*2/440));   //~2 Hz
			pcm[i]=.3f*wobble*sinf((float)tonephase);
			tonephase+=2*M_PI*440/VOICE_RATE;
		}
		if(tonephase>2*M_PI*440*1000) tonephase=0;
		return 1;
	}
	if(!capdev) return 0;
	int got=0;
	SDL_LockAudioDevice(capdev);
	if(capcount>=VOICE_FRAMESAMPLES)
	{
		for(int i=0;i<VOICE_FRAMESAMPLES;i++)
			pcm[i]=capbuf[(capread+i)%CAPTURESAMPLES];
		capread=(capread+VOICE_FRAMESAMPLES)%CAPTURESAMPLES;
		capcount-=VOICE_FRAMESAMPLES;
		got=1;
	}
	SDL_UnlockAudioDevice(capdev);
	return got;
}

void VoiceInit()
{
	if(initialized) return;
	initialized=1;
	testtone=getenv("HK_VOICE_TEST_TONE")!=0;
	autotalk=getenv("HK_VOICE_AUTOTALK")!=0;
	loopback=getenv("HK_VOICE_LOOPBACK")!=0;
	voicelog=getenv("HK_VOICE_LOG")!=0;
	int err;
	encoder=opus_encoder_create(VOICE_RATE,1,OPUS_APPLICATION_VOIP,&err);
	if(err!=OPUS_OK)
	{
		fprintf(stderr,"voice: opus encoder: %s\n",opus_strerror(err));
		encoder=0;
	}
	else
	{
		opus_encoder_ctl(encoder,OPUS_SET_BITRATE(VOICE_BITRATE));
		opus_encoder_ctl(encoder,OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	}
	for(int i=0;i<MAXPLAYERS;i++)
	{
		peer[i].dec=opus_decoder_create(VOICE_RATE,1,&err);
		if(err!=OPUS_OK) peer[i].dec=0;
	}
}

void VoiceShutdown()
{
	if(capdev) SDL_CloseAudioDevice(capdev);
	capdev=0;
	if(encoder) opus_encoder_destroy(encoder);
	encoder=0;
	for(int i=0;i<MAXPLAYERS;i++)
	{
		if(peer[i].dec) opus_decoder_destroy(peer[i].dec);
		peer[i].dec=0;
	}
	initialized=0;
}

static void StartTransmit()
{
	if(transmitting||!micenabled||!encoder) return;
	if(!testtone)
	{
		if(!OpenCapture()) return;
		ClearCapture();
		SDL_PauseAudioDevice(capdev,0);
	}
	tonetime=SDL_GetTicks();
	opus_encoder_ctl(encoder,OPUS_RESET_STATE);
	transmitting=1;
}

static void StopTransmit()
{
	if(!transmitting) return;
	transmitting=0;
	if(capdev) SDL_PauseAudioDevice(capdev,1);
	ClearCapture();
}

void VoicePushToTalk(int down)
{
	pttdown=down;
}

int VoiceTransmitting()
{
	return transmitting;
}

void VoiceSetVolume(float volume)
{
	if(volume<0) volume=0;
	if(volume>1) volume=1;
	voicevolume=volume;
}

float VoiceGetVolume()
{
	return voicevolume;
}

void VoiceSetMic(int on)
{
	micenabled=!!on;
}

int VoiceGetMic()
{
	return micenabled;
}

static void ResetPeer(int p)
{
	VoicePeer &v=peer[p];
	v.active=0;
	v.started=0;
	for(int i=0;i<VOICEJITTER;i++) v.frame[i].used=0;
	SoundVoiceClear(p);
}

//player left and its slot is free, the others kept their numbers (M11 phase 3b)
void VoiceForget(int player)
{
	if(player<0||player>=MAXPLAYERS) return;
	ResetPeer(player);
	peer[player].heard=0;
}

void VoiceReset()
{
	for(int i=0;i<MAXPLAYERS;i++)
	{
		ResetPeer(i);
		peer[i].heard=0;
	}
}

//a VOICE frame from a player that is in range (multiplay.cpp checked it)
void VoiceReceive(int player,int seq,const unsigned char *data,int len)
{
	if(player<0||player>=MAXPLAYERS||len<1||len>VOICEMAXFRAME) return;
	VoicePeer &v=peer[player];
	if(!v.dec) return;
	Uint32 now=SDL_GetTicks();
	uint16_t s=(uint16_t)seq;
	if(!v.active)
	{
		ResetPeer(player);
		opus_decoder_ctl(v.dec,OPUS_RESET_STATE);
		v.active=1;
		v.next=s;
		v.waitsince=now;
		v.decoded=v.concealed=v.dropped=0;
		if(voicelog) fprintf(stderr,"voice: hearing player %d (%s)\n",player,GetPlayer(player)->name);
	}
	int ahead=(int16_t)(uint16_t)(s-v.next);
	if(ahead<0)                            //late or duplicate: already played
	{
		v.dropped++;
		return;
	}
	if(ahead>=VOICEJITTER)
	{
		//far ahead (lost a burst): resynchronize on it
		for(int i=0;i<VOICEJITTER;i++) v.frame[i].used=0;
		v.next=s;
	}
	VoiceFrame &f=v.frame[s%VOICEJITTER];
	if(f.used&&f.seq==s)                   //duplicate
	{
		v.dropped++;
		return;
	}
	f.used=1;
	f.seq=s;
	f.len=len;
	memcpy(f.data,data,len);
	v.lastrecv=now;
	v.heard=now?now:1;
}

int VoiceIsSpeaking(int player)
{
	if(player<0||player>=MAXPLAYERS||!peer[player].heard) return 0;
	return (int)(SDL_GetTicks()-peer[player].heard)<VOICE_SPEAKINGTIME;
}

//decodes what is due for one player into its mixer stream
static void PlayPeer(int p,Uint32 now)
{
	VoicePeer &v=peer[p];
	int stored=0;
	for(int i=0;i<VOICEJITTER;i++) stored+=v.frame[i].used;
	if(!v.started)
	{
		if(stored>=VOICEPREBUFFER||(stored&&(int)(now-v.waitsince)>=VOICEPREBUFFERWAIT)) v.started=1;
	}
	if(v.started)
	{
		float pcm[VOICE_FRAMESAMPLES];
		while(stored&&SoundVoiceQueued(p)<VOICE_FRAMESAMPLES*VOICEQUEUE)
		{
			VoiceFrame &f=v.frame[v.next%VOICEJITTER];
			int n;
			if(f.used&&f.seq==v.next)
			{
				n=opus_decode_float(v.dec,f.data,f.len,pcm,VOICE_FRAMESAMPLES,0);
				f.used=0;
				stored--;
				v.decoded++;
			}
			else
			{
				n=opus_decode_float(v.dec,0,0,pcm,VOICE_FRAMESAMPLES,0);   //lost: concealment
				v.concealed++;
			}
			if(n>0) SoundVoicePush(p,pcm,n);
			v.next++;
		}
		if(!stored&&!SoundVoiceQueued(p))
		{
			v.started=0;   //ran dry: prebuffer again
			v.waitsince=now;
		}
	}
	if(!stored&&(int)(now-v.lastrecv)>VOICEIDLE)
	{
		if(voicelog) fprintf(stderr,"voice: player %d stopped: %d frames played, %d concealed, %d dropped\n",
			p,v.decoded,v.concealed,v.dropped);
		v.active=0;
		v.started=0;
	}
}

void VoiceUpdate()
{
	if(!initialized) return;
	int ingame=GetLocalPlayer()->playstate>=10;

	//sending
	int want=(pttdown||autotalk)&&ingame;
	if(want&&!transmitting) StartTransmit();
	if(!want&&transmitting) StopTransmit();
	if(transmitting)
	{
		float pcm[VOICE_FRAMESAMPLES];
		unsigned char packet[VOICEMAXFRAME];
		for(int frames=0;frames<10&&TakeFrame(pcm);frames++)
		{
			int len=opus_encode_float(encoder,pcm,VOICE_FRAMESAMPLES,packet,sizeof(packet));
			if(len<1) continue;   //error
			SendVoiceFrame(sendseq,packet,len);
			if(loopback) VoiceReceive(GetLocalNum(),sendseq,packet,len);
			sendseq++;
		}
	}

	//receiving: decode, and follow each speaker's position
	Uint32 now=SDL_GetTicks();
	Vector listener=GetLocalPlayer()->pos;
	for(int p=0;p<MAXPLAYERS;p++)
	{
		VoicePeer &v=peer[p];
		if(!v.active&&!SoundVoiceQueued(p)) continue;
		Vector pos=GetPlayer(p)->pos;
		Vector d(VoiceWrap(pos.x-listener.x),pos.y-listener.y,VoiceWrap(pos.z-listener.z));
		float gain=VoiceGain(sqrtf(d.x*d.x+d.y*d.y+d.z*d.z))*voicevolume;
		SoundVoiceSource(p,Vector(listener.x+d.x,listener.y+d.y,listener.z+d.z),gain);
		if(v.active) PlayPeer(p,now);
	}
}

#endif // !HK_WIN32_NATIVE
