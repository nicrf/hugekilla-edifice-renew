// hugekilla.cpp : Defines the entry point for the application.
//

#include "platform.h"
#include "fileio.h"
#include "hugekilla.h"
#include "gameplay.h"
#include "resource.h"
#include "land.h"
#include "redrawing.h"
#include "vector.h"
#include "playing.h"
#include "multiplay.h"
#include "sound.h"
#ifdef HK_WIN32_NATIVE   //the original Win32 window (never built): WGL and the file dialogs
#include "gl.h"
#include <commdlg.h>   //GetOpenFileName, left out by WIN32_LEAN_AND_MEAN
#endif
#include "ui.h"
#include "wire.h"
#include "landwire.h"
#include "mapfile.h"
#include <time.h>

#define MAX_LOADSTRING 100
#define WM_MOUSEWHEEL 0x020A


void Save(char *filename);
void Load(char *filename);

LandDefs ldfs;
#ifdef HK_WIN32_NATIVE  // Win32 window, GL context and message loop; see platform_sdl.cpp otherwise
POINT centermouse;
HBRUSH backbrush;
int originalscreenwidth;
int originalscreenheight;
int originalscreendepth;

// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];								// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];								// The title bar text

int lockedcursor;
HGLRC hg;
HWND hWnd;
HDC hdc;
//HDC fullscreenhdc;
Vector mousepos;

char status[10][200];
int timetotick=10;

// Foward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
void SetScreenMode();
int InitGL(HWND hWnd);

HWND GetHWND()
{
	return hWnd;
}

DEVMODE originalscreensettings;
DEVMODE newscreensettings;

int curscreenmode;


//main function

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{
 	MSG msg;
	HACCEL hAccelTable;

	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_HUGEKILLA, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);
	
	CfgFile controls;
	controls.Load("control.ini");

	curscreenmode=controls.GetValuei("changeres")!=0;
	EnumDisplaySettings(0,ENUM_CURRENT_SETTINGS,&originalscreensettings);
	ZeroMemory(&newscreensettings,sizeof(newscreensettings));
	newscreensettings.dmSize=sizeof(newscreensettings);
	newscreensettings.dmPelsWidth=controls.GetValuei("screenwidth");
	newscreensettings.dmPelsHeight=controls.GetValuei("screenheight");
	newscreensettings.dmBitsPerPel=controls.GetValuei("screendepth");
	newscreensettings.dmFields=DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT;
	graphicsprefs.stereoseparation=controls.GetValuef("stereoseparation");

	srand( (unsigned)time( NULL ) );
	
	if (!InitInstance (hInstance, nCmdShow)) 
	{
		return FALSE;
	}
	InitConsole();
	
	hdc=GetDC(hWnd);
	
	RECT backrect;
	GetClientRect(hWnd,&backrect);

	backbrush=CreateSolidBrush(0x000000);
	
	
#ifndef HKDS 
	InitGL(hWnd);
#endif
	
	LoadStartupTextures();
	
	consolevisible=1;
#ifndef HKDS
	InitSound(hWnd);
	LoadSounds();
#endif
	LoadHighTextures();
	
	InitSocks();
	SetTimer(hWnd,1,200,0);
	hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)IDC_HUGEKILLA);
	
	for(int i=0;i<MAXPLAYERS;i++)
	{
		GetPlayer(i)->playstate=0;
		GetPlayer(i)->dir=Vector(1,0,0);
		GetPlayer(i)->tilt=Vector(1,0,0);
		GetPlayer(i)->fwd=Vector(1,0,0);
		GetPlayer(i)->gunup=Vector(0,1,0);
		GetPlayer(i)->fzoom=0;
	}
	LoadControls();
	RedrawSplash(TEX_SPLASH,0,1);
	PlaySound(0,1);
#ifdef HKDS
	StartServer();
