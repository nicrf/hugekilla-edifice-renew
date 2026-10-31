//build menu opened by the "build" key: structure categories (folders of data/Structures)
//on the left, their structures on the right, picked with the mouse. Picking one loads it
//as a preview in the world (menu BUILDING): left click builds it, right click cancels.

#include "StdAfx.h"
#include "redrawing.h"
#include "playing.h"
#include "ui.h"
#include <ctype.h>

extern Vector mousepos;
extern char menutext[5000];
extern char menu;
extern Controls controls;
extern DWORD curtime;

void LockCursor(int lock);

#define MAXENTRIES 64
#define NAMELEN 64

static int buildmenuopen;
static char categories[MAXENTRIES][NAMELEN];
static int numcategories;
static int curcategory;
static char items[MAXENTRIES][NAMELEN];   //file names, extension included
static int numitems;
static int scroll;

#define PANELX1 -.9f
#define PANELX2 .9f
#define PANELY1 -.75f
#define PANELY2 .8f
#define LISTTOP .55f
#define ROWH .075f
#define VISIBLEROWS 16
#define CATX -.85f
#define ITEMX -.25f

static int Inside(float x1,float y1,float x2,float y2)
{
	return mousepos.x>x1&&mousepos.x<x2&&mousepos.y>y1&&mousepos.y<y2;
}

static int CompareNames(const void *a,const void *b)
{
	const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b;
	for(;*x&&*y;x++,y++)
	{
		int cx=tolower(*x),cy=tolower(*y);
		if(cx!=cy) return cx-cy;
	}
	return *x-*y;
}

//lists the entries of a folder: directories or files only
static int ListFolder(const char *folder,int wantdirs,char out[][NAMELEN])
{
	char filter[300];
	sprintf(filter,"%s\\*",folder);
	WIN32_FIND_DATA fd;
	HANDLE find=FindFirstFile(filter,&fd);
	if(find==INVALID_HANDLE_VALUE) return 0;
	int n=0;
	do
	{
		if(fd.cFileName[0]=='.') continue;
		int isdir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
		if(isdir!=wantdirs||n>=MAXENTRIES) continue;
		strncpy(out[n],fd.cFileName,NAMELEN-1);
		out[n][NAMELEN-1]=0;
		n++;
	}
	while(FindNextFile(find,&fd));
	FindClose(find);
	qsort(out,n,NAMELEN,CompareNames);
	return n;
}

static void SelectCategory(int c)
{
	curcategory=c;
	scroll=0;
	numitems=0;
	if(c<0||c>=numcategories) return;
	char folder[200];
	sprintf(folder,"structures\\%s",categories[c]);
	numitems=ListFolder(folder,0,items);
}

int IsBuildMenuOpen()
{
	return buildmenuopen;
}

void OpenBuildMenu()
{
	numcategories=ListFolder("structures",1,categories);
	if(curcategory>=numcategories) curcategory=0;
	SelectCategory(curcategory);
	buildmenuopen=1;
	menu=NOMENU;
	menutext[0]=0;
	LockCursor(0);
}

static void CloseBuildMenu()
{
	buildmenuopen=0;
	if(GetLocalPlayer()->playstate>=10) LockCursor(1);
}

static void PickItem(int i)
{
	if(i<0||i>=numitems) return;
	char path[300];
	sprintf(path,"structures\\%s\\%s",categories[curcategory],items[i]);
	CloseBuildMenu();
	if(!LoadStructure(path))
	{
		ConsoleDisplayText("Could not load that structure");
		return;
	}
	menu=BUILDING;
	strcpy(menutext,"Left click: build here\rRight click: cancel");
	GetLocalPlayer()->menudissapeartime=curtime+7000;
}

