

#include <StdAfx.h>
#include "redrawing.h"
#include "stdio.h"
#include "multiplay.h"
extern GraphicsPrefs graphicsprefs;
extern int server;
extern ServerVars sv;
extern int numplayers;
extern GameData gd;

char consoletext[100][500];
char consolecommand[500];
char prevconsolecommand[40][500];
int curprevconsolecommand;
int consolevisible;
DWORD consolescrolltime;

extern DWORD curtime;
extern ServerObject sobject[MAXSERVEROBJS];

char consolevar[100][40];
void *consolevarptr[100];
int consolevartype[100];
char consolevarserver[100];
int numconsolevar;
int consolepartvisible;


void ScrollConsoleText()
{
	for(int i=99;i>0;i--)
	{
		strcpy(consoletext[i],consoletext[i-1]);
	}
	consoletext[0][0]=0;
}

void ConsoleDisplayText(char *text)
{
	int len=strlen(text);
	char *curtext=text;
	for(int i=0;i<len;i++)
	{
		if(text[i]=='\r')
		{
			if(consolepartvisible<6)consolepartvisible++;
			consolescrolltime=curtime+3000;
	
			text[i]=0;
			ScrollConsoleText();
			strcpy(consoletext[0],curtext);
			curtext=&text[i+1];
		}
	}
	if(consolepartvisible<6)consolepartvisible++;
	consolescrolltime=curtime+3000;
	
	ScrollConsoleText();
	strcpy(consoletext[0],curtext);
			
	if(consolevisible) DrawConsole();
#ifdef HKDS
	DrawConsoleNonGL();
#endif

}

void ConsoleCommand()
{
	if(strlen(consolecommand)==0) return;
	ConsoleDisplayText(consolecommand);
	int i;
	for(i=38;i>=0;i--)
	{
		strcpy(prevconsolecommand[i+1],prevconsolecommand[i]);
	}
	strcpy(prevconsolecommand[0],consolecommand);
	
	if(strcmp(consolecommand,"moveunnecessarytextures")==0)
	{
		MoveUnnecessaryTextures();
	}
	if(strncmp(consolecommand,"team",4)==0)
	{
		 ChangeTeam(GetLocalNum(),atoi(&consolecommand[4]));
	}
	if(strcmp(consolecommand,"createdome")==0)
	{
		 CreateInstallation(GetLocalPlayer()->pos,0);
	}
	if(strcmp(consolecommand,"resetgame")==0&&server)
	{
		GReset();
	}
	if(strcmp(consolecommand,"die")==0)
	{
		GetLocalPlayer()->life=-1;
		Killed(GetLocalNum());
	}
	if(strcmp(consolecommand,"scorereset")==0)
	{
		if(server) ScoreReset();
	}
	if(strcmp(consolecommand,"givemoney")==0)
	{
		GetLocalPlayer()->money+=1000;
	}

	if(strncmp(consolecommand,"kick",4)==0&&IsServer())
	{
		int num=atoi(&consolecommand[4]);
		if(num>0&&num<numplayers)
		{
			DeletePlayer(num,2);
		}
	}
	if(strncmp(consolecommand,"credit",6)==0&&IsServer())
	{
		int num=atoi(&consolecommand[6]);
		GiveCredit(-1,num,1);
	}
	
	if(strcmp(consolecommand,"createplayer")==0&&server)  //dummy player
	{
		Player *cur=GetPlayer(numplayers);
		cur->playstate=10;
		cur->timeout=curtime+10000000;
		cur->pos=GetLocalPlayer()->pos;
		strcpy(cur->name,"Player");
		cur->hanggliding=1;
		cur->hangglide=1;
		cur->jetpack=1;
		cur->tilt=GetLocalPlayer()->tilt;
		numplayers++;
	}
	if(strncmp(consolecommand,"say",3)==0)
	{
		if(strlen(consolecommand)>4)
		{
			SendChatMessage(&consolecommand[4],0);
		}
	}
		
	char var[40];
	char *val=0;
	int len=strlen(consolecommand);
	for(i=0;i<len;i++)
	{
		if(consolecommand[i]==' '||consolecommand[i]==0) break;
		var[i]=consolecommand[i];
	}
	var[i]=0;
	if(consolecommand[i]!=0) val=&consolecommand[i+1];
	for(i=0;i<numconsolevar;i++)
	{
		if(strcmp(var,consolevar[i])==0)
		{
			if(val)
			{
				if(consolevarserver[i]&&!server)
				{
					ConsoleDisplayText("Only server can change value");
				}
				else
				{
					switch (consolevartype[i])
					{
					case 1:
						*(float *)consolevarptr[i]=atof(val);
						break;
					case 2:
						*(int *)consolevarptr[i]=atoi(val);
						break;
					case 3:
						strcpy((char *)consolevarptr[i],val);
						break;
					}
					ScrollConsoleText();
					strcat(consoletext[0],var);
					strcat(consoletext[0]," changed to ");
					strcat(consoletext[0],val);
					SendServerVars(0);
				}
			}
			else
			{
				ScrollConsoleText();
				strcat(consoletext[0],var);
				strcat(consoletext[0]," is ");
				switch (consolevartype[i])
				{
				case 1:
					{
						sprintf(&consoletext[0][strlen(consoletext[0])],"%f",*(float *)consolevarptr[i]);
					}
					break;
				case 2:
					sprintf(&consoletext[0][strlen(consoletext[0])],"%i",*(int *)consolevarptr[i]);
					break;
				case 3:
					strcpy(&consoletext[0][strlen(consoletext[0])],(char *)consolevarptr[i]);
					break;
				}
				
			}
		}
	}
	consolecommand[0]=0;
}
char GetRealKey(DWORD key,DWORD flags)
{
	if((key>='0'&&key<='z')||key==' '||key>=180||key==191)
	{
		if(key==190)key='.';
		if(key==188)key=',';
		if(key==191)key='?';
		if(key==222)key='\'';
		key&=~128;
		if(key=='=')key='-';
		if(key==';')key='=';
		if(key=='?')key='/';
		if(key==':')key=';';
		if((!(GetKeyState(VK_SHIFT)&1<<8))&&(key>='A'&&key<='Z')) key+=32;
		if(GetKeyState(VK_SHIFT)&1<<8) 
		{
			switch (key)
			{
			case '1': key='!'; break;
			case '2': key='@'; break;
			case '3': key='#'; break;
			case '4': key='$'; break;
			case '5': key='%'; break;
			case '6': key='^'; break;
			case '7': key='&'; break;
			case '8': key='*'; break;
			case '9': key='('; break;
			case '0': key=')'; break;
			case '-': key='_'; break;
			case '=': key='+'; break;
			case '[': key='{'; break;
			case ']': key='}'; break;
			case ';': key=':'; break;
			case '\'': key='"'; break;
			case ',': key='<'; break;
			case '.': key='>'; break;
			case '/': key='?'; break;
			case '\\': key='|'; break;
			}
		}
		return key;
	}
	return 0;
	
}

