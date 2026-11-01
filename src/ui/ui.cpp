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

//credits panel, two columns of sections; a click anywhere closes it (MenuMouseDown)
#define CREDITSPX1 -.9f
#define CREDITSPY1 -.92f
#define CREDITSPX2 .9f
#define CREDITSPY2 .9f
#define CREDITSLINEH .065f
#define BTN_CREDITSCLOSE .58f,-.87f,.84f,-.76f

//a grey heading and its lines; moves y under them
static void CreditsSection(float x,float &y,float maxw,const char *heading,const char **lines,int count)
{
	glColor3f(.6f,.6f,.6f);
	UIText(x,y,heading,.045f);
	y-=CREDITSLINEH+.005f;
	glColor3f(1,1,1);
	for(int i=0;i<count;i++,y-=CREDITSLINEH)
		UITextFit(x,y,lines[i],maxw);
	y-=.05f;
}

static void DrawCredits()
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	UIPanel(CREDITSPX1,CREDITSPY1,CREDITSPX2,CREDITSPY2,.13f,"Edifice (HugeKilla) - Credits");
	const float leftx=-.82f,rightx=.04f,colw=.8f;
	float y=.64f;
	const char *authors[]={"Programming: Nelson Planting","Models/Graphics: John Thomas"};
	CreditsSection(leftx,y,colw,"Original authors",authors,2);
	const char *license[]={"GNU LGPL v3 (see license.txt)","sourceforge.net/p/hugekilla"};
	CreditsSection(leftx,y,colw,"License",license,2);
	const char *ports[]={"macOS/SDL2 port based on the","original source code"};
	CreditsSection(leftx,y,colw,"Ports",ports,2);
	const char *tools[]={"CMake, vcpkg, Go (lobby server)"};
	CreditsSection(leftx,y,colw,"Built with",tools,1);

	//libraries built into the game; build.py ships their license texts in licenses/
	y=.64f;
	const char *libraries[]=
	{
		"SDL2 - zlib license",
		"Opus - BSD 3-clause",
		"libcurl - curl license (MIT-style)",
		"GameNetworkingSockets (Valve) - BSD 3-clause",
		"Protocol Buffers - BSD 3-clause",
		"Abseil - Apache 2.0",
		"OpenSSL - Apache 2.0",
	};
	CreditsSection(rightx,y,colw,"Third-party libraries",libraries,7);
	glColor3f(.75f,.75f,.75f);
	UITextFit(rightx,y+.03f,"Full texts: licenses/ folder of the game",colw,.045f);
	UIRect(-.02f,-.05f,-.016f,.7f,1,1,1,.12f);   //column separator

	glColor3f(.45f,.45f,.45f);
	UIText(leftx,UITextY(-.87f,-.76f,.04f),"Click or press Esc to close",.04f);
	UIButton(BTN_CREDITSCLOSE,"Close",1);
	glColor3f(1,1,1);
}

extern Vector mousepos;
extern DWORD curtime;

//---- menu screen text and widgets ----

#define CELLASPECT (.04f/.07f)     //width/height of a glyph cell, as DrawText draws it
#define GLYPHGAP .1f               //space between two glyphs, in cells
static float glyphleft[96],glyphright[96];   //ink of each glyph, in cells from its left
static int fontmeasured;

//finds where the ink of each glyph of the font texture is, so text can be packed by
//glyph width; the glyphs aren't centered in their cells, so even a fixed advance
//looks uneven ("Loade d")
static void MeasureFont()
{
	for(int c=0;c<96;c++)
	{
		glyphleft[c]=.3f;
		glyphright[c]=.7f;
	}
	glyphleft[0]=.35f;   //space
	glyphright[0]=.6f;
	GLint w=0,h=0;
	glBindTexture(GL_TEXTURE_2D,300);
	glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
	glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
	if(w<16||h<16||w>4096||h>4096) return;   //not loaded yet: measured on a later call
	fontmeasured=1;
	unsigned char *px=new unsigned char[(size_t)w*h*4];
	glPixelStorei(GL_PACK_ALIGNMENT,1);
	glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
	int cw=w/16,ch=h/16;
	for(int c=1;c<96;c++)
	{
		int x0=(c%16)*cw,y0=(c/16)*ch;
		int left=cw,right=-1;
		for(int y=0;y<ch;y++)
			for(int x=0;x<cw;x++)
				if(px[(((size_t)(y0+y))*w+x0+x)*4+3]>48)
				{
					if(x<left) left=x;
					if(x>right) right=x;
				}
		//no ink, or ink everywhere (a font without alpha): keep the default
		if(right<0||right-left>cw*7/8) continue;
		glyphleft[c]=(float)left/cw;
		glyphright[c]=(float)(right+1)/cw;
	}
	delete []px;
}