#endif

	while (1) 
	{
		while (1) 
		{
			if(hWnd)
			{
				if(!PeekMessage(&msg, NULL, 0, 0,PM_REMOVE))
				{
					break;
				}
				
			}
			else
			{
				WaitMessage();
				PeekMessage(&msg, NULL, 0, 0,PM_REMOVE);
			}
			if(msg.message==WM_QUIT)
			{
				curscreenmode=0;
				SetScreenMode();
#ifndef HKDS
				TexShutdown();
				wglMakeCurrent(0,0);
				wglDeleteContext(hg);
#endif
				ReleaseDC(hWnd,hdc);
				CloseSocks();
				KillTimer(hWnd,1);
				return msg.wParam;
			}
			if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) 
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		LARGE_INTEGER time;
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&time);
		
		curtime=((double)time.QuadPart)/(double)freq.QuadPart*1000;
	
		int redraw=0;
		if(GetForegroundWindow()==hWnd)
			redraw=1;
		else
			Sleep(4);
		if(GetLocalPlayer()->playstate==0||lockedcursor==0)
		{
			if(redraw) 
				DrawMenu();
			if(GetLocalPlayer()->playstate!=0)
				Playloop();
			SockLoop();
		}
		else
		{
			if(GetLocalPlayer()->playstate==1)
			{
				Playloop();
				SockLoop(); 
				if(redraw) DrawConsole();
			}
			else
			{
				Playloop();
				SockLoop();
				if(redraw) Redraw();
			}
		}
	}
#ifndef HKDS
	TexShutdown();
	wglMakeCurrent(0,0);
	wglDeleteContext(hg);
#endif
	ReleaseDC(hWnd,hdc);
	CloseSocks();
	KillTimer(hWnd,1);
	return msg.wParam;
}



ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX); 

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= (WNDPROC)WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_ICON1);
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
#ifndef HKDS
	wcex.lpszMenuName	= 0;
#endif
#ifdef HKDS
	wcex.lpszMenuName	= 0;//(LPCTSTR)IDC_HUGEKILLA;
#endif
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, (LPCTSTR)IDI_ICON1);

	return RegisterClassEx(&wcex);
}



void SetScreenMode()
{
	int olockedcursor=lockedcursor;
	if(curscreenmode==1)
		ChangeDisplaySettings(&newscreensettings,CDS_FULLSCREEN);
	else
		ChangeDisplaySettings(&originalscreensettings,0);
	
	ShowWindow(hWnd, SW_HIDE);
	if(curscreenmode==0)
	{
		
		SetWindowLong(hWnd,GWL_STYLE,WS_OVERLAPPEDWINDOW);
		MoveWindow(hWnd,0,0,1024,800,0);
	}
	else
	{
		
		SetWindowLong(hWnd,GWL_STYLE,0);
		MoveWindow(hWnd,0,0,newscreensettings.dmPelsWidth,newscreensettings.dmPelsHeight,0);
	}
	ShowWindow(hWnd, SW_SHOW);

	UpdateWindow(hWnd);
	lockedcursor=olockedcursor;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      0, 0, 1024, 700, NULL, NULL, hInstance, NULL);
	if (!hWnd)
   {
      return FALSE;
   }
	//ShowWindow(hWnd, nCmdShow);
	//UpdateWindow(hWnd);
   SetScreenMode();
	
	
	return TRUE;
}

int InitGL(HWND hWnd)
{
	
	PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),  /* size */
        1,                              /* version */
       PFD_SUPPORT_OPENGL |
        PFD_DRAW_TO_WINDOW |
		PFD_DOUBLEBUFFER,               /* support double-buffering */
        PFD_TYPE_RGBA,                  /* color type */
        16,                             /* prefered color depth */
        0, 0, 0, 0, 0, 0,               /* color bits (ignored) */
        0,                              /* no alpha buffer */
        0,                              /* alpha bits (ignored) */
        0,                              /* no accumulation buffer */
        0, 0, 0, 0,                     /* accum bits (ignored) */
        16,                             /* depth buffer */
        0,                              /* no stencil buffer */
        0,                              /* no auxiliary buffers */
        PFD_MAIN_PLANE,                 /* main layer */
        0,                              /* reserved */
        0, 0, 0,                        /* no layer, visible, damage masks */
    };
    int pixelFormat=2;

    pixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (pixelFormat == 0) {
        MessageBox(hWnd,"ChoosePixelFormat failed.", "Error",
                MB_ICONERROR | MB_OK);
        exit(1);
    }

    if (SetPixelFormat(hdc, pixelFormat, &pfd) != TRUE) {
        MessageBox(hWnd,"SetPixelFormat failed.", "Error",
                MB_ICONERROR | MB_OK);
	}
	
	
	hg=wglCreateContext(hdc);
	if(!hg) exit(1);
	int a=wglMakeCurrent(hdc,hg);
	
	
	return 0;
}






