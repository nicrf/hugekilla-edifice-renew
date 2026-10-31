//all the necessary functions to connect to a server, and perform all necessary communications
//like sending a building, sending the land information, movement, chat messages
//uses the class Sock intensively

#include "stdafx.h"
#include "winsock2.h"
#include "cfgfile.h"
#include "resource.h"
#include "multiplay.h"
#include "land.h"
#include "hugekilla.h"
#include "redrawing.h"
#include "playing.h"
#include "multiplaystruct.h"
#include "math.h"
#include "mmsystem.h"
#include "sound.h"
#include "stdio.h"
#include "gameplay.h"
#include "sock.h"
#define MAXLOCALLIST 128

extern int consolevisible;
extern DWORD curtime;
extern Object object[MAXOBJS];
extern AseObj projectile[20];
extern ServerVars sv;
extern ServerObject sobject[MAXSERVEROBJS];
extern LandDefs ldfs;


RemoteGameData rgd;
WORD curport;
NetSettings netsettings;
int server;
int connected;

Sock infosock;
Sock datasock;
Sock clientsock[MAXPLAYERS];

int numplayers;

HWND dlghwnd;

int numlocallistaddr;
SOCKADDR_IN locallistaddr[MAXLOCALLIST];

char blockbuffer[50000];
DWORD blockbufferpos=0;
DWORD numblocks;

DWORD nextresend;

char curobjective[1000];
char curscore[1000];
int SendMultMessage(Message *msg,int len,int player);




//used to make a SimpleMessage structure

SimpleMessage MakeSimple(char type,DWORD msg,DWORD msg2)
{
	SimpleMessage simple;
	simple.type=SIMPLE;
	simple.type2=type;
	simple.msg=msg;
	simple.msg2=msg2;
	return simple;
}

//used to convert an ip address into a sockaddr

SOCKADDR_IN MakeSockAddr(char a1,char a2,char a3,char a4,WORD port)
{
	SOCKADDR_IN addr;
	//ZeroMemory(&addr,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=port;
	addr.sin_addr.S_un.S_un_b.s_b1=a1;
	addr.sin_addr.S_un.S_un_b.s_b2=a2;
	addr.sin_addr.S_un.S_un_b.s_b3=a3;
	addr.sin_addr.S_un.S_un_b.s_b4=a4;
	return addr;
}

void HandleSimpleServer(char type,DWORD msg,DWORD msg2,SOCKADDR_IN source,int pnum);
void ReceivePosition(Message *msg);
void DeletePlayer(int num,int reason);
void ReceiveChatMessage(Message *msg);


//reads a configuration file

void LoadNetSettings()
{
	CfgFile netsetfile;
	netsetfile.Load("net.ini");
	netsettings.infosendport=htons(netsetfile.GetValuei("infosendport"));
	netsettings.datasendport=htons(netsetfile.GetValuei("datasendport"));
	char *servername;
	servername=netsetfile.GetValues("servername");
	if(servername) strcpy(netsettings.servername,servername);
	char *playername;
	playername=netsetfile.GetValues("name");
	if(playername) strcpy(netsettings.playername,playername);
#ifdef HKDS
	strcpy(netsettings.playername,netsettings.servername);
#endif
}


//initializes windows sockets

void InitSocks()
{
	LoadNetSettings();
	WORD version=MAKEWORD(2,2);
	WSADATA vdata;
	WSAStartup(version,&vdata);
}

//creates sockets and initializes some things

void StartServer()
{
	if(server) return;
	Disconnect();
	server=1;
	LoadNetSettings();
	numplayers=1;
	
	SOCKADDR_IN self;
	ZeroMemory(&self,sizeof(self));
	infosock.sock=WSASocket(AF_INET,SOCK_DGRAM,0,0,0,WSA_FLAG_OVERLAPPED);
	
	self=MakeSockAddr(0,0,0,0,netsettings.infosendport);
	bind(infosock.sock,(SOCKADDR *)&self,sizeof(self));

	BOOL broadcast=TRUE;
	setsockopt(infosock.sock,SOL_SOCKET,SO_BROADCAST,(char *)&broadcast,sizeof(broadcast));

	SetLocalNum(0);
	GetLocalPlayer()->playstate=10;
#ifdef HKDS
	GetLocalPlayer()->playstate=1;
#endif

	ConsoleDisplayText("Started server");
	RedrawSplash(1,"Loading Textures");
	LoadTextures();
	RedrawSplash(1,"Loading Sky");
	LoadSky();
	RedrawSplash(1,"Creating Land");
	RandomLand(ldfs);
	CreateLand(ldfs);

	consolevisible=0;
	strcpy(GetLocalPlayer()->name,netsettings.playername);
	curport=netsettings.datasendport;
	LockCursor(1);
#ifndef HKDS
	GPlayerEnterGame(GetLocalNum());
#endif
}

//closes sockets

void Disconnect()
{
	LockCursor(0);
	if(infosock.sock) 
	{
		closesocket(infosock.sock);
		infosock.sock=0;
	}
	if(datasock.sock) 
	{
		closesocket(datasock.sock);
		datasock.sock=0;
	}
	for(int i=1;i<numplayers;i++)
	{
		if(clientsock[i].sock) closesocket(clientsock[i].sock);
		clientsock[i].sock=0;
	}
	server=0;
	connected=0;
	numplayers=0;
}

void CloseSocks()
{
	Disconnect();
	WSACleanup();
}

//sent to show a change in a player's state (like playing, dead, etc.)

void SendState(int srcnum,int destnum)
{
	SimpleMessage state;
	state.type=SIMPLE;
	state.type2=STATECHANGE;
	state.msg=GetPlayer(srcnum)->playstate;
	if(server)
	{
		clientsock[destnum].SendReliable(&state,sizeof(state),0);
	}
	else
	{
		datasock.SendReliable(&state,sizeof(state),0);
	}
}

//the server calls this when it receives a simplemessage