int HandleConsoleKey(DWORD key,DWORD flags)
{
	if(key==VK_TAB)
	{
		if(strlen(consolecommand)==0) return 1;
		for(int i=0;i<numconsolevar;i++)
		{
			if(!strncmp(consolecommand,consolevar[i],strlen(consolecommand)))
			{
				strcpy(consolecommand,consolevar[i]);
				strcat(consolecommand," ");
				break;
			}
		}
	}
	if(key==VK_UP&&curprevconsolecommand<40)
	{
		curprevconsolecommand++;
		strcpy(consolecommand,prevconsolecommand[curprevconsolecommand]);		
	}
	if(key==VK_DOWN)
	if(curprevconsolecommand>=0)	
	{
		curprevconsolecommand--;
		if(curprevconsolecommand>=0)
			strcpy(consolecommand,prevconsolecommand[curprevconsolecommand]);		
		else
			consolecommand[0]=0;

	}
		
	if(key==VK_BACK)
	{
		int len=strlen(consolecommand);
		if(len>0)
		{
			consolecommand[len-1]=0;
		}
	}
	if(key==VK_RETURN)
	{
		curprevconsolecommand=-1;
		ConsoleCommand();
	}
	char realkey=GetRealKey(key,flags);
	if(realkey)
	{
		int len=strlen(consolecommand);
		consolecommand[len]=realkey;
		consolecommand[len+1]=0;
	}
#ifdef HKDS
	DrawConsoleNonGL();
#endif
	return 1;
	
}

void AddConsoleVar(void *var,char *name,int type,char servervar)
{
	int i;
	for(i=0;i<numconsolevar;i++)
	{
		if(strcmp(consolevar[i],name)==0)
		{
			break;
		}
	}
	if(i!=numconsolevar) return;
	strcpy(consolevar[numconsolevar],name);
	consolevarptr[numconsolevar]=var;
	consolevartype[numconsolevar]=type;
	consolevarserver[numconsolevar]=servervar;
	numconsolevar++;
}

void AddConsoleVar(float *var,char *name,char servervar)
{
	AddConsoleVar(var,name,1,servervar);
}

void AddConsoleVar(int *var,char *name,char servervar)
{
	AddConsoleVar(var,name,2,servervar);
}

void AddConsoleVar(char *var,char *name,char servervar)
{
	AddConsoleVar(var,name,3,servervar);
}

