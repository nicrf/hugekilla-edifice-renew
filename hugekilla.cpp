// hugekilla.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "resource.h"
#include "land.h"
#include "redrawing.h"
#include "vector.h"
#include "playing.h"
#include "mmsystem.h"
#include "multiplay.h"
#include "sound.h"
#include "commdlg.h"
#include "ui.h"

#define MAX_LOADSTRING 100
#define WM_MOUSEWHEEL 0x020A


extern PhysicsVars physics;
extern int consolevisible;
extern GraphicsPrefs graphicsprefs;
extern Block blocks[BLOCKGRIDSIZE][BLOCKGRIDHEIGHT][BLOCKGRIDSIZE];
extern char nextblock[BLOCKGRIDSIZE][BLOCKGRIDHEIGHT][BLOCKGRIDSIZE];
extern DWORD curtime;

void Save(char *filename);
void Load(char *filename);

LandDefs ldfs;
#ifdef _WIN32  // Win32 window, GL context and message loop; see platform_sdl.cpp otherwise
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
	
	LoadTexture("textures\\fonts\\courier.pxr",300);
	LoadTexture("textures\\startup.bmp",800);
	
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
	RedrawSplash(1,0,1);
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
					//ofn.lpstrInitialDir="objects\\";
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
					//ofn.lpstrInitialDir="objects\\";
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
						LoadTexture("textures\\startup.bmp",1);
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

#endif // _WIN32

//wrappers

LandDefs *GetDefs()
{
	return &ldfs;
}
#ifdef _WIN32
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


//saves a landscape and buildings

void Save(char *filename)
{
	if(!filename) return;
	HANDLE file=CreateFile(filename,GENERIC_WRITE,0,0,OPEN_ALWAYS,0,0);
	DWORD bw;
	WriteFile(file,"edifice",7,&bw,0);
	WriteFile(file,GetSkySettings(),sizeof(SkySettings),&bw,0);
	WriteFile(file,&ldfs,sizeof(ldfs),&bw,0);
	WriteFile(file,blocks,sizeof(blocks),&bw,0);
	WriteFile(file,nextblock,sizeof(nextblock),&bw,0);
	CloseHandle(file);
}

//loads landscape

void Load(char *filename)
{
	if(!filename) return;
	StartServer();
	
	HANDLE file=CreateFile(filename,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	DWORD br;
	if(file==INVALID_HANDLE_VALUE) return;
	char sig[7];
	ReadFile(file,sig,7,&br,0);
	ReadFile(file,GetSkySettings(),sizeof(SkySettings),&br,0);
	ReadFile(file,&ldfs,sizeof(ldfs),&br,0);
	char fullfilename[400];
	if(strlen(GetSkySettings()->cloudtex))
	{
		strcpy(fullfilename,"textures\\clouds\\");
		strcat(fullfilename,GetSkySettings()->cloudtex);
		LoadTexture(fullfilename,2);
	}
	if(strlen(GetSkySettings()->misttex))
	{
		strcpy(fullfilename,"textures\\clouds\\");
		strcat(fullfilename,GetSkySettings()->misttex);
		LoadTexture(fullfilename,6);
	}
					
	CreateLand(ldfs);
	ReadFile(file,blocks,sizeof(blocks),&br,0);
	ReadFile(file,nextblock,sizeof(nextblock),&br,0);
	
	CloseHandle(file);
	SendLandInfo(0);
					
}