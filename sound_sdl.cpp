//SDL2 replacement for the DirectSound code in sound.cpp.  Same interface: sounds
//are numbered slots, each slot is one buffer that can be playing once at a time.
//3d slots are attenuated and panned against the listener like DirectSound3D.

#ifndef _WIN32

#include "stdafx.h"
#include "vector.h"
#include "sound.h"
#include "gameplay.h"
#include <SDL.h>

#define MAXSOUNDS 3000

struct SoundSlot
{
	float *samples;   //mono, at the device rate
	int length;
	int is3d;
	int playing;
	int loop;
	int pos;
	float gain;       //from SetSoundVolume
	Vector position;
};

static SoundSlot sound[MAXSOUNDS];
static SDL_AudioDeviceID device;
static SDL_AudioSpec devicespec;

static Vector listenerpos;
static Vector listenerfront(0,0,1);
static Vector listenertop(0,1,0);
static float rollofffactor=.01f;
static float mastervolume=1;
static const float distancefactor=.1f;

static float Dot(const Vector &a,const Vector &b)
{
	return a.x*b.x+a.y*b.y+a.z*b.z;
}

static Vector Cross(const Vector &a,const Vector &b)
{
	return Vector(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
}

//computes left/right gains for a 3d slot (DirectSound inverse distance model)

static void Spatialize(const SoundSlot &s,float &left,float &right)
{
	Vector d(s.position.x-listenerpos.x,s.position.y-listenerpos.y,s.position.z-listenerpos.z);
	float dist=sqrtf(Dot(d,d));
	float meters=dist*distancefactor;
	const float mindistance=1;
	float att=1;
	if(meters>mindistance)
		att=mindistance/(mindistance+rollofffactor*(meters-mindistance));
	float pan=0;
	if(dist>0.0001f)
	{
		Vector rightv=Cross(listenertop,listenerfront);
		float len=sqrtf(Dot(rightv,rightv));
		if(len>0.0001f)
			pan=Dot(d,rightv)/(len*dist);
	}
	//constant power pan
	float angle=(pan+1)*(float)M_PI/4;
	left=att*cosf(angle)*1.4142f;
	right=att*sinf(angle)*1.4142f;
}

static void AudioCallback(void *,Uint8 *stream,int len)
{
	float *out=(float *)stream;
	int frames=len/(sizeof(float)*2);
	memset(stream,0,len);
	for(int i=0;i<MAXSOUNDS;i++)
	{
		SoundSlot &s=sound[i];
		if(!s.playing||!s.samples) continue;
		float left=s.gain,right=s.gain;
		if(s.is3d)
		{
			float l,r;
			Spatialize(s,l,r);
			left*=l;
			right*=r;
		}
		for(int f=0;f<frames;f++)
		{
			if(s.pos>=s.length)
			{
				if(!s.loop)
				{
					s.playing=0;
					break;
				}
				s.pos=0;
			}
			float v=s.samples[s.pos++];
			out[f*2]+=v*left;
			out[f*2+1]+=v*right;
		}
	}
	for(int f=0;f<frames*2;f++)
	{
		out[f]*=mastervolume;
		if(out[f]>1) out[f]=1;
		if(out[f]<-1) out[f]=-1;
	}
}

int InitSound(HWND)
{
#ifdef HKDS
	return 0;
#endif
	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq=44100;
	want.format=AUDIO_F32SYS;
	want.channels=2;
	want.samples=1024;
	want.callback=AudioCallback;
	device=SDL_OpenAudioDevice(0,0,&want,&devicespec,0);
	if(!device)
	{
		fprintf(stderr,"SDL_OpenAudioDevice failed: %s\n",SDL_GetError());
		return 1;
	}
	SDL_PauseAudioDevice(device,0);
	return 0;
}

//cleans up

int UnInitSound()
{
	if(device) SDL_CloseAudioDevice(device);
	device=0;
	for(int i=0;i<MAXSOUNDS;i++)
	{
		delete []sound[i].samples;
		sound[i].samples=0;
	}
	return 0;
}

//reads a .wav file

int LoadSound(char *filename,int num,int is3d)
{
#ifdef HKDS
	return 0;
#endif
	if(!device) return 1;
	if(num<0||num>=MAXSOUNDS) return 1;
	char path[1024];
	HKPath(filename,path,sizeof(path));
	SDL_AudioSpec spec;
	Uint8 *buf;
	Uint32 buflen;
	if(!SDL_LoadWAV(path,&spec,&buf,&buflen))
		return 1;
	SDL_AudioCVT cvt;
	if(SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_F32SYS,1,devicespec.freq)<0)
	{
		SDL_FreeWAV(buf);
		return 1;
	}
	cvt.len=buflen;
	cvt.buf=(Uint8 *)SDL_malloc(buflen*cvt.len_mult);
	memcpy(cvt.buf,buf,buflen);
	SDL_FreeWAV(buf);
	SDL_ConvertAudio(&cvt);
	int length=cvt.len_cvt/sizeof(float);
	float *samples=new float[length];
	memcpy(samples,cvt.buf,length*sizeof(float));
	SDL_free(cvt.buf);

	SDL_LockAudioDevice(device);
	SoundSlot &s=sound[num];
	delete []s.samples;
	s.samples=samples;
	s.length=length;
	s.is3d=is3d;
	s.playing=0;
	s.pos=0;
	s.gain=1;
	SDL_UnlockAudioDevice(device);
	return 0;
}