void GetMouseChange(int &x,int &y)
{
	if(lockedcursor==0)
	{
		x=0;
		y=0;
		return;
	}
	POINT np;
	GetCursorPos(&np);
	x=np.x-centermouse.x;
	y=np.y-centermouse.y;
	SetCursorPos(centermouse.x,centermouse.y);
}

void LockCursor(int lock)
{
#ifdef HKDS
	return;
#endif
	if(lock==0)
	{
		if(lockedcursor==1)
		{
			ShowCursor(1);
			lockedcursor=0;
		}
	}
	else
	{
		if(lockedcursor==0)
		{
			RECT winrect;
			lockedcursor=1;
			GetWindowRect(hWnd,&winrect);
			centermouse.x=winrect.left+(winrect.right-winrect.left)/2;
			centermouse.y=winrect.top+(winrect.bottom-winrect.top)/2;
			ShowCursor(0);
			SetCursorPos(centermouse.x,centermouse.y);
		}
	}
}

//window message handler

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hdc;
	int wmId, wmEvent;
	int extended=0;
	RECT winrect;
	PAINTSTRUCT ps;
	switch (message) 
	{
	case WM_CREATE:
		
		break;
	case WM_RBUTTONDOWN:
		HandleKey(VK_RBUTTON,0);
		break;
	case WM_RBUTTONUP:
		HandleKeyUp(VK_RBUTTON,0);

		break;
	case WM_MOUSEWHEEL:
		{
			int ud=(short)HIWORD(wParam)/120;
			if(menu==BUILDING&&ud)
			{
				BuildPreviewWheel(ud,(GetKeyState(VK_CONTROL)&0x8000)!=0);   //Ctrl: raise/lower
				break;
			}
			if(ud==-1) SwitchGun(50);
			if(ud==1) SwitchGun(51);
			break;
		}
	case WM_MOUSEMOVE:
		{
			RECT winrect;
			GetClientRect(hWnd,&winrect);
			mousepos.x=(float)LOWORD(lParam)/(winrect.right-winrect.left)*2-1;
			mousepos.y=(float)HIWORD(lParam)/(winrect.bottom-winrect.top)*-2+1;
			break;
		}
	case WM_LBUTTONDOWN:
#ifdef HKDS
		break;
#endif
		if(GetLocalPlayer()->playstate==0||lockedcursor==0) 
		{
			MenuMouseDown();
			break;
		}
		HandleKey(VK_LBUTTON,0);
		break;
	case WM_LBUTTONUP:
#ifdef HKDS
		break;
#endif
		HandleKeyUp(VK_LBUTTON,0);
		
		break;
	case WM_SIZE:
#ifndef HKDS
		glViewport(0,0,LOWORD(lParam),HIWORD(lParam));
#endif
		
		break;
	case WM_KILLFOCUS:
		if(lockedcursor==1)
		{
			LockCursor(0);
		}
		
		break;
	case WM_PAINT:
			hdc = BeginPaint(hWnd, &ps);
			DrawConsoleNonGL();
			EndPaint(hWnd, &ps);
			break;
		InvalidateRect(hWnd,0,0);
		return 1;
		break;
	case WM_SYSCHAR:
		break;

	case WM_SYSKEYDOWN:
		if(GetLocalPlayer()->playstate==0) return DefWindowProc(hWnd, message, wParam, lParam);
	case WM_KEYDOWN:
		//break;
#ifdef HKDS
		HandleConsoleKey(wParam,lParam);
		break;
#endif
		if(wParam==27&&!GetLocalPlayer()->talking)
		{
			if(lockedcursor==1)
			{
				LockCursor(0);
			}
			else
			{
				if(GetLocalPlayer()->playstate!=0)
				{
					LockCursor(1);
				}
			}

			break;
		}
		if(wParam==VK_F6)
		{
			curscreenmode=1-curscreenmode;
			SetScreenMode();
			if(lockedcursor==1)
			{
				GetWindowRect(hWnd,&winrect);
				centermouse.x=winrect.left+(winrect.right-winrect.left)/2;
				centermouse.y=winrect.top+(winrect.bottom-winrect.top)/2;
				ShowCursor(0);
				SetCursorPos(centermouse.x,centermouse.y);
			}
		}
		HandleKey(wParam,lParam);
		break;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		
		HandleKeyUp(wParam,lParam);
		break;
	case WM_COMMAND:
			wmId    = LOWORD(wParam); 
			wmEvent = HIWORD(wParam); 
			// Parse the menu selections:
			switch (wmId)
			{
			case SAVE:
				{
					OPENFILENAME ofn;
					char fn[1000];
					fn[0]=0;
					ZeroMemory(&ofn,sizeof(ofn));
					ofn.lStructSize=sizeof(ofn);
					ofn.hwndOwner=hWnd;
					ofn.lpstrFilter="Edifice files (*.ef)\0*.ef\0All files (*.*)\0*.*\0\0";
					//ofn.lpstrInitialDir="objects/";
					ofn.lpstrFile=fn;
					ofn.nMaxFile=1000;
					ofn.lpstrDefExt="ef";
					ofn.Flags=OFN_FILEMUSTEXIST|OFN_NONETWORKBUTTON|OFN_NOREADONLYRETURN|OFN_NOCHANGEDIR;
					if(GetSaveFileName(&ofn))
					{
						Save(ofn.lpstrFile);
						
					}

				}
				break;
				case LOAD:
				{
					OPENFILENAME ofn;
					char fn[1000];
					fn[0]=0;
					ZeroMemory(&ofn,sizeof(ofn));
					ofn.lStructSize=sizeof(ofn);
					ofn.hwndOwner=hWnd;
					ofn.lpstrFilter="Text files (*.ef)\0*.ef\0All files (*.*)\0*.*\0\0";
					//ofn.lpstrInitialDir="objects/";
					ofn.lpstrFile=fn;
					ofn.nMaxFile=1000;
					ofn.Flags=OFN_FILEMUSTEXIST|OFN_NONETWORKBUTTON|OFN_NOREADONLYRETURN|OFN_NOCHANGEDIR;
					if(GetOpenFileName(&ofn))
					{
						Load(ofn.lpstrFile);
					}

				}
				break;
				case ID_FILE_NEWSKY:
				LoadSky();
				LightLand(GetSkySettings()->sunheight);
				break;
				case ID_MULTIPLAY_STARTSERVER:   //not in use
					{
						HMENU menu=GetMenu(hWnd);
						EnableMenuItem(menu,ID_MULTIPLAY_JOINGAME,MF_GRAYED);
						EnableMenuItem(menu,ID_MULTIPLAY_STARTSERVER,MF_GRAYED);
						EnableMenuItem(menu,ID_MULTIPLAY_DISCONNECT,MF_ENABLED);
						
					}
					break;
				case ID_MULTIPLAY_DISCONNECT:
					{
						Disconnect();
						GetLocalPlayer()->playstate=0;
						HMENU menu=GetMenu(hWnd);
						EnableMenuItem(menu,ID_MULTIPLAY_JOINGAME,MF_ENABLED);
						EnableMenuItem(menu,ID_MULTIPLAY_STARTSERVER,MF_ENABLED);
						EnableMenuItem(menu,ID_MULTIPLAY_DISCONNECT,MF_GRAYED);
						LoadTexture("textures/startup.bmp",TEX_LAND);   //legacy Win32 menu: the land slot, not TEX_SPLASH (see CreateLand)
					}
					break;
				case ID_MULTIPLAY_JOINGAME:
					if(DialogBox(hInst, (LPCTSTR)IDD_MULTIPLAY, hWnd, (DLGPROC)Multiplay))
					{
						HMENU menu=GetMenu(hWnd);
						EnableMenuItem(menu,ID_MULTIPLAY_JOINGAME,MF_GRAYED);
						EnableMenuItem(menu,ID_MULTIPLAY_STARTSERVER,MF_GRAYED);
						EnableMenuItem(menu,ID_MULTIPLAY_DISCONNECT,MF_ENABLED);
					}
					break;

				case ID_FILE_NEWLANDSCAPE:
					if(IsServer())
					{
						RandomLand(ldfs);
						SendLandInfo(0);
						CreateLand(ldfs);
						GetLocalPlayer()->playstate=1;
#ifndef HKDS
						GetLocalPlayer()->playstate=10;
#endif

					}
					
					break;
				case IDM_ABOUT:
				   DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
				   break;
				case IDM_EXIT:
				   DestroyWindow(hWnd);
				   break;
				default:
				   return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_TIMER:
			timetotick--;
			if(timetotick<0)
			{
				timetotick=5;
				for(int i=0;i<9;i++)
				{
					strcpy(status[i],status[i+1]);
				}
			}
			if(GetFocus()==hWnd) break;
			SockLoop();
			Playloop();
		break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}

// Mesage handler for about box  (not in use)
LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
				return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				EndDialog(hDlg, LOWORD(wParam));
				return TRUE;
			}
			break;
	}
    return FALSE;
}