void HandleSimpleServer(char type,DWORD msg,DWORD msg2,SOCKADDR_IN source,int pnum)
{
	switch(type)
	{
		case REQUESTINFO:
		{
			Info info;
			info.type=INFO;
			info.namelen=strlen(netsettings.servername);
			strcpy(info.name,netsettings.servername);
			infosock.dest=source;
			infosock.Send(&info,2+info.namelen+1);
			infosock.Flush();
			ConsoleDisplayText("received info request");
			break;
		}
		case PORTACK:
			{

				static Message names;
				names.type=PLAYERNAMES;
				char *buf=names.buf;
				int bufpos=0;
				buf[bufpos]=numplayers;
				bufpos++;
				for(int i=0;i<numplayers;i++)
				{
					buf[bufpos]=i;
					bufpos++;
					*(int *)&buf[bufpos]=GetPlayer(i)->kills;
					bufpos+=4;
					*(int *)&buf[bufpos]=GetPlayer(i)->deaths;
					bufpos+=4;
					buf[bufpos]=GetPlayer(i)->team;
					bufpos+=1;
					int namelen=strlen(GetPlayer(i)->name);
					buf[bufpos]=namelen;
					bufpos++;
					CopyMemory(&buf[bufpos],GetPlayer(i)->name,namelen);
					bufpos+=namelen;
				}
				clientsock[pnum].SendReliable(&names,bufpos+1,RECEIVEDPLAYERNAMES);
				
				ConsoleDisplayText("received port acknowledgement, sending player names");
				break;
			}
		case RECEIVEDPLAYERNAMES:
			{
				SendLandInfo(pnum);
				ConsoleDisplayText("received player names acknowledgement, sending land info");
				break;
			}
		case RECEIVEDLANDINFO:
			{
				StartBlockBuffer();

				for(int x=0;x<BLOCKGRIDSIZE;x++)
				for(int y=0;y<BLOCKGRIDHEIGHT;y++)
				for(int z=0;z<BLOCKGRIDSIZE;z++)
				{
					Block *cur=GetBlock(x,y,z);
					if(cur->type)
					{
						AddBlockBuffer(x,y,z,cur->type,0,cur->top,cur->side,cur->bottom,0);
						if(blockbufferpos+20>BIGBUFSIZE)
						{
							*(DWORD *)&blockbuffer[1]=numblocks;
							clientsock[pnum].SendReliable(blockbuffer,blockbufferpos,0);
							StartBlockBuffer();
						}
					}
				}
				*(DWORD *)&blockbuffer[1]=numblocks;
				clientsock[pnum].SendReliable(blockbuffer,blockbufferpos,RECEIVEDBLOCKINFO);
				StartBlockBuffer();
				ConsoleDisplayText("received landinfo acknowledgement, sending blockinfo");
				char sobjectbuf[50000];
				sobjectbuf[0]=SERVEROBJECTSNAPSHOT;
				CopyMemory(&sobjectbuf[1],sobject,sizeof(ServerObject)*MAXSERVEROBJS);
				clientsock[pnum].SendReliable(sobjectbuf,sizeof(ServerObject)*MAXSERVEROBJS+1,0);
				SendServerVars(pnum); 
				SendRGD(pnum);
				
				
				
				break;
			}
		case RECEIVEDBLOCKINFO:
			{
				SimpleMessage joingame=MakeSimple(JOINGAME,pnum,0);
				for(int i=1;i<numplayers;i++)
				{
					clientsock[i].SendReliable(&joingame,sizeof(joingame),0);
				}
				for(i=0;i<numplayers;i++)
				if(GetPlayer(i)->playstate==10)
				{
					joingame=MakeSimple(JOINGAME,i,0);
					clientsock[pnum].SendReliable(&joingame,sizeof(joingame),0);
				}
				GPlayerEnterGame(pnum);
				ResetPlayer(pnum);
				GetPlayer(pnum)->playstate=10;
				
				ConsoleDisplayText("received blockinfo acknowledgement, sending objects and join game");
				break;
			}
		
		case PINGREPLY:
			{
				if(GetPlayer(pnum)->pingid==msg)
				{
					GetPlayer(pnum)->ping=(GetPlayer(pnum)->ping*3+(curtime-GetPlayer(pnum)->lastpingtime))/4;
					GetPlayer(pnum)->pingwasrecv=1;
					GetPlayer(pnum)->packetloss=(1-(float)msg2/clientsock[pnum].packetssent)*30000;
					for(int i=1;i<numplayers;i++)
					{
						SendSimple(i,PINGINFO,pnum,MAKEWPARAM(GetPlayer(pnum)->ping,GetPlayer(pnum)->packetloss));
					}
					SendSimple(pnum,PINGINFO,0,MAKEWPARAM(GetPlayer(0)->ping,GetPlayer(0)->packetloss));
				}
				break;
			}
		case PAIN:
			{
				for(int i=1;i<numplayers;i++)
				{
					if(i!=pnum) SendSimple(i,PAIN,pnum,msg2);
				}
				SetSoundPosition(msg2+1,GetPlayer(pnum)->pos);
				if(msg2<500) PlaySound(msg2+1,0);
				else PlaySound(msg2,0);
				
			}
			break;
		case KILLED:
			{
				ReceiveKillMessage(pnum,msg2);
				SimpleMessage killed=MakeSimple(KILLED,pnum,msg2);
				if(server)
				{
					for(int i=1;i<numplayers;i++)
						if(i!=pnum)clientsock[i].SendReliable(&killed,sizeof(killed),0);
				}
				break;
			}
				
		case CHANGETEAM:
			{
				ChangeTeam(pnum,msg2);
				break;
			}
		case CLAIMOBJECT:
			{
				ClaimObject(pnum);
				break;
			}
	}
}

//called every frame on the server side

