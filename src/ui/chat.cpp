//chat box: recent messages at the bottom left (sender colored by team, fading out after a
//while), and an input bar while typing (talk / talkteam keys; Tab switches all/team).
//Typed text comes from SDL text input, so it follows the keyboard layout.

#include "gl.h"
#include "redrawing.h"
#include "playing.h"
#include "multiplay.h"
#include "ui.h"
#include "voice.h"
#include <ctype.h>

#define CHATHISTORY 32
#define CHATMAXLEN 120          //characters typed in one message
#define CHATH .045f             //chat text is smaller than the rest of the HUD
#define LINEH .055f
#define CHATX -.97f
#define CHATWIDTH .92f          //from CHATX (keeps the crosshair clear)
#define INPUTY -.27f            //above the minimap
#define VISIBLETIME 10000       //ms a message stays on screen when not typing
#define FADETIME 2000

struct ChatLine
{
	char name[32];
	int team;
	int teamonly;
	char text[256];
	DWORD time;
};

static ChatLine history[CHATHISTORY];
static int numlines;            //total added; history is a ring
static int ignorenexttext;      //the key that opened the chat also sends its character

void ChatAddLine(const char *name,int team,int teamonly,const char *text)
{
	ChatLine &line=history[numlines%CHATHISTORY];
	strncpy(line.name,name,31);
	line.name[31]=0;
	line.team=team;
	line.teamonly=teamonly;
	strncpy(line.text,text,255);
	line.text[255]=0;
	line.time=curtime;
	numlines++;
}

void ChatOpened(int key)
{
	ignorenexttext=key;
}

//appends typed text (UTF-8) to the message; accented Latin letters become their
//base letter since the game font is ASCII
void ChatHandleText(const char *utf8)
{
	if(!GetLocalPlayer()->talking) return;
	static const char latin1[]="AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
	const unsigned char *p=(const unsigned char *)utf8;
	int len=strlen(talktext);
	while(*p)
	{
		char c=0;
		if(*p<128)
		{
			c=*p;
			p++;
		}
		else if((*p&0xE0)==0xC0&&p[1])
		{
			int code=((p[0]&0x1F)<<6)|(p[1]&0x3F);
			if(code>=0xC0&&code<=0xFF) c=latin1[code-0xC0];
			p+=2;
		}
		else
		{
			p++;
			while((*p&0xC0)==0x80) p++;   //skip the rest of a longer sequence
		}
		if(c<32||c>126) continue;
		if(ignorenexttext)
		{
			int opener=ignorenexttext;
			ignorenexttext=0;
			if(tolower(c)==tolower(opener)) continue;
		}
		if(len>=CHATMAXLEN) break;
		talktext[len++]=c;
		talktext[len]=0;
	}
	ignorenexttext=0;
}

//draws text at the chat size; returns the x where more text goes
static float ChatText(float x,float y,const char *text)
{
	UIText(x,y,text,CHATH);
	return x+UITextAdvance(text,CHATH);
}

static void TeamColor(int team,float alpha)
{
	if(team==1) glColor4f(1,.4f,.4f,alpha);
	else if(team==2) glColor4f(.5f,.65f,1,alpha);
	else glColor4f(.85f,.85f,.85f,alpha);
}

//splits text into lines that fit in width, at spaces when possible; returns the
//number of lines written to lines[]
static int Wrap(const char *text,float width,char lines[][CHATMAXLEN+80],int maxlines)
{
	int n=0;
	int len=strlen(text);
	int pos=0;
	while(pos<len&&n<maxlines)
	{
		int take=len-pos;
		if(take>CHATMAXLEN+79) take=CHATMAXLEN+79;
		if(UITextWidth(&text[pos],CHATH,take)>width)
		{
			//width of fit+1 characters: advance of fit, plus the ink of the next
			int fit=1;
			float advance=UITextAdvance(&text[pos],CHATH,1);
			while(fit<take&&advance+UITextWidth(&text[pos+fit],CHATH,1)<=width)
				advance+=UITextAdvance(&text[pos+fit++],CHATH,1);
			take=fit;
			for(int i=fit;i>fit/2;i--)
				if(text[pos+i]==' ')
				{
					take=i;
					break;
				}
		}
		memcpy(lines[n],&text[pos],take);
		lines[n][take]=0;
		n++;
		pos+=take;
		while(text[pos]==' ') pos++;
	}
	return n;
}