static int GlyphIndex(char c)
{
	int i=(unsigned char)c-32;
	return i>=0&&i<96?i:'?'-32;
}

float UITextAdvance(const char *text,float h,int len)
{
	if(!fontmeasured) MeasureFont();
	if(len<0||len>(int)strlen(text)) len=strlen(text);
	float w=0;
	for(int i=0;i<len;i++)
	{
		int g=GlyphIndex(text[i]);
		w+=glyphright[g]-glyphleft[g]+GLYPHGAP;
	}
	return w*h*CELLASPECT;
}

float UITextWidth(const char *text,float h,int len)
{
	if(len<0||len>(int)strlen(text)) len=strlen(text);
	return len?UITextAdvance(text,h,len)-GLYPHGAP*h*CELLASPECT:0;
}

float UIText(float x,float y,const char *text,float h,int len)
{
	if(!fontmeasured) MeasureFont();
	if(len<0||len>(int)strlen(text)) len=strlen(text);
	const float cell=1.f/16;
	float cw=h*CELLASPECT;
	glBindTexture(GL_TEXTURE_2D,300);
	glBegin(GL_QUADS);
	for(int i=0;i<len;i++)
	{
		int g=GlyphIndex(text[i]);
		float x1=x-glyphleft[g]*cw;
		float u=(g%16)*cell,v=(g/16)*cell;
		if(g)
		{
			glTexCoord2f(u,v);
			glVertex2f(x1,y+h);
			glTexCoord2f(u,v+cell);
			glVertex2f(x1,y);
			glTexCoord2f(u+cell,v+cell);
			glVertex2f(x1+cw,y);
			glTexCoord2f(u+cell,v);
			glVertex2f(x1+cw,y+h);
		}
		x+=(glyphright[g]-glyphleft[g]+GLYPHGAP)*cw;
	}
	glEnd();
	return x-GLYPHGAP*cw;
}

float UITextFit(float x,float y,const char *text,float maxw,float h)
{
	int len=strlen(text);
	if(UITextWidth(text,h,len)<=maxw) return UIText(x,y,text,h,len);
	float dots=UITextWidth("...",h)+GLYPHGAP*h*CELLASPECT;
	while(len>0&&UITextWidth(text,h,len)+dots>maxw) len--;
	while(len>0&&text[len-1]==' ') len--;
	x=UIText(x,y,text,h,len);
	return UIText(len?x+GLYPHGAP*h*CELLASPECT:x,y,"...",h);
}

//lines separated by '\r' or '\n' (game messages, as DrawText takes them), going down
//by lineh from y; returns the y of the last line
float UITextLines(float x,float y,const char *text,float h,float lineh)
{
	for(;;)
	{
		int len=strcspn(text,"\r\n");
		UIText(x,y,text,h,len);
		if(!text[len]) return y;
		text+=len+1;
		y-=lineh;
	}
}

//the glyphs' capitals span about rows 8 to 27 of their 32
float UITextY(float y1,float y2,float h)
{
	return (y1+y2)/2-h*.45f;
}

int UIInside(float x1,float y1,float x2,float y2)
{
	return mousepos.x>x1&&mousepos.x<x2&&mousepos.y>y1&&mousepos.y<y2;
}

void UIRect(float x1,float y1,float x2,float y2,float r,float g,float b,float a)
{
	glDisable(GL_TEXTURE_2D);
	glColor4f(r,g,b,a);
	glBegin(GL_QUADS);
	glVertex2f(x1,y1);
	glVertex2f(x2,y1);
	glVertex2f(x2,y2);
	glVertex2f(x1,y2);
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

static void Outline(float x1,float y1,float x2,float y2,float t,float r,float g,float b,float a)
{
	UIRect(x1,y1,x2,y1+t,r,g,b,a);
	UIRect(x1,y2-t,x2,y2,r,g,b,a);
	UIRect(x1,y1+t,x1+t*.75f,y2-t,r,g,b,a);
	UIRect(x2-t*.75f,y1+t,x2,y2-t,r,g,b,a);
}

//panel of a menu screen: opaque enough that the main menu behind doesn't show through
void UIPanel(float x1,float y1,float x2,float y2,float titleh,const char *title)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	UIRect(x1,y1,x2,y2,.035f,.035f,.04f,1);
	UIRect(x1,y2-titleh,x2,y2,1,1,1,.06f);
	UIRect(x1,y2-titleh-.004f,x2,y2-titleh,1,1,.5f,.5f);
	Outline(x1,y1,x2,y2,.004f,1,1,1,.15f);
	glColor3f(1,1,.5f);
	UIText(x1+.05f,UITextY(y2-titleh,y2,.065f),title,.065f);
}