void ServerLoop()
{
	GTick();
	static Message msg;
	SOCKADDR_IN source;
	infosock.CheckReliableResend();
		
	while(infosock.Receive(&msg,&source))
	{
		switch (msg.type)
		{
		case REQUESTENTER:
			{
				RequestEnter *req=(RequestEnter *)&msg;
				int addplayer=numplayers;
				
				char message[1000];
				strcpy(message,req->name);
				strcat(message," at ");
				strcat(message,inet_ntoa(source.sin_addr));
				strcat(message," requests to connect");
				ConsoleDisplayText(message);
				
				if(numplayers>=MAXPLAYERS) 
				{
					ConsoleDisplayText("server was full");
					SimpleMessage serverfull=MakeSimple(SERVERFULL,numplayers,0);
					break;
				}
				
				strcpy(GetPlayer(addplayer)->name,req->name);
				clientsock[addplayer].dest=source;
				curport=htons(curport)+1;  //now in normal form
				if(curport>netsettings.datasendport+25)
				{
					curport=netsettings.datasendport;
				}
				for(int i=0;i<numplayers;i++)
				{
					if(clientsock[i].dest.sin_port==curport)
					{
						curport++;
					}
				}
				curport=htons(curport);
				int port=curport;
				
				
				SOCKADDR_IN self;
				clientsock[addplayer].sock=WSASocket(AF_INET,SOCK_DGRAM,0,0,0,WSA_FLAG_OVERLAPPED);
				clientsock[addplayer].dest.sin_port=port;
				clientsock[addplayer].ResetReliable();

				ZeroMemory(&self,sizeof(self));
				self=MakeSockAddr(0,0,0,0,port);

				bind(clientsock[addplayer].sock,(SOCKADDR *)&self,sizeof(self));
				SimpleMessage enter=MakeSimple(ENTER,clientsock[addplayer].dest.sin_port,addplayer);
				infosock.dest=source;
				infosock.Send(&enter,sizeof(enter));
				infosock.Flush();
				SimpleMessage tmpmsg=MakeSimple(PORTOPENER,0,0);
				/*for(int m=0;m<30;m++)
				{
					clientsock[addplayer].Send(&tmpmsg,sizeof(tmpmsg));
					clientsock[addplayer].Flush();
				}*/
				strcpy(message,GetPlayer(addplayer)->name);
				strcat(message," has connected");
				ConsoleDisplayText(message);
				GetPlayer(addplayer)->playstate=1;
				GetPlayer(addplayer)->kills=0;
				GetPlayer(addplayer)->deaths=0;
				static Message names;
				names.type=PLAYERNAMES;
				char *buf=names.buf;
				int bufpos=0;
				buf[bufpos]=1;
				bufpos++;
				buf[bufpos]=addplayer;
				bufpos++;
				*(int *)&buf[bufpos]=GetPlayer(addplayer)->kills;
				bufpos+=4;
				*(int *)&buf[bufpos]=GetPlayer(addplayer)->deaths;
				bufpos+=4;
				buf[bufpos]=GetPlayer(addplayer)->team;
				bufpos+=1;
				
				int namelen=strlen(GetPlayer(addplayer)->name);
				buf[bufpos]=namelen;
				bufpos++;
				CopyMemory(&buf[bufpos],GetPlayer(addplayer)->name,namelen);
				bufpos+=namelen;
				
				for(i=1;i<numplayers;i++)
				{
					clientsock[i].SendReliable(&names,bufpos+1,0);
				}
				GetPlayer(addplayer)->timeout=curtime+70000;
				GetPlayer(addplayer)->gunup=Vector(0,1,0);
				clientsock[addplayer].packetssent=0;
				clientsock[addplayer].packetsreceived=0;
				numplayers++;
				
				break;
			}	
			
		case SIMPLE:
			{
				SimpleMessage *simple=(SimpleMessage*)&msg;
				HandleSimpleServer(simple->type2,simple->msg,simple->msg2,source,0);
				break;
			}
		}
	}
	
	for(int i=1;i<numplayers;i++)
	{
		clientsock[i].CheckReliableResend();
		while(clientsock[i].Receive(&msg,&source))
		{
			GetPlayer(i)->timeout=curtime+70000;
			switch(msg.type)
			{
				case PLAYERPOSITION:
					{
						ReceivePosition(&msg);
						break;
					}
				case SIMPLE:
				{
					SimpleMessage *simple=(SimpleMessage*)&msg;
					HandleSimpleServer(simple->type2,simple->msg,simple->msg2,source,i);
					break;
				}
				case BLOCKS:
					{
						ReceiveBlockBuffer((char *)&msg,i);
						
						break;
					}
				case CHATMESSAGE:
					{
						msg.buf[1]=i;
						ReceiveChatMessage(&msg);
						break;
					}
				case FIREGUN:
				{
					FireGunDirection *firegun=(FireGunDirection *)&msg;
					TryFireGun(i,firegun->pos,firegun->dir,firegun->reload);
					break;
				}
				case KILLEDPOSITION:
				{
					KilledPosition *kp=(KilledPosition*)&msg;
					GetPlayer(i)->pos=kp->pos;
					ReceiveKillMessage(i,kp->killer);
					if(server)
					{
						for(int i3=1;i3<numplayers;i3++)
							if(i3!=i)clientsock[i3].SendReliable(&kp,sizeof(kp),0);
					}
					break;
				}
			}
		};
		if(curtime>GetPlayer(i)->timeout)
		{
			DeletePlayer(i,0);
		}
		if((GetPlayer(i)->pingwasrecv&&(curtime>GetPlayer(i)->lastpingtime+1000))||
			curtime>GetPlayer(i)->lastpingtime+500)
		{
			GetPlayer(i)->pingid++;
			GetPlayer(i)->pingwasrecv=0;
			GetPlayer(i)->lastpingtime=curtime;
			SendSimple(i,PING,GetPlayer(i)->pingid,curtime);
			clientsock[i].Flush();
		}
	}
	GetPlayer(0)->ping=curtime-GetPlayer(0)->lastpingtime;
	GetPlayer(0)->lastpingtime=curtime;
	infosock.Flush();
	
	if(curtime>nextresend)
	{
		char localnum=GetLocalNum();
		for(i=0;i<numplayers;i++)
		for(int i2=1;i2<numplayers;i2++)
		{
			if(localnum!=i2&&i!=i2&&GetPlayer(i2)->playstate>=10&&GetPlayer(i)->playstate>=10)
				SendPosition(i,i2);
		}
		nextresend=curtime+sv.positionresendtime;
	}
	for(i=0;i<numplayers;i++)
		{
			clientsock[i].Flush();
		}
	
}

//quick way to send a simplemessage

void SendSimple(char num,char type,DWORD msg,DWORD msg2)
{
	SimpleMessage simple;
	simple.type=SIMPLE;
	simple.type2=type;
	simple.msg=msg;
	simple.msg2=msg2;
	if(server)
		clientsock[num].Send(&simple,sizeof(SimpleMessage));
	else
		datasock.Send(&simple,sizeof(SimpleMessage));
}

//client calls this when it receives a simplemessage

