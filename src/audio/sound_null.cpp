//No audio: the sound (sound.h) and voice chat (voice.h) of the dedicated server
//(edifice-server, CMake EDIFICE_DEDICATED), which has no speaker nor microphone and
//links neither SDL nor opus. Voice frames are still relayed between the players in
//range (multiplay.cpp): the server only never decodes or plays them itself.

#include "sound.h"
#include "voice.h"

void LoadSounds()
{
}

void StopSound(int)
{
}

void PlaySound(int,int)
{
}

void SetPlayerSoundPosition(Vector,Vector,Vector)
{
}

void SetSoundPosition(int,Vector)
{
}

int LoadSound(char *,int,int)
{
	return 1;
}

void SetSoundVolume(int,int)
{
}

int UnInitSound()
{
	return 1;
}

int InitSound()
{
	return 0;
}

void SetRolloff(float)
{
}

void SetMasterVolume(float)
{
}

void SoundVoicePush(int,const float *,int)
{
}

int SoundVoiceQueued(int)
{
	return 0;
}

void SoundVoiceSource(int,Vector,float)
{
}

void SoundVoiceClear(int)
{
}

void VoiceInit()
{
}

void VoiceShutdown()
{
}

void VoicePushToTalk(int)
{
}

void VoiceUpdate()
{
}

void VoiceReceive(int,int,const unsigned char *,int)
{
}

void VoiceReset()
{
}

void VoiceForget(int)
{
}

int VoiceTransmitting()
{
	return 0;
}

int VoiceIsSpeaking(int)
{
	return 0;
}

void VoiceSetVolume(float)
{
}

float VoiceGetVolume()
{
	return 0;
}

void VoiceSetMic(int)
{
}

int VoiceGetMic()
{
	return 0;
}
