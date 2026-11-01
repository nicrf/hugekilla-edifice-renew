//build menu opened by the "build" key: structure categories (folders of data/Structures)
//on the left, their structures on the right, picked with the mouse. Picking one loads it
//as a preview in the world (menu BUILDING): left click builds it, right click cancels.

#include "gl.h"
#include "platform.h"
#include "fileio.h"
#include "redrawing.h"
#include "playing.h"
#include "ui.h"
#include "hkkeys.h"
#include <ctype.h>

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
#define TITLEH .12f
#define LISTTOP .64f           //top of the first row
#define ROWH .065f
#define VISIBLEROWS 16
#define CATX -.82f
#define ITEMX -.22f
#define CATX1 (CATX-.04f)      //click and hover areas of the two lists
#define CATX2 (ITEMX-.08f)
#define ITEMX1 (ITEMX-.04f)
#define ITEMX2 (PANELX2-.04f)

static int Inside(float x1,float y1,float x2,float y2)
{
	return mousepos.x>x1&&mousepos.x<x2&&mousepos.y>y1&&mousepos.y<y2;
}

//bottom of a row
static float RowY(int row)
{
	return LISTTOP-(row+1)*ROWH;
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
	std::vector<HKDirEntry> entries=HKListDir(folder);
	int n=0;
	for(size_t i=0;i<entries.size()&&n<MAXENTRIES;i++)
	{
		if(entries[i].name[0]=='.'||entries[i].isdir!=wantdirs) continue;
		strncpy(out[n],entries[i].name.c_str(),NAMELEN-1);
		out[n][NAMELEN-1]=0;
		n++;
	}
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
	sprintf(folder,"structures/%s",categories[c]);
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
	sprintf(path,"structures/%s/%s",categories[curcategory],items[i]);
	CloseBuildMenu();
	if(!LoadStructure(path))
	{
		ConsoleDisplayText("Could not load that structure");
		return;
	}
	menu=BUILDING;
	BuildPreviewText();
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
		if(Inside(CATX1,RowY(c),CATX2,RowY(c)+ROWH))
		{
			SelectCategory(c);
			return;
		}
	for(int r=0;r<VISIBLEROWS&&scroll+r<numitems;r++)
		if(Inside(ITEMX1,RowY(r),ITEMX2,RowY(r)+ROWH))
		{
			PickItem(scroll+r);
			return;
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
	if(vk==KEY_ESCAPE||vk==controls.build||vk==KEY_RBUTTON) CloseBuildMenu();
	if(vk==KEY_UP) BuildMenuWheel(1);
	if(vk==KEY_DOWN) BuildMenuWheel(-1);
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
	glEnable(GL_TEXTURE_2D);
	UIPanel(PANELX1,PANELY1,PANELX2,PANELY2,TITLEH,"Build");
	glColor3f(.6f,.6f,.6f);
	const char *hint=numcategories?"Pick a structure":"No structures found";
	UIText(PANELX2-.05f-UITextWidth(hint),UITextY(PANELY2-TITLEH,PANELY2),hint);
	UIRect(CATX2+.02f,RowY(VISIBLEROWS-1),CATX2+.024f,LISTTOP,1,1,1,.12f);   //column separator

	char name[NAMELEN];
	for(int c=0;c<numcategories&&c<VISIBLEROWS;c++)
	{
		float y1=RowY(c),y2=y1+ROWH;
		int hover=Inside(CATX1,y1,CATX2,y2);
		if(c==curcategory) UIRect(CATX1,y1,CATX2,y2,1,1,.5f,.15f);
		else if(hover) UIRect(CATX1,y1,CATX2,y2,1,1,1,.06f);
		if(c==curcategory) glColor3f(1,1,.5f);
		else if(hover) glColor3f(1,1,1);
		else glColor3f(.75f,.75f,.75f);
		UITextFit(CATX,UITextY(y1,y2),categories[c],CATX2-CATX-.02f);
	}
	for(int r=0;r<VISIBLEROWS&&scroll+r<numitems;r++)
	{
		float y1=RowY(r),y2=y1+ROWH;
		int hover=Inside(ITEMX1,y1,ITEMX2,y2);
		if(hover) UIRect(ITEMX1,y1,ITEMX2,y2,1,1,1,.08f);
		glColor3f(hover?1:.85f,hover?1:.85f,hover?1:.85f);
		DisplayName(items[scroll+r],name,NAMELEN);
		UITextFit(ITEMX,UITextY(y1,y2),name,ITEMX2-ITEMX-.02f);
	}
	glColor3f(.45f,.45f,.45f);
	if(numitems>VISIBLEROWS)
	{
		char more[64];
		sprintf(more,"%d-%d of %d    Mouse wheel: more",scroll+1,scroll+VISIBLEROWS,numitems);
		UIText(ITEMX,UITextY(PANELY1+.02f,PANELY1+.1f,.04f),more,.04f);
	}
	UIText(CATX,UITextY(PANELY1+.02f,PANELY1+.1f,.04f),"Esc or right click: close",.04f);
	glPopAttrib();
	glColor3f(1,1,1);
}