void HandleSimpleClient(char type,DWORD msg,DWORD msg2,SOCKADDR_IN source)
{
	switch(type)
	{
		case ENTER:
			{
				if(GetLocalPlayer()->playstate==1&&connected==0)
				{
					connected=1;
					SetLocalNum(msg2);
					GetLocalPlayer()->playstate=1;
					GetLocalPlayer()->timeout=curtime+70000;
					numplayers=msg2+1;
					datasock.sock=WSASocket(AF_INET,SOCK_DGRAM,0,0,0,WSA_FLAG_OVERLAPPED);
					SOCKADDR_IN self;
					//ZeroMemory(&self,sizeof(self));
					self=MakeSockAddr(0,0,0,0,msg);
					int g=bind(datasock.sock,(SOCKADDR *)&self,sizeof(self));
					datasock.dest=source;
					datasock.dest.sin_port=msg;
					SimpleMessage portack;
					portack.type=SIMPLE;
					portack.type2=PORTACK;
					portack.msg=msg;
					datasock.SendReliable(&portack,sizeof(portack),0);
					closesocket(infosock.sock);
					infosock.sock=0;
					ConsoleDisplayText("connection accepted");
					datasock.packetssent=0;
					datasock.packetsreceived=0;
					LockCursor(1);
				}
				break;
			}
		case JOINGAME:
			{
				GetPlayer(msg)->playstate=10;
				consolevisible=0;
				if(msg==GetLocalNum()) ConsoleDisplayText("entering game");
				ResetPlayer(msg);
				break;
			}
		case DELETEPLAYER:
			{
				DeletePlayer(msg,msg2);
				break;
			}
		case PING:
			{
				SendSimple(-1,PINGREPLY,msg,datasock.packetsreceived);
				break;
			}
		case PINGINFO:
			{
				GetPlayer(msg)->ping=LOWORD(msg2);
				GetPlayer(msg)->packetloss=HIWORD(msg2);
				break;
			}
		case PAIN:
			{
				SetSoundPosition(msg2+1,GetPlayer(msg)->pos);
				if(msg2<500) PlaySound(msg2+1,0);
				else PlaySound(msg2,0);
				break;
			}
		case KILLED:
			{
				ReceiveKillMessage(msg,msg2);
				break;
			}
		case CHANGETEAM:
			{
				ReceiveChangeTeam(msg,msg2);
				break;
			}
		case SCORERESET:
			{
				ScoreReset();
				break;
			}
		case CREDITS:
			{
				int credit=msg;
				int number=msg2;
				if(credit>=0) GetLocalPlayer()->credits[credit]=number;
				else
					ZeroMemory(&GetLocalPlayer()->credits,sizeof(GetLocalPlayer()->credits));
				break;
			}
	}
}

//called every frame on the client side

void ClientLoop()
{
	static Message msg;
	SOCKADDR_IN source;
	datasock.CheckReliableResend();
	infosock.CheckReliableResend();
		
	while(1)
	{	
		if(!datasock.Receive(&msg,&source))
			if(!infosock.Receive(&msg,&source))
				break;
		GetPlayer(0)->timeout=curtime+70000;
		//if(!(source.sin_addr.s_addr==infosock.dest.sin_addr.s_addr)) continue;
		switch(msg.type)
		{
			case INFO:
				{
					if(numlocallistaddr==MAXLOCALLIST) break;
					for(int i=0;i<numlocallistaddr;i++)
					{
						if(locallistaddr[i].sin_addr.s_addr==source.sin_addr.s_addr&&
							locallistaddr[i].sin_port==source.sin_port) break;
					}
					if(i!=numlocallistaddr) break;
					Info *info=(Info *)&msg;
					locallistaddr[numlocallistaddr]=source;
					numlocallistaddr++;
					SendDlgItemMessage(dlghwnd,IDC_LOCALGAMES,LB_ADDSTRING,0,(long)info->name);
					ConsoleDisplayText("received info");
					break;
				}
			case LANDINFO:
				{
					
					LandInfo *linfo=(LandInfo*)&msg;
					CopyMemory(GetDefs(),&linfo->ldfs,sizeof(linfo->ldfs));
					RedrawSplash(1,"Loading Textures");
					LoadTextures();
					RedrawSplash(1,"Loading Sky");
					CopyMemory(GetSkySettings(),&linfo->skysets,sizeof(SkySettings));
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
					RedrawSplash(1,"Creating Land");
					CreateLand(*GetDefs());
					for(int i=0;i<numplayers;i++)
					{
						GetPlayer(i)->playstate=1;
					}
					ConsoleDisplayText("received landinfo");
					break;
				}
			case SIMPLE:
				{
					SimpleMessage *simple=(SimpleMessage*)&msg;
					HandleSimpleClient(simple->type2,simple->msg,simple->msg2,source);
					break;
				}
			case PLAYERPOSITION:
				{
					if(connected)
					{
						ReceivePosition(&msg);
					}
					break;
				}
			case SERVERVARS:
				{
					CopyMemory(&sv,msg.buf,sizeof(sv));
					break;
				}
			case PLAYERNAMES:
				{
					int bufpos=0;
					char *buf=msg.buf;
					int numnames=buf[bufpos];
					bufpos++;
					for(int i=0;i<numnames;i++)
					{
						int playernum=buf[bufpos];
						bufpos++;
						GetPlayer(playernum)->kills=*(int *)&buf[bufpos];
						bufpos+=4;
						GetPlayer(playernum)->deaths=*(int *)&buf[bufpos];
						bufpos+=4;
						GetPlayer(playernum)->team=buf[bufpos];
						bufpos+=1;
						int namelen=buf[bufpos];
						bufpos++;
						CopyMemory(GetPlayer(playernum)->name,&buf[bufpos],namelen);
						GetPlayer(playernum)->name[namelen]=0;
						bufpos+=namelen;
						if(playernum>=numplayers) 
						{
							char message[1000];
							strcpy(message,GetPlayer(playernum)->name);
							strcat(message," has joined the game");
							ConsoleDisplayText(message);
							numplayers=playernum+1;
						}
					}
					ConsoleDisplayText("received player names");
					break;
				}
			case REMOTEGAMEDATA:
				{
					CopyMemory(&rgd,msg.buf,sizeof(rgd));
					break;
				}
			case BLOCKS:
				{
					ReceiveBlockBuffer((char *)&msg,0);
					break;
				}
			case CHATMESSAGE:
				{
					//ConsoleDisplayText("chat message");
					ReceiveChatMessage(&msg);
					break;		
				}
			case FIREGUN:
				{
					HandleFireGun(0,&msg);
					break;
				}
			case SERVEROBJECTSNAPSHOT:
				{
					CopyMemory(sobject,&msg.buf,sizeof(ServerObject)*MAXSERVEROBJS,sizeof(ServerObject));
					break;
				}
			case UPDATESERVEROBJECT:
				{
					ReceiveUpdateServerObject((UpdateServerObject *)&msg);
					break;
				}
			case KILLEDPOSITION:
				{
					KilledPosition *kp=(KilledPosition*)&msg;
					GetPlayer(kp->killed)->pos=kp->pos;
					ReceiveKillMessage(kp->killed,kp->killer);
					break;
				}
			case 0:
				break;
			default:
				{
					ConsoleDisplayText("Unknown message received");
					break;
				}
		}
	}
	if(curtime>GetPlayer(0)->timeout&&GetLocalPlayer()->playstate==1)
	{
		if(connected==0)
		{
			RequestEnter req;
			req.type=REQUESTENTER;
			req.namelen=strlen(netsettings.playername);
			strcpy(req.name,netsettings.playername);
			infosock.Send(&req,2+req.namelen+1);
			infosock.Flush();
			GetPlayer(0)->timeout=curtime+2000;
			ConsoleDisplayText("retrying");
		}
		else
		{
			ConsoleDisplayText("server timed out");
			//GetPlayer(0)->timeout=curtime+60000;
			Disconnect();
			GetLocalPlayer()->playstate=0;
		}
	}
	if(curtime>nextresend)
	{
		if(GetLocalPlayer()->playstate>=10)SendPosition(GetLocalNum(),-1);
		nextresend=curtime+sv.positionresendtime;
	}
	datasock.Flush();
	infosock.Flush();
	
}

