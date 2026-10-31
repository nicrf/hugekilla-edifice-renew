//used to draw menus (only the main menu now), see if they are clicked, and take the appropriate action


#include "StdAfx.h"
#include "multiplay.h"
#include "ui.h"
#include "vector.h"
#include "redrawing.h"

Button button[50];
int numbuttons;
int uicurmenu;
int showcredits;
extern Vector mousepos;

//text button that opens the credits screen
#define CREDITSX1 -.1f
#define CREDITSY1 -.97f
#define CREDITSX2 .3f
#define OPTIONSX1 -.6f
#define OPTIONSX2 -.2f
#define CREDITSY2 -.87f

static int OverOptionsButton()
{
	return mousepos.x>OPTIONSX1&&mousepos.x<OPTIONSX2&&mousepos.y>CREDITSY1&&mousepos.y<CREDITSY2;
}

static int OverCreditsButton()
{
	return mousepos.x>CREDITSX1&&mousepos.x<CREDITSX2&&mousepos.y>CREDITSY1&&mousepos.y<CREDITSY2;
}

//returns 1 if the credits screen was open

int CloseCredits()
{
	int was=showcredits;
	showcredits=0;
	return was;
}

static void DrawCredits()
{
	glDisable(GL_TEXTURE_2D);
	glColor4f(0,0,0,.85f);
	glBegin(GL_QUADS);
	glVertex2f(-.9f,-.85f);
	glVertex2f(.9f,-.85f);
	glVertex2f(.9f,.75f);
	glVertex2f(-.9f,.75f);
	glEnd();
	glEnable(GL_TEXTURE_2D);
	float y=.62f;
	glColor3f(1,1,.5f);
	DrawText(-.8f,y,"Edifice (HugeKilla) - Credits"); y-=.16f;
	glColor3f(.7f,.7f,.7f);
	DrawText(-.8f,y,"Original authors"); y-=.1f;
	glColor3f(1,1,1);
	DrawText(-.8f,y,"Programming: Nelson Planting"); y-=.08f;
	DrawText(-.8f,y,"Models/Graphics: John Thomas"); y-=.16f;
	glColor3f(.7f,.7f,.7f);
	DrawText(-.8f,y,"License"); y-=.1f;
	glColor3f(1,1,1);
	DrawText(-.8f,y,"GNU LGPL v3 (see license.txt)"); y-=.08f;
	DrawText(-.8f,y,"sourceforge.net/p/hugekilla"); y-=.16f;
	glColor3f(.7f,.7f,.7f);
	DrawText(-.8f,y,"Ports"); y-=.1f;
	glColor3f(1,1,1);
	DrawText(-.8f,y,"macOS/SDL2 port based on the"); y-=.08f;
	DrawText(-.8f,y,"original source code"); y-=.16f;
	glColor3f(.6f,.6f,.6f);
	DrawText(-.8f,-.78f,"Click or press Esc to close");
	glColor3f(1,1,1);
}

extern Vector mousepos;


extern HDC hdc;
extern HINSTANCE hInst;

//adds a button to the screen

void AddButton(int handler, int tex, int texpos,float x1,float y1,float x2,float y2)
{
	button[numbuttons].handler=handler;
	button[numbuttons].tex=tex;
	button[numbuttons].texpos=texpos;
	button[numbuttons].x1=x1;
	button[numbuttons].y1=y1;
	button[numbuttons].x2=x2;
	button[numbuttons].y2=y2;
	numbuttons++;
}

void DeleteButtons()
{
	numbuttons=0;
}


//places the main menu on the screen

void MainMenuButtons()
{
	DeleteButtons();
	AddButton(4,0,3,-.6,-.8, -.2 , -.6);
	if(GetLocalPlayer()->playstate!=0) AddButton(3,0,2,-.6,-.6, -.2 , -.4);
	if(GetLocalPlayer()->playstate==0)
	{
		AddButton(2,0,1,-.6, -.4, -.2 ,-.2);
		AddButton(1,0,0,-.6, -.2, -.2 ,  0);
	}
}

