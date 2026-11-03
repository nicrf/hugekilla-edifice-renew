//a loader of config files, which are used for land, controls, network settings, etc.

#include "fileio.h"
#include "cfgfile.h"
#include "vector.h"
#include <string.h>
#include <string>
#include <vector>

struct CfgOverrideLine
{
	std::string file,key,value;
};
static std::vector<CfgOverrideLine> overrides;

void CfgFile::Override(const char *file,const char *key,const char *value)
{
	std::string v;
	for(const char *c=value;*c;c++)
		if(*c!='\r'&&*c!='\n') v+=*c;   //one line
	for(CfgOverrideLine &o:overrides)
		if(o.file==file&&o.key==key)
		{
			o.value=v;
			return;
		}
	overrides.push_back({file,key,v});
}

//the overrides of the file at this path, as lines in front of its own: FindValue takes
//the first line of a key
static std::string OverrideLines(const char *filename)
{
	const char *base=filename;
	for(const char *c=filename;*c;c++)
		if(*c=='/'||*c=='\\') base=c+1;
	std::string lines;
	for(const CfgOverrideLine &o:overrides)
		if(!strcasecmp(o.file.c_str(),base)) lines+=o.key+"="+o.value+"\r\n";
	return lines;
}

CfgFile::CfgFile()
{
	data=0;
}
CfgFile::~CfgFile()
{
	if(data) delete []data;
}
int CfgFile::Load(const char *filename)
{
	long size;
	char *file=HKReadFile(filename,&size);
	std::string lines=OverrideLines(filename);
	if(!file&&lines.empty()) return 0;
	if(!lines.empty())
	{
		long total=(long)lines.size()+(file?size:0);
		char *both=new char[total+1];
		memcpy(both,lines.data(),lines.size());
		if(file) memcpy(both+lines.size(),file,size);
		delete []file;
		file=both;
		size=total;
	}
	
	if(data)delete []data;
	data=file;
	datalen=size+1;

	for(int i=0;i<datalen-1;i++)
	{
		if(data[i]=='\r')data[i]=0;
	}
	data[datalen-1]=0;
	return 1;
}
int CfgFile::FindValue(const char *&title)
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
int CfgFile::GetValuei(const char *title)
{
	int i=FindValue(title);
	if(i==-1) return 0;
	return atoi(&data[i]);
}

float CfgFile::GetValuef(const char *title)
{
	int i=FindValue(title);
	if(i==-1) return 0.0f;
	return (float)atof(&data[i]);
}

Vector CfgFile::GetValuev(const char *title)
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
char * CfgFile::GetValues(const char *title)
{
	char * ret;
	int i=FindValue(title);
	if(i==-1) return 0;
	ret=&data[i];	
	return ret;
}