//called every frame

void SockLoop()
{
	if(server) 
		ServerLoop();
	else
		ClientLoop();
}


//window message handler for the connect dialog

LRESULT CALLBACK Multiplay(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	
	switch (message)
	{
		case WM_INITDIALOG:
			dlghwnd=hDlg;
			LoadNetSettings();
				return TRUE;
		case WM_DESTROY:
			dlghwnd=0;
			
				return TRUE;
		case WM_SHOWWINDOW:
			if(wParam)
			{
				SOCKADDR_IN self;
				infosock.sock=WSASocket(AF_INET,SOCK_DGRAM,0,0,0,WSA_FLAG_OVERLAPPED);
				
				self=MakeSockAddr(0,0,0,0,netsettings.infosendport);
				bind(infosock.sock,(SOCKADDR *)&self,sizeof(self));
				BOOL broadcast=TRUE;
				setsockopt(infosock.sock,SOL_SOCKET,SO_BROADCAST,(char *)&broadcast,sizeof(broadcast));
			}

				return 1;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				GetLocalPlayer()->playstate=0;
				EndDialog(hDlg, 0);
				return TRUE;
			}
			if(LOWORD(wParam)==IDC_PASTE)
			{
				OpenClipboard(hDlg);
				HANDLE clipboard=GetClipboardData(CF_OEMTEXT);
				CloseClipboard();
				SetDlgItemText(hDlg,IDC_IP,(char *)clipboard);

				return 1;
			}
			if(LOWORD(wParam)==IDC_REFRESH)
			{
				SOCKADDR_IN dest;
				infosock.dest=MakeSockAddr(255,255,255,255,netsettings.infosendport);
				SimpleMessage simple;
				simple.type=SIMPLE;
				simple.type2=REQUESTINFO;
				infosock.Send(&simple,sizeof(simple));
				infosock.Flush();
				SendDlgItemMessage(dlghwnd,IDC_LOCALGAMES,LB_RESETCONTENT,0,0);
				numlocallistaddr=0;
				ConsoleDisplayText("Sent info request");
				return 1;			
			}
			if(LOWORD(wParam)==IDC_JOININTERNET)
			{
				datasock.ResetReliable();
				SOCKADDR_IN dest;
				infosock.dest=MakeSockAddr(255,255,255,255,netsettings.infosendport);
				char iptext[40];
				GetWindowText(GetDlgItem(hDlg,IDC_IP),iptext,40);
				
				infosock.dest.sin_addr.s_addr=inet_addr(iptext);
				SimpleMessage simple;
				simple.type=SIMPLE;
				simple.type2=REQUESTINFO;
				infosock.Send(&simple,sizeof(simple));
				infosock.Flush();
				SendDlgItemMessage(dlghwnd,IDC_LOCALGAMES,LB_RESETCONTENT,0,0);
				numlocallistaddr=0;
				ConsoleDisplayText("Sent info request");
				return 1;			
			}
			if(LOWORD(wParam)==IDC_JOINLOCAL)
			{
				datasock.ResetReliable();
				HWND listhwnd=GetDlgItem(dlghwnd,IDC_LOCALGAMES);
				int lbsel=SendMessage(listhwnd,LB_GETCURSEL,0,0);
				if(lbsel==LB_ERR) return 1;
				infosock.dest=locallistaddr[lbsel];
				RequestEnter req;
				req.type=REQUESTENTER;
				req.namelen=strlen(netsettings.playername);
				strcpy(req.name,netsettings.playername);
				infosock.Send(&req,2+req.namelen+1);
				infosock.Flush();
				connected=0;
				GetLocalPlayer()->playstate=1;
				GetLocalPlayer()->timeout=curtime+2000;
				EndDialog(hDlg,1);
				ConsoleDisplayText("Sent connection request");
			}
			break;
	}
    return FALSE;
}

int GetNumPlayers()
{
	return numplayers;
}
int IsServer()
{
	return server;
}
int IsConnected()
{
	return connected;
}

//called by the client and server when a player leaves

void DeletePlayer(int num,int reason)
{
	
	if(server)
	{
		GPlayerLeave(num);
		SimpleMessage deleteplayer=MakeSimple(DELETEPLAYER,num,reason);
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(&deleteplayer,sizeof(deleteplayer),0);
		}
		
		closesocket(clientsock[num].sock);
		
		clientsock[num].ResetReliable();
		
	}
	int localnum=GetLocalNum();
	if(localnum==num)
	{
		Disconnect();
		SetLocalNum(0);
		GetLocalPlayer()->playstate=0;
	}
	if(localnum>num)
	{
		SetLocalNum(localnum-1);
	}
	char message[1000];
	strcpy(message,GetPlayer(num)->name);
	switch (reason)
	{
	case 0:
		strcat(message," has timed out");
		break;
	case 1:
		strcat(message," has left the game");
		break;
	case 2:
		strcat(message," has been kicked");
		break;
	}
	ConsoleDisplayText(message);
	numplayers--;
	for(int i=num;i<numplayers;i++)
	{
		CopyMemory(GetPlayer(i),GetPlayer(i+1),sizeof(Player));
		if(server) CopyMemory(&clientsock[i],&clientsock[i+1],sizeof(Sock));
	}
	ZeroMemory(GetPlayer(numplayers),sizeof(Player));
	ZeroMemory(&clientsock[numplayers],sizeof(Sock));
	
}

//sends the land information necessary to construct the same landscape