#endif // HK_WIN32_NATIVE

//wrappers

LandDefs *GetDefs()
{
	return &ldfs;
}
#ifdef HK_WIN32_NATIVE
HDC GetGlHDC()
{
	return hdc;
}
#endif

//general function used to make and object as close as possible to the player
//(since the world is repeating), and returns 1 if it is close enough to draw

int MakeClose(Vector *pos)
{
	Vector ppos=GetLocalPlayer()->pos;
	Vector pdir=GetLocalPlayer()->dir;
	if(pos->x<ppos.x-LANDGRIDSIZE/2)
		pos->x+=LANDGRIDSIZE;
	else
		if(pos->x>ppos.x+LANDGRIDSIZE/2)
			pos->x-=LANDGRIDSIZE;
	if(pos->z<ppos.z-LANDGRIDSIZE/2)
		pos->z+=LANDGRIDSIZE;
	else
		if(pos->z>ppos.z+LANDGRIDSIZE/2)
			pos->z-=LANDGRIDSIZE;
	if(fabs(pos->x-ppos.x)<graphicsprefs.drawdistance)
	if(fabs(pos->z-ppos.z)<graphicsprefs.drawdistance)
	{
		if(DotProduct(*pos-(ppos-pdir*2),pdir)>0)
			return 1;
	}
	return 0;
}


