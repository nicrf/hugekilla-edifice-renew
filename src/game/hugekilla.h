

#if !defined(AFX_HUGEKILLA_H__08A8C965_E458_11D2_87C8_444553540001__INCLUDED_)
#define AFX_HUGEKILLA_H__08A8C965_E458_11D2_87C8_444553540001__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "resource.h"


#endif // !defined(AFX_HUGEKILLA_H__08A8C965_E458_11D2_87C8_444553540001__INCLUDED_)
HWND GetHWND();
void GetMouseChange(int &x,int &y);
struct LandDefs;
LandDefs *GetDefs();
HDC GetGlHDC();
int MakeClose(Vector *pos);
void LockCursor(int lock);

//map save files (maps/<name>.map in the user folder, format in mapfile.h)
#define MAXMAPNAME 32
int ValidMapName(const char *name);   //[A-Za-z0-9_-]{1,32}
const char *MapPath(const char *name);
int ListSavedMaps(char (*names)[MAXMAPNAME+1],int max);
int SaveMapPath(const char *path,char *err,int errsize);
int LoadMapPath(const char *path,char *err,int errsize);
int StartServerMap();
int ReloadServerMap();