int ButtonOver()
{
	for(int i=0;i<numbuttons;i++)
	{
		if(mousepos.x>button[i].x1&&mousepos.y>button[i].y1&&
			mousepos.x<button[i].x2&&mousepos.y<button[i].y2)
			return i;
	}
	return -1;
}


void DrawMenu()
{
	int over=ButtonOver();	
	glClearColor(0,0,0,0);
	if(GetLocalPlayer()->playstate==0)
		RedrawSplash(800,0,1);
	else
		glClear(GL_COLOR_BUFFER_BIT);
	MainMenuButtons();
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	for(int i=0;i<numbuttons;i++)
	{
		glBindTexture(GL_TEXTURE_2D,1300+button[i].tex);
		if(i==over)
			glColor3f(1,1,1);
		else
			glColor3f(.7,.7,.7);
		glBegin(GL_QUADS);
		glTexCoord2f(0,((float)button[i].texpos+1)/4);
		glVertex2f(button[i].x1,button[i].y1);
		glTexCoord2f(1,((float)button[i].texpos+1)/4);
		glVertex2f(button[i].x2,button[i].y1);
		glTexCoord2f(1,(float)button[i].texpos/4);
		glVertex2f(button[i].x2,button[i].y2);
		glTexCoord2f(0,(float)button[i].texpos/4);
		glVertex2f(button[i].x1,button[i].y2);
		glEnd();
	}
	glColor3f(1,1,1);
	if(GetLocalPlayer()->playstate!=0)
		DrawText(-.8,.8,"Press escape to resume game");
	{
		char version[64];
		sprintf(version,"v%s (protocol %d)",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
		glColor3f(.5,.5,.5);
		DrawText(.97f-strlen(version)*.04f,.9f,version);
		glColor3f(1,1,1);
	}
	if(OverCreditsButton()&&!showcredits)
		glColor3f(1,1,1);
	else
		glColor3f(.7,.7,.7);
	DrawText(CREDITSX1+.06f,CREDITSY1+.015f,"Credits");
	glColor3f(1,1,1);
#ifndef HK_WIN32_NATIVE
	if(OverOptionsButton()&&!showcredits&&!IsOptionsOpen())
		glColor3f(1,1,1);
	else
		glColor3f(.7,.7,.7);
	DrawText(OPTIONSX1+.06f,CREDITSY1+.015f,"Options");
	glColor3f(1,1,1);
	DrawOptions();
	DrawServerSetup();
#endif
	if(showcredits)
		DrawCredits();
#ifndef HK_WIN32_NATIVE
	DrawJoinScreen();
#endif

	SwapBuffers(hdc);	
}


void MenuMouseDown()
{
	if(CloseCredits()) return;
#ifndef HK_WIN32_NATIVE
	if(IsOptionsOpen())
	{
		OptionsClick();
		return;
	}
	if(IsServerSetupOpen())
	{
		ServerSetupClick();
		return;
	}
	if(OverOptionsButton())
	{
		OpenOptions();
		return;
	}
#endif
	if(OverCreditsButton())
	{
		showcredits=1;
		return;
	}
#ifndef HK_WIN32_NATIVE
	if(IsJoinScreenOpen()) return;
#endif
	int over=ButtonOver();
	if(over>=0)
		HandleButton(button[over].handler);
}
extern LandDefs ldfs;
extern HWND hWnd;

void HandleButton(int handler)
{
	switch (handler)
	{
	case 0:  //error
		break;
	case 1:  //start server
#ifndef HK_WIN32_NATIVE
		OpenServerSetup();
#else
		StartServer();
#endif
		break;
	case 2:  //play
		if(GetLocalPlayer()->playstate==0)
#ifdef HK_WIN32_NATIVE
			DialogBox(hInst, (LPCTSTR)IDD_MULTIPLAY, hWnd, (DLGPROC)Multiplay);
#else
			OpenJoinScreen();
#endif
		break;
	case 3:  //disconnect

		Disconnect();
		GetLocalPlayer()->playstate=0;
		LoadTexture("textures\\startup.bmp",800);
		break;
	case 4: //quit
		DestroyWindow(hWnd);
		break;
	}
}
						