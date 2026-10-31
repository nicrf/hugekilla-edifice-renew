#ifndef CFGFILEH
#define CFGFILEH
#include "vector.h"
class CfgFile
{
	
public:
	CfgFile();
	~CfgFile();
	int Load(LPCTSTR file);
	
	int GetValuei(LPCTSTR title);
	float GetValuef(LPCTSTR title);
	Vector GetValuev(LPCTSTR title);
	char * GetValues(LPCTSTR title);

private:
	char *data;
	int datalen;
	int FindValue(LPCTSTR &title);
};
#endif