#ifndef SOUNDH
#define SOUNDH

#include "win32compat.h"
#include "vector.h"

#ifdef PlaySound
#undef PlaySound	// <mmsystem.h> maps it to PlaySoundA
#endif
void LoadSounds();
void StopSound(int num);
void PlaySound(int num,int loop);
void SetPlayerSoundPosition(Vector pos,Vector dir,Vector vel);
void SetSoundPosition(int num,Vector pos);
int LoadSound(char *filename,int num,int is3d);
void SetSoundVolume(int num,int volume);
int UnInitSound();
int InitSound(HWND hwnd);
void SetRolloff(float rolloff);
void SetMasterVolume(float volume);
//voice chat streams (voice.cpp), one per player: mono samples at VOICE_RATE
void SoundVoicePush(int stream,const float *pcm,int n);
int SoundVoiceQueued(int stream);
void SoundVoiceSource(int stream,Vector pos,float gain);
void SoundVoiceClear(int stream);

#endif
