

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