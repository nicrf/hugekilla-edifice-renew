//used to draw menus (only the main menu now), see if they are clicked, and take the appropriate action


#include "stdafx.h"
#include "multiplay.h"
#include "ui.h"
#include "vector.h"
#include "redrawing.h"

Button button[50];
int numbuttons;
int uicurmenu;

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

	SwapBuffers(hdc);	
}


void MenuMouseDown()
{
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
		StartServer();
		break;
	case 2:  //play
		if(GetLocalPlayer()->playstate==0)
			DialogBox(hInst, (LPCTSTR)IDD_MULTIPLAY, hWnd, (DLGPROC)Multiplay);
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
						