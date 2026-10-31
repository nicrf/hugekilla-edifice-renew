//used to do ambient sound, and 3d sound.  it works like opengl in that it
//accepts numbers instead of pointers to sounds.  it uses directsound


//#include "StdAfx.h"
#include <windows.h>
#include "vector.h"

#include <mmsystem.h>
#include <dsound.h>
#include "sound.h"

#include "gameplay.h"

LPDIRECTSOUND dsound;
LPDIRECTSOUNDBUFFER primary;
LPDIRECTSOUND3DLISTENER primary3d;
LPDIRECTSOUNDBUFFER sound[3000];
LPDIRECTSOUND3DBUFFER sound3d[3000];
HWND mainhwnd;


int InitSound(HWND hwnd)
{
#ifdef HKDS
	return 0;
#endif

	mainhwnd=hwnd;
	int err=DirectSoundCreate(0,&dsound,0);
	if(err) return err;
	err=dsound->SetCooperativeLevel(hwnd,DSSCL_EXCLUSIVE);
	if(err) return err;
	
	DSBUFFERDESC primary3ddesc;
	primary3ddesc.dwSize=sizeof(primary3ddesc);
	primary3ddesc.dwFlags=DSBCAPS_CTRL3D |DSBCAPS_PRIMARYBUFFER ;
	primary3ddesc.dwBufferBytes=0;
	primary3ddesc.dwReserved=0;
	primary3ddesc.lpwfxFormat=0;
	err=dsound->CreateSoundBuffer(&primary3ddesc,&primary,0);
	WAVEFORMATEX wfex;
	primary->GetFormat(&wfex,sizeof(wfex),0);
	wfex.wFormatTag=WAVE_FORMAT_PCM;
	wfex.nSamplesPerSec*=2;
	wfex.nAvgBytesPerSec*=4;
	wfex.wBitsPerSample=16;
	wfex.cbSize=0;
	err=primary->SetFormat(&wfex);
	err=primary->QueryInterface(IID_IDirectSound3DListener,(void **)&primary3d);
	if(err) return err;
	primary3d->SetRolloffFactor(.01,DS3D_IMMEDIATE);
	primary3d->SetDistanceFactor(.1,DS3D_IMMEDIATE);
}

//cleans up

int UnInitSound()
{
	for(int i=0;i<3000;i++)
	{
		if(sound3d[i]) 
		{
			sound3d[i]->Release();
			sound3d[i]=0;
		}
		if(sound[i]) 
		{
			sound[i]->Release();
			sound[i]=0;
		}
	}
	if(primary3d)
		primary3d->Release();
	primary3d=0;
	if(primary)
		primary->Release();
	primary=0;
	return 0;
}

//reads a .wav file

int LoadSound(char *filename,int num,int is3d)
{
#ifdef HKDS
	return 0;
#endif
	if(!dsound) return 1;
	StopSound(num);
	if(sound3d[num])
	{
		sound3d[num]->Release();
		sound3d[num]=0;
	}
	if(sound[num]) 
	{
		sound[num]->Release();
		sound[num]=0;
	}
	WAVEFORMAT wf;
	HANDLE file=CreateFile(filename,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	if(file==INVALID_HANDLE_VALUE)
	{
		return 1;
	}
	char wavejunk[16];
	DWORD bytesread;
	ReadFile(file,wavejunk,16,&bytesread,0);
	DWORD wfsize;
	ReadFile(file,&wfsize,4,&bytesread,0);
	ReadFile(file,&wf,sizeof(wf),&bytesread,0);	
	WORD bitssample;
	ReadFile(file,&bitssample,2,&bytesread,0);
	WAVEFORMATEX wfex;
	wfex.wFormatTag=wf.wFormatTag;
	wfex.nChannels=wf.nChannels;
	wfex.nSamplesPerSec=wf.nSamplesPerSec;
	wfex.nAvgBytesPerSec=wf.nAvgBytesPerSec;
	wfex.nBlockAlign=wf.nBlockAlign;
	wfex.wBitsPerSample=bitssample;
	wfex.cbSize=0;
	char data[4];
	ReadFile(file,data,4,&bytesread,0);
	DWORD wavesize;
	ReadFile(file,&wavesize,4,&bytesread,0);
	
	DSBUFFERDESC sounddesc;
	sounddesc.dwSize=sizeof(sounddesc);
	sounddesc.dwFlags=is3d?DSBCAPS_CTRL3D:0;
	sounddesc.dwBufferBytes=wavesize;
	sounddesc.dwReserved=0;
	sounddesc.lpwfxFormat=&wfex;
	sound[num]=0;
	int err=dsound->CreateSoundBuffer(&sounddesc,&sound[num],0);
	if(err) goto error;
	char *buf1,*buf2;
	DWORD buf1size,buf2size;
	err=sound[num]->Lock(0,wavesize,(void **)&buf1,&buf1size,(void **)&buf2,&buf2size,0);
	if(err) goto error;
	ReadFile(file,buf1,buf1size,&bytesread,0);
	ReadFile(file,buf2,buf2size,&bytesread,0);
	sound[num]->Unlock(buf1,buf1size,buf2,buf2size);
	if(is3d) sound[num]->QueryInterface(IID_IDirectSound3DBuffer,(void **)&sound3d[num]);
error:
	CloseHandle(file);
	return 0;
}

void SetSoundPosition(int num,Vector pos)
{
	if(num>=0&&num<3000)
	{
		if(!sound3d[num]) return;
		sound3d[num]->SetPosition(pos.x,pos.y,pos.z,DS3D_IMMEDIATE);
	}
}

void SetPlayerSoundPosition(Vector pos,Vector dir,Vector vel)
{
	if(!primary3d) return;
	primary3d->SetPosition(pos.x,pos.y,pos.z,DS3D_IMMEDIATE);
	primary3d->SetVelocity(vel.x,vel.y,vel.z,DS3D_IMMEDIATE);
	Vector up=GetRight(dir,Vector(0,-1,0));
	primary3d->SetOrientation(dir.x,-dir.y,dir.z,up.x,up.y,up.z,DS3D_IMMEDIATE);
}

void SetSoundVolume(int num,int volume)
{
	if(!sound[num]) return;
	sound[num]->SetVolume(volume);
}

void PlaySound(int num,int loop)
{
	if(!sound[num]) return;
	//sound[num]->Stop();
	sound[num]->SetCurrentPosition(0);
	sound[num]->Play(0,0,loop?DSBPLAY_LOOPING:0);
}

void SetRolloff(float rolloff)
{
	if(!primary3d) return;
	primary3d->SetRolloffFactor(rolloff,DS3D_IMMEDIATE);
}

void StopSound(int num)
{
	if(!sound[num]) return;
	sound[num]->Stop();
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


/*

int InitSound(HWND hwnd)
{
	return 0;
}

//cleans up

int UnInitSound()
{

	return 0;
}

//reads a .wav file

int LoadSound(char *filename,int num,int is3d)
{
	return 0;
}

void SetSoundPosition(int num,Vector pos)
{

}

void SetPlayerSoundPosition(Vector pos,Vector dir,Vector vel)
{

}

void SetSoundVolume(int num,int volume)
{

}

void PlaySound(int num,int loop)
{

}

void SetRolloff(float rolloff)
{

}

void StopSound(int num)
{

}

//used to load all the sounds for one gun, since they have similar names.

void LoadGunSounds(char *gun,int num)
{

}

void LoadSounds()
{

}*/