static void Box(float x1,float y1,float x2,float y2,float alpha)
{
	glDisable(GL_TEXTURE_2D);
	glColor4f(0,0,0,alpha);
	glBegin(GL_QUADS);
	glVertex2f(x1,y1);
	glVertex2f(x2,y1);
	glVertex2f(x2,y2);
	glVertex2f(x1,y2);
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

void DrawChat()
{
	int typing=GetLocalPlayer()->talking!=0;
	int maxshown=typing?9:6;

	glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);

	//wrapped lines of the recent messages, newest at the bottom
	struct Shown
	{
		char text[CHATMAXLEN+80];
		int prefixlen;      //colored part ("[Team] name:") on the first line only
		int team;
		float alpha;
	};
	static Shown shown[CHATHISTORY*3];
	int count=0;
	int first=numlines>CHATHISTORY?numlines-CHATHISTORY:0;
	for(int i=numlines-1;i>=first&&count<maxshown;i--)
	{
		const ChatLine &line=history[i%CHATHISTORY];
		DWORD age=curtime-line.time;
		float alpha=1;
		if(!typing)
		{
			if(age>VISIBLETIME) break;
			if(age>VISIBLETIME-FADETIME) alpha=(float)(VISIBLETIME-age)/FADETIME;
		}
		char full[CHATMAXLEN+400];
		snprintf(full,sizeof(full),"%s%s: %s",line.teamonly?"[Team] ":"",line.name,line.text);
		int prefixlen=strlen(line.name)+2+(line.teamonly?7:0);
		char lines[8][CHATMAXLEN+80];
		int n=Wrap(full,CHATWIDTH,lines,8);
		//this message's lines go above the ones already collected
		for(int l=n-1;l>=0&&count<maxshown;l--)
		{
			Shown &s=shown[count++];
			strcpy(s.text,lines[l]);
			s.prefixlen=l==0?prefixlen:0;
			s.team=line.teamonly?-1:line.team;
			if(l==0) s.team=line.team;
			s.alpha=alpha;
		}
	}

	float bottom=INPUTY+LINEH+.02f;
	if(typing)
	{
		float top=bottom+maxshown*LINEH+.02f;
		Box(CHATX-.02f,INPUTY-.025f,CHATX+CHATWIDTH+.02f,top,.45f);
		Box(CHATX-.02f,INPUTY-.025f,CHATX+CHATWIDTH+.02f,INPUTY+LINEH-.005f,.35f);
	}
	for(int k=0;k<count;k++)
	{
		const Shown &s=shown[k];
		float y=bottom+k*LINEH;
		if(s.prefixlen)
		{
			char prefix[CHATMAXLEN+80];
			int plen=s.prefixlen<(int)strlen(s.text)?s.prefixlen:strlen(s.text);
			memcpy(prefix,s.text,plen);
			prefix[plen]=0;
			TeamColor(s.team,s.alpha);
			float x=ChatText(CHATX,y,prefix);
			glColor4f(1,1,1,s.alpha);
			ChatText(x,y,&s.text[plen]);
		}
		else
		{
			glColor4f(1,1,1,s.alpha);
			ChatText(CHATX,y,s.text);
		}
	}

	if(typing)
	{
		int team=GetLocalPlayer()->talking==2;
		const char *label=team?"Team: ":"All: ";
		if(team) glColor4f(.6f,1,.6f,1);
		else glColor4f(1,1,.5f,1);
		float x=ChatText(CHATX,INPUTY,label);
		//keep the end of a long message visible
		float room=CHATX+CHATWIDTH-x-.02f;
		int len=strlen(talktext);
		const char *visible=talktext;
		if(UITextWidth(visible,CHATH)>room)
		{
			x=ChatText(x,INPUTY,"..");
			room-=UITextAdvance("..",CHATH);
			while(*visible&&UITextWidth(visible,CHATH)>room) visible++;
		}
		glColor4f(1,1,1,1);
		x=UIText(x,INPUTY,visible,CHATH);
		if(!((curtime/500)&1))   //caret, as in the menus' text fields
			UIRect(x+.005f,INPUTY+CHATH*.12f,x+.009f,INPUTY+CHATH*.8f,1,1,.5f,.9f);
		char hint[80];
		snprintf(hint,sizeof(hint),"%d/%d  Tab: %s  Enter: send  Esc: cancel",len,CHATMAXLEN,team?"all":"team");
		glColor4f(.6f,.6f,.6f,1);
		ChatText(CHATX,INPUTY-.07f,hint);
	}
	glPopAttrib();
	glColor3f(1,1,1);
}

//voice chat (src/audio/voice.cpp): "Mic on" while transmitting, and who is heard
#define VOICEHUDX .4f
#define VOICEHUDY .62f

void DrawVoiceHUD()
{
	int transmitting=VoiceTransmitting();
	int speakers=0;
	for(int i=0;i<GetNumPlayers()&&i<MAXPLAYERS;i++)
		if(VoiceIsSpeaking(i)&&i!=GetLocalNum()) speakers++;
	if(!transmitting&&!speakers) return;
	glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	float y=VOICEHUDY;
	if(transmitting)
	{
		glColor4f(.4f,1,.4f,1);
		ChatText(VOICEHUDX,y,"Mic on");
		y-=LINEH;
	}
	for(int i=0;i<GetNumPlayers()&&i<MAXPLAYERS;i++)
	{
		if(!VoiceIsSpeaking(i)||i==GetLocalNum()) continue;
		glColor4f(1,1,1,1);
		float x=ChatText(VOICEHUDX,y,"Speaking: ");
		TeamColor(GetPlayer(i)->team,1);
		ChatText(x,y,GetPlayer(i)->name);
		y-=LINEH;
	}
	glPopAttrib();
}