//"<" or ">" at an end of a value that steps through choices
void UIArrow(float x1,float y1,float x2,float y2,const char *label)
{
	int over=UIInside(x1,y1,x2,y2);
	UIRect(x1,y1,x2,y2,1,1,1,over?.25f:.1f);
	glColor3f(over?1:.8f,over?1:.8f,over?.5f:.8f);
	UIText((x1+x2-UITextWidth(label))/2,UITextY(y1,y2),label);
}

//framed button; returns 1 when the mouse is over it. Disabled: no frame, dim text
int UIButton(float x1,float y1,float x2,float y2,const char *label,int enabled,int strong)
{
	int over=enabled&&UIInside(x1,y1,x2,y2);
	if(!enabled)
		UIRect(x1,y1,x2,y2,1,1,1,.04f);
	else if(strong)
	{
		UIRect(x1,y1,x2,y2,.3f,.75f,.3f,over?.55f:.35f);
		Outline(x1,y1,x2,y2,.004f,.6f,1,.6f,over?.9f:.6f);
	}
	else
	{
		UIRect(x1,y1,x2,y2,1,1,1,over?.24f:.12f);
		Outline(x1,y1,x2,y2,.004f,1,1,1,over?.6f:.3f);
	}
	if(!enabled) glColor3f(.38f,.38f,.38f);
	else if(strong) glColor3f(.85f,1,.85f);
	else glColor3f(over?1:.9f,over?1:.9f,over?1:.9f);
	float w=UITextWidth(label);
	UIText((x1+x2-w)/2,UITextY(y1,y2),label);
	return over;
}

//text field: the end of the text when it doesn't fit, a caret when focused, a grey
//placeholder when empty
void UIField(float x1,float y1,float x2,float y2,const char *text,int focused,const char *placeholder)
{
	UIRect(x1,y1,x2,y2,0,0,0,.7f);
	if(focused) Outline(x1,y1,x2,y2,.004f,1,1,.5f,.75f);
	else Outline(x1,y1,x2,y2,.004f,1,1,1,UIInside(x1,y1,x2,y2)?.4f:.18f);
	float pad=.02f,room=x2-x1-2*pad-.015f;
	float y=UITextY(y1,y2);
	float x=x1+pad;
	if(!text[0]&&placeholder&&!focused)
	{
		glColor3f(.42f,.42f,.42f);
		UITextFit(x,y,placeholder,room);
		return;
	}
	const char *shown=text;
	while(*shown&&UITextWidth(shown)>room) shown++;
	glColor3f(1,1,focused?.75f:1);
	x=UIText(x,y,shown);
	if(focused&&!((curtime/500)&1))
		UIRect(x+.006f,y+UITEXTH*.12f,x+.011f,y+UITEXTH*.8f,1,1,.5f,.9f);
}


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
	//a full screen panel (join, server setup, options, credits) hides the menu, which
	//would show through
#ifndef HK_WIN32_NATIVE
	int covered=IsJoinScreenOpen()||IsServerSetupOpen()||IsOptionsOpen()||showcredits;
#else
	int covered=showcredits;
#endif
	for(int i=0;i<numbuttons&&!covered;i++)
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
		UIText(-.8f,.8f,"Press escape to resume game");
	{
		char version[64];
		sprintf(version,"v%s (protocol %d)",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
		glColor3f(.5,.5,.5);
		UIText(.97f-UITextWidth(version),.92f,version);
		glColor3f(1,1,1);
	}
	//framed on their click areas (OverCreditsButton, OverOptionsButton)
	if(!covered) UIButton(CREDITSX1,CREDITSY1,CREDITSX2,CREDITSY2,"Credits",1);
#ifndef HK_WIN32_NATIVE
	if(!covered) UIButton(OPTIONSX1,CREDITSY1,OPTIONSX2,CREDITSY2,"Options",1);
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
						