//a loader of config files, which are used for land, controls, network settings, etc.

#include "stdafx.h"
#include "cfgfile.h"
#include "vector.h"

CfgFile::CfgFile()
{
	data=0;
}
CfgFile::~CfgFile()
{
	if(data) delete []data;
}
int CfgFile::Load(LPCTSTR filename)
{
	HANDLE file=CreateFile(filename,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	if(file==INVALID_HANDLE_VALUE) return 0;
	
	if(data)delete []data;
	data=0;
	datalen=GetFileSize(file,0)+1;
	if(datalen==-1) return 0;
	data=new char[datalen];
	DWORD read=0;
	ReadFile(file,data,datalen-1,&read,NULL);
	CloseHandle(file);

	for(int i=0;i<datalen-1;i++)
	{
		if(data[i]=='\r')data[i]=0;
	}
	data[datalen-1]=0;
	return 1;
}
int CfgFile::FindValue(LPCTSTR &title)
{
	if(!data) return -1;
	char firstchar=title[0];
	int titlelen=strlen(title);
	for(int i=0;i<datalen-titlelen;i++)
	{
		if(data[i]==firstchar&&(i==0||data[i-1]=='\n'))
		if(strncmp(title,&data[i],titlelen)==0)
		if(data[i+titlelen]==' '||data[i+titlelen]=='=')
		{
			do
			{
				i++;
				if(data[i]=='=')
				{
					do
					{
						i++;
					}
					while(data[i]==' '&&i<datalen);
					return i;
				}
				if(data[i]=='\r')
				{
					return -1;
				}
			}while(i<datalen);
		}
	}
	return -1;
}
int CfgFile::GetValuei(LPCTSTR title)
{
	int i=FindValue(title);
	if(i==-1) return 0;
	return atoi(&data[i]);
}

float CfgFile::GetValuef(LPCTSTR title)
{
	int i=FindValue(title);
	if(i==-1) return 0.0f;
	return (float)atof(&data[i]);
}

Vector CfgFile::GetValuev(LPCTSTR title)
{
	int i=FindValue(title);
	if(i==-1) return Vector();
	Vector ret;
	ret.x=atof(&data[i]);
	do
	{
		i++;
	}while(data[i]!=','&&data[i]!='\r');
	if(data[i]=='\r') return ret;
	i++;
	ret.y=atof(&data[i]);
	do
	{
		i++;
	}while(data[i]!=','&&data[i]!='\r');
	if(data[i]=='\r') return ret;
	i++;
	ret.z=atof(&data[i]);
	return ret;
}
char * CfgFile::GetValues(LPCTSTR title)
{
	char * ret;
	int i=FindValue(title);
	if(i==-1) return 0;
	ret=&data[i];	
	return ret;
}