void BuildMenuClick()
{
	if(!buildmenuopen) return;
	if(!Inside(PANELX1,PANELY1,PANELX2,PANELY2))
	{
		CloseBuildMenu();
		return;
	}
	for(int c=0;c<numcategories&&c<VISIBLEROWS;c++)
	{
		float y=LISTTOP-c*ROWH;
		if(Inside(CATX-.02f,y-.01f,ITEMX-.1f,y+ROWH-.01f))
		{
			SelectCategory(c);
			return;
		}
	}
	for(int r=0;r<VISIBLEROWS&&scroll+r<numitems;r++)
	{
		float y=LISTTOP-r*ROWH;
		if(Inside(ITEMX-.02f,y-.01f,PANELX2,y+ROWH-.01f))
		{
			PickItem(scroll+r);
			return;
		}
	}
}

void BuildMenuWheel(int dir)
{
	if(!buildmenuopen) return;
	scroll-=dir;
	if(scroll>numitems-VISIBLEROWS) scroll=numitems-VISIBLEROWS;
	if(scroll<0) scroll=0;
}

//returns 1 when the key was used by the menu (all keys are, while it is open)
int BuildMenuHandleKey(int vk)
{
	if(!buildmenuopen) return 0;
	if(vk==VK_ESCAPE||vk==controls.build||vk==VK_RBUTTON) CloseBuildMenu();
	if(vk==VK_UP) BuildMenuWheel(1);
	if(vk==VK_DOWN) BuildMenuWheel(-1);
	return 1;
}

//structure name without its extension
static void DisplayName(const char *file,char *out,int size)
{
	strncpy(out,file,size-1);
	out[size-1]=0;
	char *dot=strrchr(out,'.');
	if(dot&&dot!=out) *dot=0;
}

void DrawBuildMenu()
{
	if(!buildmenuopen) return;
	glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_TEXTURE_2D);
	glColor4f(0,0,0,.8f);
	glBegin(GL_QUADS);
	glVertex2f(PANELX1,PANELY1);
	glVertex2f(PANELX2,PANELY1);
	glVertex2f(PANELX2,PANELY2);
	glVertex2f(PANELX1,PANELY2);
	glEnd();
	//highlight of the selected category
	if(curcategory<numcategories&&curcategory<VISIBLEROWS)
	{
		float y=LISTTOP-curcategory*ROWH;
		glColor4f(1,1,.5f,.15f);
		glBegin(GL_QUADS);
		glVertex2f(CATX-.03f,y-.012f);
		glVertex2f(ITEMX-.1f,y-.012f);
		glVertex2f(ITEMX-.1f,y+ROWH-.012f);
		glVertex2f(CATX-.03f,y+ROWH-.012f);
		glEnd();
	}
	glEnable(GL_TEXTURE_2D);

	glColor3f(1,1,.5f);
	DrawText(CATX,.68f,"Build");
	glColor3f(.6f,.6f,.6f);
	DrawText(ITEMX,.68f,numcategories?"Pick a structure":"No structures found");

	char name[NAMELEN];
	for(int c=0;c<numcategories&&c<VISIBLEROWS;c++)
	{
		float y=LISTTOP-c*ROWH;
		int hover=Inside(CATX-.02f,y-.01f,ITEMX-.1f,y+ROWH-.01f);
		if(c==curcategory) glColor3f(1,1,.5f);
		else if(hover) glColor3f(1,1,1);
		else glColor3f(.75f,.75f,.75f);
		strncpy(name,categories[c],13);
		name[13]=0;
		DrawText(CATX,y,name);
	}
	for(int r=0;r<VISIBLEROWS&&scroll+r<numitems;r++)
	{
		float y=LISTTOP-r*ROWH;
		int hover=Inside(ITEMX-.02f,y-.01f,PANELX2,y+ROWH-.01f);
		glColor3f(hover?1:.85f,hover?1:.85f,hover?1:.85f);
		DisplayName(items[scroll+r],name,27);
		DrawText(ITEMX,y,name);
	}
	glColor3f(.6f,.6f,.6f);
	if(numitems>VISIBLEROWS)
		DrawText(ITEMX,PANELY1+.1f,"Mouse wheel: more");
	DrawText(CATX,PANELY1+.03f,"Esc or right click: close");
	glPopAttrib();
	glColor3f(1,1,1);
}