void InitConsole()
{
	/*physics.walkaccelerate=.2;
	physics.strafeaccelerate=.15;
	physics.resistance=1.3;
	physics.stopresistance=1.6;*/
	graphicsprefs.drawdistance=57;
	graphicsprefs.mipdist[0]=0;
	graphicsprefs.mipdist[1]=4;
	graphicsprefs.mipdist[2]=8;
	graphicsprefs.mipdist[3]=8;
	graphicsprefs.mipdist[4]=4;
	
	AddConsoleVar(&sv.walkaccelerate,"walkaccelerate",1);
	AddConsoleVar(&sv.strafeaccelerate,"strafeaccelerate",1);
	AddConsoleVar(&sv.resistance,"resistance",1);
	AddConsoleVar(&sv.stopresistance,"stopresistance",1);
	AddConsoleVar(&sv.gatspeed,"gatspeed",1);
	AddConsoleVar(&sv.gattimeout,"gattimeout",1);
	AddConsoleVar(&sv.gatammohold,"gatammohold",1);
	AddConsoleVar(&sv.gatreloadtime,"gatreloadtime",1);
	AddConsoleVar(&sv.gravity,"gravity",1);
	AddConsoleVar(&sv.grenadedamage,"grenadedamage",1);
	AddConsoleVar(&sv.grenadeexplodetime,"grenadeexplodetime",1);
	AddConsoleVar(&sv.grenadespeed,"grenadespeed",1);
	AddConsoleVar(&sv.grenadetimeout,"grenadetimeout",1);
	AddConsoleVar(&sv.grenadeammohold,"grenadeammohold",1);
	AddConsoleVar(&sv.grenadereloadtime,"grenadereloadtime",1);
	AddConsoleVar(&sv.highbulletdamage,"highbulletdamage",1);
	AddConsoleVar(&sv.lowbulletdamage,"lowbulletdamage",1);
	AddConsoleVar(&sv.m16speed,"m16speed",1);
	AddConsoleVar(&sv.m16timeout,"m16timeout",1);
	AddConsoleVar(&sv.m16ammohold,"m16ammohold",1);
	AddConsoleVar(&sv.m16reloadtime,"m16reloadtime",1);
	AddConsoleVar(&sv.steyrspeed,"steyrspeed",1);
	AddConsoleVar(&sv.steyrtimeout,"steyrtimeout",1);
	AddConsoleVar(&sv.steyrammohold,"steyrammohold",1);
	AddConsoleVar(&sv.steyrreloadtime,"steyrreloadtime",1);
	AddConsoleVar(&sv.umpspeed,"umpspeed",1);
	AddConsoleVar(&sv.umptimeout,"umptimeout",1);
	AddConsoleVar(&sv.umpammohold,"umpammohold",1);
	AddConsoleVar(&sv.umpreloadtime,"umpreloadtime",1);
	AddConsoleVar(&sv.berettaspeed,"berettaspeed",1);
	AddConsoleVar(&sv.berettatimeout,"berettatimeout",1);
	AddConsoleVar(&sv.berettaammohold,"berettaammohold",1);
	AddConsoleVar(&sv.berettareloadtime,"berettareloadtime",1);
	AddConsoleVar(&sv.sr1000speed,"sr1000speed",1);
	AddConsoleVar(&sv.sr1000timeout,"sr1000timeout",1);
	AddConsoleVar(&sv.sr1000ammohold,"sr1000ammohold",1);
	AddConsoleVar(&sv.sr1000reloadtime,"sr1000reloadtime",1);
	
	AddConsoleVar(&sv.scale,"scale",1);
	AddConsoleVar(&sv.positionresendtime,"positionresendtime",1);

	AddConsoleVar(&sv.radarenable,"radarenable",1);
	AddConsoleVar(&sv.chopperthrust,"chopperthrust",1);
	AddConsoleVar(&sv.thrustrechargerate,"thrustrechargerate",1);
	AddConsoleVar(&sv.thrustuse,"thrustuse",1);

	AddConsoleVar(&sv.gamemode,"gamemode",1);

	AddConsoleVar(&sv.timelimit,"timelimit",1);
	AddConsoleVar(&sv.capturelimit,"capturelimit",1);
	CfgFile svini;
	svini.Load("sv.ini");
	for(int i=0;i<numconsolevar;i++)
	{
		switch(consolevartype[i])
		{
		case 1:
			*(float *)consolevarptr[i]=svini.GetValuef(consolevar[i]);
			break;
		case 2:
			*(int *)consolevarptr[i]=svini.GetValuef(consolevar[i]);
			break;
		}
	}
	AddConsoleVar(&graphicsprefs.drawdistance,"drawdistance",0);

	AddConsoleVar(&graphicsprefs.stereoseparation,"stereoseperation",0);

}
extern HDC hdc;
extern HBRUSH backbrush;
extern HWND hWnd;

void DrawConsoleNonGL()
{
#ifndef HKDS
	return;
#endif
	RECT backrect;
	GetClientRect(hWnd,&backrect);
	
	FillRect(hdc,&backrect,backbrush);
	SetTextColor(hdc,0xeeeeee);
	SetBkMode(hdc,TRANSPARENT);
	int ypos;
	int lineheight=16;
	ypos=backrect.bottom-3;
	ypos-=lineheight+3;
	TextOut(hdc,3,ypos,consolecommand,strlen(consolecommand));
	for(int i=0;i<50;i++)
	{
		ypos-=lineheight;
		TextOut(hdc,3,ypos,consoletext[i],strlen(consoletext[i]));
	}
}