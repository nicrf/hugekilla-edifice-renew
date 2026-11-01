//scoreboard shown while the "showscores" key is held and at the end of a game:
//game mode and objective, team score, then one row per player (kills, deaths, K/D,
//ping, packet loss), sorted by team and kills. A host listed on the lobby also sees
//its game code.

#include "renderer.h"
#include "redrawing.h"
#include "playing.h"
#include "multiplay.h"
#include "ui.h"
#include "lobbygame.h"

#define BOARDX1 -.96f
#define BOARDX2 .96f
#define ROWH .065f
#define MAXROWS 16
#define TITLEH .065f            //game mode line

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
	if(team==1) RColor3f(1*brightness,.35f*brightness,.35f*brightness);
	else if(team==2) RColor3f(.45f*brightness,.6f*brightness,1*brightness);
	else RColor3f(brightness,brightness,brightness);
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
		if(PlayerSlotUsed(i)) order[count++]=i;   //not the free slots (M11 phase 3b)
	qsort(order,count,sizeof(int),ComparePlayers);
	int rows=count<MAXROWS?count:MAXROWS;

	float top=.82f;
	float bottom=top-.26f-rows*ROWH-.08f;

	RPushAttrib(RATTR_ENABLE|RATTR_CURRENT|RATTR_COLOR);
	REnable(RCAP_BLEND);
	RBlendFunc(RBLEND_ALPHA);
	REnable(RCAP_TEXTURE);
	//translucent: the game goes on behind
	UIRect(BOARDX1,bottom,BOARDX2,top,.035f,.035f,.04f,.78f);
	UIRect(BOARDX1,top-.2f,BOARDX2,top,1,1,1,.05f);
	UIRect(BOARDX1,top-.204f,BOARDX2,top-.2f,1,1,.5f,.5f);

	char text[200];
	RColor3f(1,1,.5f);
	if(sv.gamemode==0)
		sprintf(text,"Capture the flag - first to %d",sv.capturelimit);
	else
		strcpy(text,"Sandbox");
	float textx=BOARDX1+.08f,right=BOARDX2-.08f;
	UIText(textx,top-.09f,text,TITLEH);
	char line[200];
	FirstLine(curscore,line,sizeof(line));
	if(!line[0]) FirstLine(curobjective,line,sizeof(line));
	RColor3f(.85f,.85f,.85f);
	UITextFit(textx,top-.165f,line,1.1f);
	sprintf(text,"%d player%s",count,count==1?"":"s");
	UIText(right-UITextWidth(text),top-.09f,text);
	if(LobbyHostCode()[0])   //hosting on the Internet: the code to give to friends
	{
		sprintf(text,"Game code %s",LobbyHostCode());
		RColor3f(1,1,.5f);
		UIText(right-UITextWidth(text),top-.165f,text);
	}

	float y=top-.28f;
	RColor3f(.6f,.6f,.6f);
	const char *headers[6]={"Name","Kills","Deaths","K/D","Ping","Loss"};
	for(int c=0;c<6;c++)
		UIText(colx[c],y,headers[c],.045f);

	for(int r=0;r<rows;r++)
	{
		int i=order[r];
		Player *cur=GetPlayer(i);
		y-=ROWH;
		int local=cur==GetLocalPlayer();
		if(local)
		{
			UIRect(BOARDX1+.04f,y-.012f,BOARDX2-.04f,y+ROWH-.012f,1,1,1,.07f);
			RColor3f(1,1,1);
			UIText(colx[0]-.05f,y,">");
		}
		TeamColor(cur->team,local?1:.85f);
		UITextFit(colx[0],y,cur->name,colx[1]-colx[0]-.04f);
		if(cur->playstate>=10)
		{
			sprintf(text,"%d",cur->kills);
			UIText(colx[1],y,text);
			sprintf(text,"%d",cur->deaths);
			UIText(colx[2],y,text);
			if(cur->deaths) sprintf(text,"%.1f",(float)cur->kills/cur->deaths);
			else sprintf(text,"%d",cur->kills);
			UIText(colx[3],y,text);
		}
		else
			UIText(colx[1],y,i==0?"Server":"Connecting");
		sprintf(text,"%d",(int)cur->ping);
		UIText(colx[4],y,text);
		sprintf(text,"%d%%",(int)((cur->packetloss*100)/30000));
		UIText(colx[5],y,text);
	}
	if(count>rows)
	{
		RColor3f(.6f,.6f,.6f);
		sprintf(text,"+%d more",count-rows);
		UIText(colx[0],y-ROWH,text);
	}
	RPopAttrib();
	RColor3f(1,1,1);
}
