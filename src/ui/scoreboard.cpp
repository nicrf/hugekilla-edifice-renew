//scoreboard shown while the "showscores" key is held and at the end of a game:
//game mode and objective, team score, then one row per player (kills, deaths, K/D,
//ping, packet loss), sorted by team and kills.

#include "StdAfx.h"
#include "redrawing.h"
#include "playing.h"
#include "multiplay.h"
#include "ui.h"

extern ServerVars sv;
extern char curscore[1000];
extern char curobjective[1000];

#define BOARDX1 -.96f
#define BOARDX2 .96f
#define ROWH .075f
#define MAXROWS 16

static const float colx[6]={-.84f,-.14f,.1f,.4f,.58f,.76f};

//sort order: red team, blue team, no team; then most kills, then fewest deaths
static int TeamRank(int team)
{
	return team==1?0:team==2?1:2;
}

static int ComparePlayers(const void *a,const void *b)
{
	Player *pa=GetPlayer(*(const int *)a),*pb=GetPlayer(*(const int *)b);
	if(TeamRank(pa->team)!=TeamRank(pb->team)) return TeamRank(pa->team)-TeamRank(pb->team);
	if(pa->kills!=pb->kills) return pb->kills-pa->kills;
	return pa->deaths-pb->deaths;
}

static void TeamColor(int team,float brightness)
{
	if(team==1) glColor3f(1*brightness,.35f*brightness,.35f*brightness);
	else if(team==2) glColor3f(.45f*brightness,.6f*brightness,1*brightness);
	else glColor3f(brightness,brightness,brightness);
}

//first line of a multi-line game message ('\r' separated)
static void FirstLine(const char *src,char *dst,int size)
{
	int n=0;
	while(src[n]&&src[n]!='\r'&&src[n]!='\n'&&n<size-1)
	{
		dst[n]=src[n];
		n++;
	}
	dst[n]=0;
}

void DrawScoreboard()
{
	int numplayers=GetNumPlayers();
	int order[MAXPLAYERS];
	int count=0;
	for(int i=0;i<numplayers&&i<MAXPLAYERS;i++)
		order[count++]=i;
	qsort(order,count,sizeof(int),ComparePlayers);
	int rows=count<MAXROWS?count:MAXROWS;

	float top=.82f;
	float bottom=top-.32f-rows*ROWH-.12f;

	glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_TEXTURE_2D);
	glColor4f(0,0,0,.72f);
	glBegin(GL_QUADS);
	glVertex2f(BOARDX1,bottom);
	glVertex2f(BOARDX2,bottom);
	glVertex2f(BOARDX2,top);
	glVertex2f(BOARDX1,top);
	glEnd();
	//header separator
	glColor4f(1,1,1,.25f);
	glBegin(GL_QUADS);
	glVertex2f(BOARDX1+.04f,top-.255f);
	glVertex2f(BOARDX2-.04f,top-.255f);
	glVertex2f(BOARDX2-.04f,top-.25f);
	glVertex2f(BOARDX1+.04f,top-.25f);
	glEnd();
	glEnable(GL_TEXTURE_2D);

	char text[200];
	glColor3f(1,1,.5f);
	if(sv.gamemode==0)
		sprintf(text,"Capture the flag - first to %d",sv.capturelimit);
	else
		strcpy(text,"Sandbox");
	DrawText(BOARDX1+.08f,top-.1f,text);
	char line[200];
	FirstLine(curscore,line,sizeof(line));
	if(!line[0]) FirstLine(curobjective,line,sizeof(line));
	glColor3f(.85f,.85f,.85f);
	DrawText(BOARDX1+.08f,top-.18f,line);
	sprintf(text,"%d player%s",count,count==1?"":"s");
	DrawText(BOARDX2-.08f-strlen(text)*.04f,top-.1f,text);

	float y=top-.33f;
	glColor3f(.6f,.6f,.6f);
	const char *headers[6]={"Name","Kills","Deaths","K/D","Ping","Loss"};
	for(int c=0;c<6;c++)
		DrawText(colx[c],y,headers[c]);

	for(int r=0;r<rows;r++)
	{
		int i=order[r];
		Player *cur=GetPlayer(i);
		y-=ROWH;
		int local=cur==GetLocalPlayer();
		if(local)
		{
			glColor3f(1,1,1);
			DrawText(colx[0]-.06f,y,">");
		}
		TeamColor(cur->team,local?1:.85f);
		strncpy(line,cur->name,16);
		line[16]=0;
		DrawText(colx[0],y,line);
		if(cur->playstate>=10)
		{
			sprintf(text,"%d",cur->kills);
			DrawText(colx[1],y,text);
			sprintf(text,"%d",cur->deaths);
			DrawText(colx[2],y,text);
			if(cur->deaths) sprintf(text,"%.1f",(float)cur->kills/cur->deaths);
			else sprintf(text,"%d",cur->kills);
			DrawText(colx[3],y,text);
		}
		else
			DrawText(colx[1],y,i==0?"Server":"Connecting");
		sprintf(text,"%d",(int)cur->ping);
		DrawText(colx[4],y,text);
		sprintf(text,"%d%%",(int)((cur->packetloss*100)/30000));
		DrawText(colx[5],y,text);
	}
	if(count>rows)
	{
		glColor3f(.6f,.6f,.6f);
		sprintf(text,"+%d more",count-rows);
		DrawText(colx[0],y-ROWH,text);
	}
	glPopAttrib();
	glColor3f(1,1,1);
}