//---- map save files (roadmap M6, format in src/game/mapfile.h) ----

static char servermappath[1200];   //map the server plays (reloaded at each new round), "" = random land
static MapData *pendingmap;         //read by LoadMapPath, installed by StartServer

static void FreeMapData(MapData *map)
{
	if(!map) return;
	free(map->blocks);
	free(map->nextblock);
	free(map);
}

static MapData *NewMapData()
{
	MapData *map=(MapData *)calloc(1,sizeof(MapData));
	if(!map) return 0;
	map->blocks=(Block *)malloc(sizeof(Block)*(size_t)MAPNUMBLOCKS);
	map->nextblock=(char *)malloc(MAPNUMBLOCKS);
	if(!map->blocks||!map->nextblock)
	{
		FreeMapData(map);
		return 0;
	}
	return map;
}

int ValidMapName(const char *name)
{
	int len=0;
	for(;name[len];len++)
	{
		char c=name[len];
		if(!(c>='A'&&c<='Z'||c>='a'&&c<='z'||c>='0'&&c<='9'||c=='_'||c=='-')) return 0;
		if(len>=MAXMAPNAME) return 0;
	}
	return len>0;
}

//maps/<name>.map in the user folder
const char *MapPath(const char *name)
{
	static char path[1300];
	char rel[64];
	snprintf(rel,sizeof(rel),"maps/%s.map",name);
	snprintf(path,sizeof(path),"%s",UserFile(rel));
	return path;
}

//names (without .map) of the maps in the user maps folder, sorted
int ListSavedMaps(char (*names)[MAXMAPNAME+1],int max)
{
	std::vector<HKDirEntry> files=HKListDir(UserFile("maps"));
	int n=0;
	for(size_t i=0;i<files.size()&&n<max;i++)
	{
		std::string name=files[i].name;
		if(files[i].isdir||name.size()<5||!HKHasExt(name.c_str(),".map")) continue;
		name.resize(name.size()-4);
		if(!ValidMapName(name.c_str())) continue;
		strcpy(names[n++],name.c_str());
	}
	for(int i=1;i<n;i++)   //few files: insertion sort
		for(int j=i;j>0&&stricmp(names[j-1],names[j])>0;j--)
		{
			char tmp[MAXMAPNAME+1];
			strcpy(tmp,names[j]);
			strcpy(names[j],names[j-1]);
			strcpy(names[j-1],tmp);
		}
	return n;
}