void SetSoundPosition(int num,Vector pos)
{
	if(num<0||num>=MAXSOUNDS||!sound[num].is3d) return;
	SDL_LockAudioDevice(device);
	sound[num].position=pos;
	SDL_UnlockAudioDevice(device);
}

void SetPlayerSoundPosition(Vector pos,Vector dir,Vector vel)
{
	if(!device) return;
	Vector up=GetRight(dir,Vector(0,-1,0));
	SDL_LockAudioDevice(device);
	listenerpos=pos;
	listenerfront=Vector(dir.x,-dir.y,dir.z);
	listenertop=up;
	SDL_UnlockAudioDevice(device);
}

//volume is in hundredths of a decibel (0 = full, negative = quieter), as in DirectSound

void SetSoundVolume(int num,int volume)
{
	if(num<0||num>=MAXSOUNDS||!sound[num].samples) return;
	if(volume>0) volume=0;
	SDL_LockAudioDevice(device);
	sound[num].gain=powf(10.0f,volume/2000.0f);
	SDL_UnlockAudioDevice(device);
}

void PlaySound(int num,int loop)
{
	if(num<0||num>=MAXSOUNDS||!sound[num].samples) return;
	SDL_LockAudioDevice(device);
	sound[num].pos=0;
	sound[num].loop=loop;
	sound[num].playing=1;
	SDL_UnlockAudioDevice(device);
}

void SetRolloff(float rolloff)
{
	if(!device) return;
	SDL_LockAudioDevice(device);
	rollofffactor=rolloff;
	SDL_UnlockAudioDevice(device);
}

void SetMasterVolume(float volume)
{
	if(!device) return;
	SDL_LockAudioDevice(device);
	mastervolume=volume;
	SDL_UnlockAudioDevice(device);
}

void StopSound(int num)
{
	if(num<0||num>=MAXSOUNDS||!sound[num].samples) return;
	SDL_LockAudioDevice(device);
	sound[num].playing=0;
	SDL_UnlockAudioDevice(device);
}

//used to load all the sounds for one gun, since they have similar names.

void LoadGunSounds(char *gun,int num)
{
	char fullname[100];
	strcpy(fullname,"sound\\");
	strcat(fullname,gun);
	strcat(fullname,"fire");
	strcat(fullname,".wav");
	LoadSound(fullname,num*4+1,0);
	LoadSound(fullname,num*4+2,1);
	strcpy(fullname,"sound\\");
	strcat(fullname,gun);
	strcat(fullname,"reload");
	strcat(fullname,".wav");
	LoadSound(fullname,num*4+3,0);
	LoadSound(fullname,num*4+4,1);
}

void LoadSounds()
{

	LoadGunSounds("gat",0);
	LoadGunSounds("m16",1);
	LoadGunSounds("acr",2);
	LoadGunSounds("ump",3);
	LoadGunSounds("grenade",4);
	LoadGunSounds("beretta",5);
	LoadGunSounds("sr1000",6);

	LoadSound("sound\\footsteps.wav",100,1);
	LoadSound("sound\\wallscrape.wav",110,1);
	LoadSound("sound\\landing.wav",120,1);
	LoadSound("sound\\thump.wav",130,1);
	LoadSound("sound\\splash4.wav",140,1);
	LoadSound("sound\\hit1.wav",150,0);
	LoadSound("sound\\death.wav",160,0);
	LoadSound("sound\\hit1.wav",151,1);
	LoadSound("sound\\death.wav",161,1);
	LoadSound("sound\\thrust.wav",170,0);
	LoadSound("sound\\thrust.wav",171,1);
	LoadSound("sound\\nade.wav",181,1);

	LoadSound("sound\\grenadesplat.wav",191,1);

	LoadSound("sound\\thunder2.wav",500,0);


	LoadSound("sound\\returnflag.wav",RETURNFLAG,0);
	LoadSound("sound\\stolenflag.wav",STOLENFLAG,0);
	LoadSound("sound\\capturedflag.wav",CAPTUREDFLAG,0);
	LoadSound("sound\\redteamwins.wav",REDTEAMWINS,0);
	LoadSound("sound\\blueteamwins.wav",BLUETEAMWINS,0);
}

#endif // !_WIN32
