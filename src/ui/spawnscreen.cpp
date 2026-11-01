//screen shown after creating or joining a capture the flag game, until the player
//first spawns (which used to take a shot at a blank screen): the players of each
//team, whether the game has started, how to play with the current key bindings, and
//a Play button. The player can't move or look around meanwhile. The mouse stays captured (freed, the game shows its pause menu):
//a click anywhere fires, which spawns, as the button says; Enter or Space too.

#ifndef HK_WIN32_NATIVE

#include "StdAfx.h"
#include "redrawing.h"
#include "playing.h"
#include "gameplay.h"
#include "multiplay.h"
#include "ui.h"

extern ServerVars sv;
extern Vector mousepos;
extern RemoteGameData rgd;
extern Controls controls;
extern int numplayers;
extern int consolevisible;
extern char curscore[1000];
extern NetSettings netsettings;
void LockCursor(int lock);

#define PANELX1 -.9f
#define PANELX2 .9f
#define PANELY1 -.8f
#define PANELY2 .78f
#define TITLEH .12f
#define ROWH .062f
#define MIDX -.12f            //left: teams, right: how to play
#define PLAYX1 .42f
#define PLAYY1 (PANELY1+.05f)
#define PLAYY2 (PANELY1+.17f)

int IsSpawnScreenOpen()
{
	Player *me=GetLocalPlayer();
	//respawntime is 1 on entering the game (GPlayerEnterGame), a time after a death
	return sv.gamemode==0&&me->playstate==11&&me->respawntime<=1&&!consolevisible&&
		!IsBuildMenuOpen()&&!IsOptionsOpen();
}

static void Play()
{
	LockCursor(1);
	HandleKey(controls.fire,0);   //spawns like a shot did (Play() in playing.cpp)
}

int SpawnScreenHandleKey(int vk)
{
	if(!IsSpawnScreenOpen()) return 0;
	if(vk==VK_RETURN||vk==VK_SPACE)
	{
		Play();
		return 1;
	}
	return 0;
}

//keys the game ignores while the screen is up: only playing (fire, Enter, Space),
//talking, the scores, the console and the function keys get through
int SpawnScreenBlocksKey(int vk)
{
	if(!IsSpawnScreenOpen()||GetLocalPlayer()->talking) return 0;
	if(vk==controls.fire||vk==controls.talk||vk==controls.talkteam||vk==controls.pushtotalk||
		vk==controls.showscores||vk==192||(vk>=VK_F1&&vk<=VK_F12))
		return 0;
	return 1;
}

//a key binding as shown to players
static const char *KeyLabel(int vk)
{
	static char buf[8][16];
	static int n;
	char *s=buf[n++&7];
	switch(vk)
	{
	case VK_LBUTTON: return "Left click";
	case VK_RBUTTON: return "Right click";
	case VK_SPACE: return "Space";
	case VK_TAB: return "Tab";
	case VK_SHIFT: return "Shift";
	case VK_CONTROL: return "Ctrl";
	case VK_MENU: return "Alt";
	}
	if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9')) snprintf(s,16,"%c",vk);
	else snprintf(s,16,"key %d",vk);
	return s;
}

static void TeamColumn(float x,float y,int team,const char *name)
{
	int count=0;
	for(int i=0;i<numplayers;i++)
		if(GetPlayer(i)->playstate>=10&&GetPlayer(i)->team==team) count++;
	char line[80];
	snprintf(line,sizeof(line),"%s  %d",name,count);
	if(team==1) glColor3f(1,.4f,.4f);
	else glColor3f(.5f,.65f,1);
	UIText(x,y,line);
	y-=ROWH;
	int shown=0;
	for(int i=0;i<numplayers&&shown<8;i++)
	{
		Player *p=GetPlayer(i);
		if(p->playstate<10||p->team!=team) continue;
		glColor3f(p==GetLocalPlayer()?1:.82f,p==GetLocalPlayer()?1:.82f,p==GetLocalPlayer()?.6f:.82f);
		UITextFit(x+.03f,y,p->name,MIDX-x-.08f,.045f);
		y-=ROWH*.85f;
		shown++;
	}
}

void DrawSpawnScreen()
{
	if(!IsSpawnScreenOpen()) return;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	char title[160];
	snprintf(title,sizeof(title),"Capture the flag - %s",netsettings.servername);
	UIPanel(PANELX1,PANELY1,PANELX2,PANELY2,TITLEH,title);
	glEnable(GL_TEXTURE_2D);

	//teams and game state
	float x=PANELX1+.07f,y=PANELY2-TITLEH-.1f;
	glColor3f(.6f,.6f,.6f);
	UIText(x,y,"Players");
	TeamColumn(x,y-ROWH*1.3f,1,"Red");
	TeamColumn(x+(MIDX-x)/2,y-ROWH*1.3f,2,"Blue");
	const char *state;
	if(rgd.gamestate==1) state=curscore[0]?curscore:"The game is on";
	else if(rgd.gamestate==2) state="Game over";
	else state="Starts with a player in each team";
	glColor3f(1,1,.6f);
	UITextFit(x,PLAYY2+.12f,state,MIDX-x-.05f);

	//how to play
	UIRect(MIDX,PLAYY2+.06f,MIDX+.004f,PANELY2-TITLEH-.05f,1,1,1,.12f);
	x=MIDX+.07f;
	glColor3f(.6f,.6f,.6f);
	UIText(x,y,"How to play");
	char lines[9][120];
	int n=0;
	snprintf(lines[n++],120,"Each team first gets a station: its carrier");
	snprintf(lines[n++],120,"picks a good spot and drops it (%s).",KeyLabel(controls.claim));
	snprintf(lines[n++],120,"Build forts around it (%s): the circle on",KeyLabel(controls.build));
	snprintf(lines[n++],120,"the minimap. Building uses credits.");
	snprintf(lines[n++],120,"Steal the enemy flag at its station (%s),",KeyLabel(controls.claim));
	snprintf(lines[n++],120,"drop it next to yours (%s) to score.",KeyLabel(controls.claim));
	if(sv.capturelimit>0) snprintf(lines[n++],120,"First team to %d captures wins.",sv.capturelimit);
	snprintf(lines[n++],120,"Move %s%s%s%s, jump %s, jetpack %s, scores %s",
		KeyLabel(controls.forward),KeyLabel(controls.lstrafe),KeyLabel(controls.back),
		KeyLabel(controls.rstrafe),KeyLabel(controls.jump),KeyLabel(controls.jetpack),
		KeyLabel(controls.showscores));
	glColor3f(.9f,.9f,.9f);
	float ly=y-ROWH*1.3f;
	for(int i=0;i<n;i++)
	{
		if(i==n-1)
		{
			ly-=ROWH*.4f;
			glColor3f(.62f,.62f,.62f);
		}
		UITextFit(x,ly,lines[i],PANELX2-x-.05f,.045f);
		ly-=ROWH*.9f;
	}

	glColor3f(.55f,.55f,.55f);
	UIText(PANELX1+.07f,UITextY(PLAYY1,PLAYY2,.042f),"Click anywhere, Enter or Space: play",.042f);
	Vector mouse=mousepos;
	mousepos=Vector(PLAYX1+.01f,PLAYY1+.01f,0);   //drawn as hovered: the default action
	UIButton(PLAYX1,PLAYY1,PANELX2-.05f,PLAYY2,"Play",1,1);
	mousepos=mouse;
}

#endif