//saves the land and the blocks; 1 if done, else 0 and a reason
int SaveMapPath(const char *path,char *err,int errsize)
{
	MapData map;
	map.sky=*GetSkySettings();
	map.defs=ldfs;
	map.blocks=&blocks[0][0][0];
	map.nextblock=&nextblock[0][0][0];
	size_t size;
	unsigned char *data=WriteMapFile(&map,&size);
	if(!data)
	{
		snprintf(err,errsize,"Out of memory");
		return 0;
	}
	//"wb" truncates: the original OPEN_ALWAYS left the end of a longer old file
	FILE *file=HKFileOpen(path,"wb");
	int ok=file&&fwrite(data,1,size,file)==size;
	if(file&&fclose(file)) ok=0;
	free(data);
	if(!ok) snprintf(err,errsize,"Can't write %s",path);
	return ok;
}

//reads and checks a map file: the map, or 0 and a reason
static MapData *ReadMapPath(const char *path,char *err,int errsize)
{
	FILE *file=HKFileOpen(path,"rb");
	if(!file)
	{
		snprintf(err,errsize,"No map file %s",path);
		return 0;
	}
	//size first: never allocate for a file no map can be
	long size=-1;
	if(!fseek(file,0,SEEK_END)) size=ftell(file);
	if(size<0||(size_t)size>MAPMAXSIZE||fseek(file,0,SEEK_SET))
	{
		fclose(file);
		snprintf(err,errsize,size<0?"Can't read %s":"Map file too large: %s",path);
		return 0;
	}
	unsigned char *data=(unsigned char *)malloc(size?size:1);
	MapData *map=NewMapData();
	int ok=data&&map&&fread(data,1,size,file)==(size_t)size;
	fclose(file);
	if(!ok) snprintf(err,errsize,data&&map?"Can't read %s":"Out of memory",path);
	else ok=ReadMapFile(data,size,map,err,errsize);
	free(data);
	if(!ok)
	{
		FreeMapData(map);
		return 0;
	}
	return map;
}

//replaces the world by the map. Clients get it like any land: LANDINFO, then all
//the blocks once they acknowledge it (multiplay.cpp, RECEIVEDLANDINFO)
static void InstallMap(MapData *map)
{
	ApplyLand(&map->defs,&map->sky);   //CreateLand clears the blocks
	CopyMemory(blocks,map->blocks,sizeof(blocks));
	CopyMemory(nextblock,map->nextblock,sizeof(nextblock));
	char text[200];
	snprintf(text,sizeof(text),map->format==1?"Loaded original save %s":"Loaded map %s",servermappath);
	ConsoleDisplayText(text);
}

//loads a map on the server, or starts a server with it, like the original
//Load(); refused while connected to someone else's server
int LoadMapPath(const char *path,char *err,int errsize)
{
	if(!IsServer()&&IsConnected())
	{
		snprintf(err,errsize,"Only the server can load a map");
		return 0;
	}
	MapData *map=ReadMapPath(path,err,errsize);
	if(!map) return 0;
	snprintf(servermappath,sizeof(servermappath),"%s",path);
	if(!IsServer())
	{
		FreeMapData(pendingmap);
		pendingmap=map;
		StartServer();   //installs pendingmap (StartServerMap)
		return 1;
	}
	InstallMap(map);
	FreeMapData(map);
	SendLandInfo(0);
	GRestartPlayers();
	return 1;
}

//StartServer: the map LoadMapPath read, if any (1), else a random land is made (0)
int StartServerMap()
{
	if(!pendingmap)
	{
		servermappath[0]=0;
		return 0;
	}
	InstallMap(pendingmap);
	FreeMapData(pendingmap);
	pendingmap=0;
	return 1;
}

//new round: the server's map again, with its original blocks (1), or 0 for a random land
int ReloadServerMap()
{
	if(!servermappath[0]) return 0;
	char err[300];
	MapData *map=ReadMapPath(servermappath,err,sizeof(err));
	if(!map)
	{
		ConsoleDisplayText(err);
		servermappath[0]=0;
		return 0;
	}
	InstallMap(map);
	FreeMapData(map);
	SendLandInfo(0);
	return 1;
}

//original menu entries (HK_WIN32_NATIVE)

void Save(char *filename)
{
	if(!filename) return;
	char err[300];
	if(!SaveMapPath(filename,err,sizeof(err))) ConsoleDisplayText(err);
}

void Load(char *filename)
{
	if(!filename) return;
	char err[300];
	if(!LoadMapPath(filename,err,sizeof(err))) ConsoleDisplayText(err);
}
