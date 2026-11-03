#ifndef CFGFILEH
#define CFGFILEH
#include "hktypes.h"
#include "vector.h"
class CfgFile
{
	
public:
	CfgFile();
	~CfgFile();
	int Load(const char *file);
	
	int GetValuei(const char *title);
	float GetValuef(const char *title);
	Vector GetValuev(const char *title);
	char * GetValues(const char *title);

	//the dedicated server's flags and HK_* variables (src/platform/dedicated.cpp): key
	//of the settings file named file ("net.ini", "sv.ini") takes this value, whatever
	//the file says (or if there is no such file), in every CfgFile that loads it
	static void Override(const char *file,const char *key,const char *value);

private:
	char *data;
	int datalen;
	int FindValue(const char *&title);
};
#endif