void SendLandInfo(int num)
{
	LandInfo landinfo;
	landinfo.type=LANDINFO;
	CopyMemory(&landinfo.ldfs,GetDefs(),sizeof(landinfo.ldfs));
	CopyMemory(&landinfo.skysets,GetSkySettings(),sizeof(SkySettings));
	if(num>0)
	{
		GetPlayer(num)->playstate=1;
		clientsock[num].SendReliable(&landinfo,sizeof(landinfo),RECEIVEDLANDINFO);
	}
	else
	{
		for(int i=1;i<numplayers;i++)
		{
			GetPlayer(i)->playstate=1;
			clientsock[i].SendReliable(&landinfo,sizeof(landinfo),RECEIVEDLANDINFO);
		}
	}
}


#define pi 3.14159265


//converts the angle made by x and y to a byte

unsigned char GetAng(float x,float y)
{
	float a=atan(y/x);
	if(x<0)a-=pi;
	a/=pi;
	a+=.5;
	a*=127;
	a+=1;
	return -a;	
}

//called to receive a player position

void ReceivePosition(Message *msg)
{
	PlayerPosition *pos=(PlayerPosition*)msg;
	Player *src=GetPlayer(pos->pnum);
	src->up=!!((1<<0)&pos->keys);
	src->down=!!((1<<1)&pos->keys);
	src->lstrafe=!!((1<<2)&pos->keys);
	src->rstrafe=!!((1<<3)&pos->keys);
	src->jetpack=!!((1<<4)&pos->keys);
	src->hanggliding=!!((1<<5)&pos->keys);
	src->pos.x=(float)pos->posx/65536*LANDGRIDSIZE;
	src->pos.y=(float)pos->posy/65536*LANDGRIDSIZE;
	src->pos.z=(float)pos->posz/65536*LANDGRIDSIZE;
	src->fwd.x=sin(((float)pos->fwd/255-.5)*pi*2);
	src->fwd.z=cos(((float)pos->fwd/255-.5)*pi*2);
	src->tilt.x=sin(((float)pos->tilt/255-.5)*pi*2);
	src->tilt.y=cos(((float)pos->tilt/255-.5)*pi*2);
	src->vel.x=(float)pos->velx/64;
	src->vel.y=(float)pos->vely/64;
	src->vel.z=(float)pos->velz/64;
	src->weapon=pos->weapon;
	if(pos->state!=1) 
		src->playstate=pos->state;
	MakeClose(&src->pos);
}

//sends a certain player's position

void SendPosition(char srcnum,char destnum)
{
	Player *src=GetPlayer(srcnum);
	PlayerPosition pos;
	pos.type=PLAYERPOSITION;
	
	pos.keys=0;
	pos.keys|=(1<<0)*src->up;
	pos.keys|=(1<<1)*src->down;
	pos.keys|=(1<<2)*src->lstrafe;
	pos.keys|=(1<<3)*src->rstrafe;
	pos.keys|=(1<<4)*src->jetpack;
	pos.keys|=(1<<5)*src->hanggliding;
	
	pos.posx=src->pos.x/LANDGRIDSIZE*65536;
	pos.posy=src->pos.y/LANDGRIDSIZE*65536;
	pos.posz=src->pos.z/LANDGRIDSIZE*65536;
	pos.fwd=GetAng(src->fwd.x,src->fwd.z);
	pos.tilt=GetAng(src->tilt.x,src->tilt.y);
	pos.velx=src->vel.x*64;
	pos.vely=src->vel.y*64;
	pos.velz=src->vel.z*64;
	pos.weapon=src->weapon;
	pos.pnum=srcnum;
	pos.state=src->playstate;
	if(!server)
	{
		datasock.Send(&pos,sizeof(pos));
	}
	else
	{
		clientsock[destnum].Send(&pos,sizeof(pos));
	}
}

//called in structure.cpp when a building is going to be sent

void StartBlockBuffer()
{
	numblocks=0;
	blockbufferpos=0;
	blockbuffer[blockbufferpos]=BLOCKS;
	blockbufferpos++;
	blockbufferpos+=4; //for numblocks
}

//called by structure.cpp

void AddBlockBuffer(char x,char y,char z,char type,char team,char top,char side,char bottom,char del)
{
	if(blockbufferpos+30>BIGBUFSIZE)
	{
		return;
	}
	blockbuffer[blockbufferpos]=x;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=y;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=z;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=type;
	blockbufferpos++;	
	blockbuffer[blockbufferpos]=team;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=top;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=side;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=bottom;
	blockbufferpos++;
	blockbuffer[blockbufferpos]=del;
	blockbufferpos++;
	numblocks++;
}

//finishes things off

void SendBlockBuffer()
{
	*(DWORD *)&blockbuffer[1]=numblocks;
	if(server)
	{
		ReceiveBlockBuffer(blockbuffer,0);
	}
	else
	{
		datasock.SendReliable(blockbuffer,blockbufferpos,0);
	}
}

//creates a structure

void ReceiveBlockBuffer(char *buf,int pnum)
{
	if(server&&!GetPlayer(pnum)->credits[0])
	{
		return;
	}
	GiveCredit(pnum,0,-1);
						
	int pos=0;
	DWORD numblocks=*(DWORD *)&buf[1];
	pos=5;
	for(int i=0;i<numblocks;i++)
	{
		char x,y,z,type,team,top,side,bottom,del;
		x=buf[pos];
		pos++;
		y=buf[pos];
		pos++;
		z=buf[pos];
		pos++;
		type=buf[pos];
		pos++;
		team=buf[pos];
		pos++;
		top=buf[pos];
		pos++;
		side=buf[pos];
		pos++;
		bottom=buf[pos];
		pos++;
		del=buf[pos];
		pos++;
		if(del)
		{
			DeleteBlock(x,y,z);
		}
		else
		{
			AddBlock(x,y,z,type,top,side,bottom);
		}
	}
	if(server)
	{
		for(i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(buf,pos,0);
		}
	}
}

int SendMultMessage(Message *msg,int len,int player=-1)
{
	int selfrecv=0;
	if(!server) return 0;
	for(int i=0;i<numplayers;i++)
	{
		if(player==TARGTEAM||GetPlayer(i)->team==player-TARGTEAM||player==-1||i==player)
		{
			if(i==0)
				selfrecv=1;
			else
				clientsock[i].SendReliable(msg,len,0);
		}
	}
	return selfrecv;
}


