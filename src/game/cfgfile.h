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

private:
	char *data;
	int datalen;
	int FindValue(const char *&title);
};
#endif