//sends a message reliably
void ReceiveChatMessage(Message *msg)
{
	char dest=msg->buf[0];
	//if(server==1&&(dest!=CMSG_BROADCAST&&dest!=CMSG_TEAM)) return;
	


	char chatmessage[1000];
	char message[1000];
	
	WORD len=*(WORD *)&msg->buf[2];
	CopyMemory(chatmessage,&msg->buf[4],len);
	chatmessage[len]=0;
		
	if(dest==CMSG_BROADCAST||dest==CMSG_TEAM)
	{
		strcpy(message,GetPlayer(msg->buf[1])->name);
		strcat(message,": ");
		strcat(message,chatmessage);
		ConsoleDisplayText(message);
	}
	if(dest==CMSG_SCORE)
	{
		strcpy(curscore,chatmessage);
	}
	if(dest==CMSG_OBJECTIVE)
	{
		strcpy(curobjective,chatmessage);
	}
	if(dest==CMSG_INFO)
	{
		ConsoleDisplayText(chatmessage);
	}
	/*if(server)
	{
		for(int i2=1;i2<numplayers;i2++)
		{
			clientsock[i2].SendReliable(msg,len+5,0);
		}
	}*/
	if(msg->buf[1])  //this came from a player (called from serverloop)
	{
		if(dest==CMSG_BROADCAST)
			SendMultMessage(msg,len+5,-1);
		if(dest==CMSG_TEAM)
			SendMultMessage(msg,len+5,GetPlayer(msg->buf[1])->team+TARGTEAM);
	}

}

void SendChatMessage(char *text,int dest,int player)
{
	static Message chat;
	chat.type=CHATMESSAGE;
	int bufpos=0;
	chat.buf[bufpos]=dest;
	bufpos++;
	chat.buf[bufpos]=0;
	bufpos++;
	WORD len=strlen(text);
	*(WORD *)&chat.buf[bufpos]=len;
	bufpos+=2;
	CopyMemory(&chat.buf[bufpos],text,len);
	bufpos+=len;
	if(server)
	{
		if(SendMultMessage(&chat,len+5,player))
			ReceiveChatMessage(&chat);
	}
	else
	{
		datasock.SendReliable(&chat,bufpos+1,0);
	}
}

//called when a player shoots

void CreateObject(int num,int obj,Vector pos,Vector dir,Vector vel)
{
	for(int i=0;i<MAXOBJS;i++)
	{
		if(object[i].type==0)
		{
			object[i].type=obj;
			object[i].pos=pos;
			object[i].vel=vel;
			object[i].dir=dir;
			object[i].pnum=num;
			object[i].timeout=curtime+1000;

			object[i].wasfroze=0;
			switch(obj)
			{
			case 0:
			case 1:
			case 2:
			case 3:
				object[i].object=&projectile[0];
				break;
			case 4:
				object[i].object=&projectile[2];
				object[i].timeout=curtime+sv.grenadeexplodetime;
				break;
			}
			break;
		}
	}
}

//called on all computers when someone fires

void HandleFireGun(int num,Message *msg)
{
	FireGunDirection *firegun=(FireGunDirection *)msg;
	float speed=0;
	if(GetPlayer(firegun->pnum)->weapon==0)
		speed=sv.gatspeed;
	if(GetPlayer(firegun->pnum)->weapon==1)
		speed=sv.m16speed;
	if(GetPlayer(firegun->pnum)->weapon==2)
		speed=sv.steyrspeed;
	if(GetPlayer(firegun->pnum)->weapon==3)
		speed=sv.umpspeed;
	if(GetPlayer(firegun->pnum)->weapon==4)
		speed=sv.grenadespeed;
	if(GetPlayer(firegun->pnum)->weapon==5)
		speed=sv.berettaspeed;
	if(GetPlayer(firegun->pnum)->weapon==6)
		speed=sv.sr1000speed;
	int objecttype=1;
	if(GetPlayer(firegun->pnum)->weapon==3) objecttype=3;
	if(GetPlayer(firegun->pnum)->weapon==5) objecttype=3;
	if(GetPlayer(firegun->pnum)->weapon==4) objecttype=4;
	speed*=2;
	if(speed==0) return;
	if(!firegun->reload) CreateObject(firegun->pnum,objecttype,firegun->pos-Vector(0,.2,0),firegun->dir,firegun->dir*speed);
	int soundnum=0;
	if(!firegun->reload)
	{
		float factor=1;
		if(GetPlayer(firegun->pnum)->weapon==0) factor=2.5;
		GetPlayer(firegun->pnum)->inaccuracyvel=Vector(RND-.5,RND-.3,RND-.5)*.02*factor;
		GetPlayer(firegun->pnum)->inaccuracy=GetPlayer(firegun->pnum)->inaccuracyvel*3*factor;
	}
	soundnum=GetPlayer(firegun->pnum)->weapon*4+2;
	if(firegun->reload) soundnum+=2;
	if(firegun->pnum==GetLocalNum()) soundnum--;
	if(soundnum)
	{
		SetSoundPosition(soundnum,GetPlayer(firegun->pnum)->pos);
		PlaySound(soundnum,0);
	}
}

//tries to fire the gun, and sends out the message that the gun was fired

int TryFireGun(int num,Vector pos,Vector dir,char reload)
{
	if(!reload)
	{
		if(GetPlayer(num)->ammoingun[GetPlayer(num)->weapon]<=0&&GetPlayer(num)==GetLocalPlayer())
			return 0;
		GetPlayer(num)->ammoingun[GetPlayer(num)->weapon]--;
		
		
	}
	FireGunDirection firegun;
	firegun.type=FIREGUN;
	firegun.pnum=num;
	firegun.dir=dir;
	firegun.pos=pos;
	firegun.reload=reload;
	HandleFireGun(0,(Message *)&firegun);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
		{
			if(i!=num) clientsock[i].Send(&firegun,sizeof(firegun));
		}
		
	}
	else
	{
		datasock.Send(&firegun,sizeof(firegun));
	}
	return 1;
}

//sends to all computers to play this sound

void HavePain(int sound,int targ)
{
	if(!server||(targ==-1||GetLocalPlayer()->team==targ-TARGTEAM||targ==0))
		PlaySound(sound,0);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
		{
			if(targ==-1||GetPlayer(i)->team==targ-TARGTEAM||targ==i) 
				SendSimple(i,PAIN,GetLocalNum(),sound);
		}
	}
	else
	{
		SendSimple(0,PAIN,GetLocalNum(),sound);
	}
}

//when someone dies somehow this is called

void ReceiveKillMessage(int killed,int killer)
{
	GetPlayer(killed)->deaths++;
	if(killed!=killer) GetPlayer(killer)->kills++;
	char cmsg[300];
	char *skilled=GetPlayer(killed)->name;
	char *skiller=GetPlayer(killer)->name;
	if(killed==killer) 
	{
		skilled="himself";
	}
		
	sprintf(cmsg,"%s killed %s",GetPlayer(killer)->name,skilled);
	ConsoleDisplayText(cmsg);
	if(server)
	for(int i=0;i<MAXSERVEROBJS;i++)
	if(sobject[i].type&&sobject[i].attached==killed)
	{
		ClaimObject(killed);
		break;
	}
}

//called by the person that was killed

void Killed(int killer)
{
	SimpleMessage killed=MakeSimple(KILLED,GetLocalNum(),killer);
	ReceiveKillMessage(GetLocalNum(),killer);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
			clientsock[i].SendReliable(&killed,sizeof(killed),0);
	}
	else
	{
		datasock.SendReliable(&killed,sizeof(killed),0);
	}
}

//called when someone joins

void ResetPlayer(int num)
{
	GetPlayer(num)->gunmainup=Vector(0,1,0);
	GetPlayer(num)->pos=Vector(0,0,0);
	GetPlayer(num)->gunup=Vector(0,1,0);
}


//sends the various server controlled variables, like gravity, gun properties, etc.

void SendServerVars(int num)
{
	Message servervars;
	servervars.type=SERVERVARS;
	CopyMemory(servervars.buf,&sv,sizeof(sv));
	if(server)
	{
		if(num)
		{
			clientsock[num].SendReliable(&servervars,sizeof(sv)+1,0);
		}
		else
		{
			for(int i=0;i<numplayers;i++)
			{
				clientsock[i].SendReliable(&servervars,sizeof(sv)+1,0);
			}
		}
	}
	else
	{
		datasock.SendReliable(&servervars,sizeof(sv)+1,0);
	}
}

void SendRGD(int player)
{
	Message msg;
	msg.type=REMOTEGAMEDATA;
	CopyMemory(msg.buf,&rgd,sizeof(rgd));
	SendMultMessage(&msg,sizeof(rgd)+1);
}
//called by everyone when someone changes team

void ReceiveChangeTeam(int player,int team)
{
	char message[500];
	char *teamname;
	if(team==0) teamname="White";
	if(team==1) teamname="Red";
	if(team==2) teamname="Blue";
	sprintf(message,"%s has joined team %s",GetPlayer(player)->name,teamname);
	ConsoleDisplayText(message);
	GetPlayer(player)->team=team;
}

//called by console.cpp when a player's team is changed

void ChangeTeam(int player,int team)
{
	if(team<0||team>2) return;
	if((team=GPlayerChangeTeam(player,team))==-1) return;
	SimpleMessage changeteam=MakeSimple(CHANGETEAM,player,team);
	if(server)
	{
		ReceiveChangeTeam(player,team);
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(&changeteam,sizeof(changeteam),0);
		}
	}
	else
	{
		datasock.SendReliable(&changeteam,sizeof(changeteam),0);
	}
}

//updates the position of an server controlled object (sent reliably so it only
//needs to be done once.

void SendUpdateServerObject(int num)
{
	UpdateServerObject update;
	update.type=UPDATESERVEROBJECT;
	update.num=num;
	int size=(char *)&sobject[num].timeout-(char *)&sobject[num];
	CopyMemory(&update.updated,&sobject[num],size);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(&update,sizeof(update),0);
		}
	}
}

void ReceiveUpdateServerObject(UpdateServerObject *update)
{
	int size=(char *)&sobject->timeout-(char *)sobject;
	CopyMemory(&sobject[update->num],&update->updated,size);

}

//called by console.cpp for the command, "createdome"
//basically creates a server controlled object

void CreateInstallation(Vector pos,char team)
{
	if(!server) return;
	ServerObject *cur=0;
	for(int i=0;i<MAXSERVEROBJS;i++)
	if(!sobject[i].type)
	{
		cur=&sobject[i];
		break;
	}
	if(!cur) return;
	cur->type=1;
	cur->pos=pos;
	cur->dir=Vector(1,0,0);
	cur->team=team;
	cur->vel=Vector(0,0,0);
	cur->attached=-1;
	cur->speedpenalty=1;
	SendUpdateServerObject(i);
}

//called when someone tries to pick up a server controlled object

void ClaimObject(int pnum)
{
	if(GetPlayer(pnum)->playstate!=10) return;
	if(!server)
	{
		SimpleMessage claim;
		claim=MakeSimple(CLAIMOBJECT,0,0);
		datasock.SendReliable(&claim,sizeof(claim),0);
		return;
	}
	Vector pos=GetPlayer(pnum)->pos;
	float leastmag=50;
	int candidate;
	for(int i=0;i<MAXSERVEROBJS;i++)
	{
		
		if(sobject[i].attached==-1)
		if(Magnitude(pos-sobject[i].pos)<leastmag&&GCanClaimObject(pnum,i))
		{
			leastmag=Magnitude(pos-sobject[i].pos);
			candidate=i;
		}
		if(sobject[i].attached==pnum&&sobject[i].type)
		{
			if(GDropObject(pnum,i))
			{
				sobject[i].attached=-1;
				sobject[i].pos.y-=1;
				SendUpdateServerObject(i);
			}
			return;
		}
	}
	if(leastmag<2.4)
	{
		GClaimObject(pnum,candidate);
	}
}
						
//resets the scores of all players

void ScoreReset()
{
	SimpleMessage scorereset=MakeSimple(SCORERESET,0,0);
	ConsoleDisplayText("Scores reset");
	for(int i=0;i<numplayers;i++)
	{
		GetPlayer(i)->kills=0;
		GetPlayer(i)->deaths=0;
	}
	if(server)
	for(int i=1;i<numplayers;i++)
	{
		clientsock[i].SendReliable(&scorereset,sizeof(scorereset),0);
	}
}

void GiveCredit(int player, int credit, int number)
{
	if(!server) return;
	for(int i=0;i<numplayers;i++)
	{
		if(player==TARGTEAM||GetPlayer(i)->team==player-TARGTEAM||player==-1||i==player)
		{
			if(credit>=0)
			{
				if(number==0) GetPlayer(i)->credits[credit]=0;
				GetPlayer(i)->credits[credit]+=number;
			}
			else
			{
				ZeroMemory(&GetPlayer(i)->credits,sizeof(GetPlayer(i)->credits));
			}
			SimpleMessage crd;
			if(credit>=0)
				crd=MakeSimple(CREDITS,credit,GetPlayer(i)->credits[credit]);
			else
				crd=MakeSimple(CREDITS,credit,0);
			if(i>0) 
				clientsock[i].SendReliable(&crd,sizeof(crd),0);
		}
	}
}