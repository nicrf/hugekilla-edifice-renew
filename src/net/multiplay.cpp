//all the necessary functions to connect to a server, and perform all necessary communications
//like sending a building, sending the land information, movement, chat messages
//uses the class Sock intensively

#include "StdAfx.h"
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
#include "version.h"
#define MAXLOCALLIST 128
#ifndef HK_WIN32_NATIVE
#ifndef _WIN32
#include <netdb.h>
#endif
#include <SDL.h>
#endif

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
	unsigned char *b=(unsigned char *)&addr.sin_addr;
	b[0]=a1;
	b[1]=a2;
	b[2]=a3;
	b[3]=a4;
	return addr;
}

//---- game/protocol version (src/net/version.h) ----

//hash of the sizes of everything sent over the network as raw structs: two builds
//that lay them out differently (struct packing, field added) can't play together
static unsigned int LayoutHash()
{
	const unsigned int sizes[]=
	{
		sizeof(SimpleMessage),sizeof(PlayerPosition),sizeof(FireGunDirection),
		sizeof(CreateObject),sizeof(LandInfo),sizeof(UpdateServerObject),
		sizeof(KilledPosition),sizeof(ServerVars),sizeof(RequestEnter),sizeof(Info),
		sizeof(ServerObject),sizeof(RemoteGameData)
	};
	unsigned int h=2166136261u;   //FNV-1a
	for(unsigned int i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++)
		for(int b=0;b<4;b++)
		{
			h^=(sizes[i]>>(b*8))&0xff;
			h*=16777619u;
		}
	return h;
}

int WriteVersionTrailer(char *buf)
{
	unsigned char *p=(unsigned char *)buf;
	unsigned int magic=EDIFICE_NET_MAGIC,layout=LayoutHash();
	int pos=0;
	for(int b=0;b<4;b++) p[pos++]=(magic>>(b*8))&0xff;
	p[pos++]=EDIFICE_PROTOCOL_VERSION&0xff;
	p[pos++]=(EDIFICE_PROTOCOL_VERSION>>8)&0xff;
	for(int b=0;b<4;b++) p[pos++]=(layout>>(b*8))&0xff;
	int n=strlen(EDIFICE_VERSION_STRING);
	if(n>15) n=15;
	p[pos++]=n;
	memcpy(&p[pos],EDIFICE_VERSION_STRING,n);
	return pos+n;
}

void ReadVersionTrailer(const char *buf,int len,PeerVersion *out)
{
	const unsigned char *p=(const unsigned char *)buf;
	memset(out,0,sizeof(*out));
	out->protocol=1;
	strcpy(out->version,"old");
	if(len<11) return;
	unsigned int magic=p[0]|(p[1]<<8)|(p[2]<<16)|((unsigned int)p[3]<<24);
	if(magic!=EDIFICE_NET_MAGIC) return;
	out->present=1;
	out->protocol=p[4]|(p[5]<<8);
	out->layout=p[6]|(p[7]<<8)|(p[8]<<16)|((unsigned int)p[9]<<24);
	int n=p[10];
	if(n>15) n=15;
	if(n>len-11) n=len-11;
	for(int i=0;i<n;i++)
		out->version[i]=(p[11+i]>=32&&p[11+i]<127)?p[11+i]:'?';
	out->version[n]=0;
}

int VersionCompatible(const PeerVersion *peer,char *reason,int reasonsize)
{
	if(peer->present&&peer->protocol==EDIFICE_PROTOCOL_VERSION&&peer->layout==LayoutHash())
	{
		if(reasonsize) reason[0]=0;
		return 1;
	}
	if(!peer->present)
		snprintf(reason,reasonsize,"original game (p1), you %s p%d",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	else if(peer->protocol!=EDIFICE_PROTOCOL_VERSION)
		snprintf(reason,reasonsize,"other %s p%d, you %s p%d",peer->version,peer->protocol,EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	else
		snprintf(reason,reasonsize,"same protocol, other build (%s)",peer->version);
	return 0;
}

//client asks infosock.dest to join, with its version
static void SendRequestEnter()
{
	char buf[2+32+VERSIONTRAILER_MAX];
	int nl=strlen(netsettings.playername);
	if(nl>31) nl=31;
	buf[0]=REQUESTENTER;
	buf[1]=nl;
	memcpy(&buf[2],netsettings.playername,nl);
	buf[2+nl]=0;
	int len=2+nl+1;
	len+=WriteVersionTrailer(&buf[len]);
	infosock.Send(buf,len);
	infosock.Flush();
}

//server answers an info request, with its version
static void SendInfo(SOCKADDR_IN dest)
{
	char buf[2+100+VERSIONTRAILER_MAX];
	int nl=strlen(netsettings.servername);
	if(nl>99) nl=99;
	buf[0]=INFO;
	buf[1]=nl;
	memcpy(&buf[2],netsettings.servername,nl);
	buf[2+nl]=0;
	int len=2+nl+1;
	len+=WriteVersionTrailer(&buf[len]);
	infosock.dest=dest;
	infosock.Send(buf,len);
	infosock.Flush();
}

//server turns a client down: a SIMPLE message new clients understand, plus a
//console text (CMSG_INFO chat) that original clients display
static void RefuseClient(SOCKADDR_IN dest,char why,const char *text)
{
	infosock.dest=dest;
	SimpleMessage refuse=MakeSimple(why,EDIFICE_PROTOCOL_VERSION,0);
	infosock.Send(&refuse,sizeof(refuse));
	char chat[300];
	int len=strlen(text);
	if(len>250) len=250;
	chat[0]=CHATMESSAGE;
	chat[1]=CMSG_INFO;
	chat[2]=0;
	chat[3]=len&0xff;
	chat[4]=(len>>8)&0xff;
	memcpy(&chat[5],text,len);
	infosock.Send(chat,len+5);
	infosock.Flush();
}

//---- validation of what comes from the network (roadmap M2) ----

static int ValidPlayer(int n)
{
	return n>=0&&n<MAXPLAYERS;
}

static int ValidSObj(int n)
{
	return n>=0&&n<MAXSERVEROBJS;
}

static int ValidWeapon(int n)
{
	return n>=0&&n<10;
}

//keeps a file name received from the network inside the textures folder
static void SanitizeNetFileName(char *name,int size)
{
	name[size-1]=0;
	if(strstr(name,"..")||strchr(name,'/')||strchr(name,'\\')||strchr(name,':'))
		name[0]=0;
}

void HandleSimpleServer(char type,DWORD msg,DWORD msg2,SOCKADDR_IN source,int pnum);
void ReceivePosition(Message *msg,int sender);
void DeletePlayer(int num,int reason);
void ReceiveChatMessage(Message *msg,int msglen=-1);


//reads a configuration file

void LoadNetSettings()
{
	CfgFile netsetfile;
	netsetfile.Load(UserFile("net.ini"));
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
			SendInfo(source);
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
				int i;
				for(i=1;i<numplayers;i++)
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
				if(!ValidPlayer(msg2)) break;
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
		
	int msglen;
	while((msglen=infosock.Receive(&msg,&source)))
	{
		switch (msg.type)
		{
		case REQUESTENTER:
			{
				RequestEnter *req=(RequestEnter *)&msg;
				req->name[sizeof(req->name)-1]=0;
				int reqnamelen=(unsigned char)req->namelen;
				if(reqnamelen>31) reqnamelen=31;
				PeerVersion peer;
				ReadVersionTrailer((char *)&msg+2+reqnamelen+1,msglen-(2+reqnamelen+1),&peer);
				int addplayer=numplayers;
				
				char message[1000];
				strcpy(message,req->name);
				strcat(message," at ");
				strcat(message,inet_ntoa(source.sin_addr));
				strcat(message," requests to connect");
				ConsoleDisplayText(message);
				
				char reason[200];
				if(!VersionCompatible(&peer,reason,sizeof(reason)))
				{
					strcpy(message,"refused, ");
					strcat(message,reason);
					ConsoleDisplayText(message);
					sprintf(message,"This server runs Edifice %s (protocol %d): update your game to join.",
						EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
					RefuseClient(source,VERSIONMISMATCH,message);
					break;
				}
				//a retried request (its ENTER got lost) gets the same slot again
				int known;
				for(known=1;known<numplayers;known++)
					if(GetPlayer(known)->playstate==1&&
						clientsock[known].requestaddr.sin_addr.s_addr==source.sin_addr.s_addr&&
						clientsock[known].requestaddr.sin_port==source.sin_port)
						break;
				if(known<numplayers)
				{
					SimpleMessage enter=MakeSimple(ENTER,htons(clientsock[known].localport),known);
					infosock.dest=source;
					infosock.Send(&enter,sizeof(enter));
					infosock.Flush();
					break;
				}
				if(numplayers>=MAXPLAYERS) 
				{
					ConsoleDisplayText("server was full");
					RefuseClient(source,SERVERFULL,"The server is full.");
					break;
				}
				
				//each client gets its own server port: the first free one after datasendport
				int baseport=ntohs(netsettings.datasendport);
				SOCKET newsock=WSASocket(AF_INET,SOCK_DGRAM,0,0,0,WSA_FLAG_OVERLAPPED);
				int port=0;
				for(int p=baseport+1;p<=baseport+2*MAXPLAYERS&&!port&&newsock;p++)
				{
					int used=0;
					for(int c=1;c<numplayers;c++)
						if(clientsock[c].sock&&clientsock[c].localport==p) used=1;
					if(used) continue;
					SOCKADDR_IN self=MakeSockAddr(0,0,0,0,htons(p));
					if(bind(newsock,(SOCKADDR *)&self,sizeof(self))==0) port=p;
				}
				if(!port)
				{
					if(newsock) closesocket(newsock);
					ConsoleDisplayText("no free port for a new client");
					RefuseClient(source,SERVERFULL,"The server has no free port.");
					break;
				}

				strcpy(GetPlayer(addplayer)->name,req->name);
				clientsock[addplayer].sock=newsock;
				clientsock[addplayer].dest=source;
				clientsock[addplayer].dest.sin_port=htons(port);   //the client binds the same port
				clientsock[addplayer].localport=port;
				clientsock[addplayer].requestaddr=source;
				clientsock[addplayer].filterip=1;
				clientsock[addplayer].ResetReliable();
				SimpleMessage enter=MakeSimple(ENTER,htons(port),addplayer);
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
				
				for(int i=1;i<numplayers;i++)
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
				//anyone can reach the info port: only info requests are answered there
				if(simple->type2==REQUESTINFO)
					HandleSimpleServer(simple->type2,simple->msg,simple->msg2,source,0);
				break;
			}
		}
	}
	
	int i;
	for(i=1;i<numplayers;i++)
	{
		clientsock[i].CheckReliableResend();
		int clen;
		while((clen=clientsock[i].Receive(&msg,&source)))
		{
			GetPlayer(i)->timeout=curtime+70000;
			switch(msg.type)
			{
				case PLAYERPOSITION:
					{
						ReceivePosition(&msg,i);   //a client only moves itself
						break;
					}
				case SIMPLE:
				{
					SimpleMessage *simple=(SimpleMessage*)&msg;
					int ackcallback=simple->type2==0||simple->type2==RECEIVEDLANDINFO||
						simple->type2==RECEIVEDPLAYERNAMES||simple->type2==RECEIVEDJOINGAME||
						simple->type2==RECEIVEDBLOCKINFO;
					//"received" steps come from our own reliable acks, never from the client
					if(ackcallback&&!clientsock[i].fromack) break;
					HandleSimpleServer(simple->type2,simple->msg,simple->msg2,source,i);
					break;
				}
				case BLOCKS:
					{
						ReceiveBlockBuffer((char *)&msg,i,clen);
						
						break;
					}
				case CHATMESSAGE:
					{
						msg.buf[1]=i;
						ReceiveChatMessage(&msg,clen);
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
					if(!ValidPlayer(kp->killer)) break;
					kp->killed=i;
					GetPlayer(i)->pos=kp->pos;
					ReceiveKillMessage(i,kp->killer);
					if(server)
					{
						for(int i3=1;i3<numplayers;i3++)
							if(i3!=i)clientsock[i3].SendReliable(kp,sizeof(*kp),0);   //was &kp,sizeof(kp): sent the pointer
					}
					break;
				}
			}
		};
		if(curtime>GetPlayer(i)->timeout||clientsock[i].reliablefailed)
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
		case VERSIONMISMATCH:
		case SERVERFULL:
			{
				//only while waiting for an answer to REQUESTENTER
				if(GetLocalPlayer()->playstate!=1||connected) break;
				char text[200];
				if(type==SERVERFULL)
					strcpy(text,"Can't join:\rthe server is full");
				else
					snprintf(text,sizeof(text),"Can't join: server is\rprotocol %d, you %d.\rUpdate the game.",(int)msg,EDIFICE_PROTOCOL_VERSION);
				ConsoleDisplayText(text);
				Disconnect();
				GetLocalPlayer()->playstate=0;
#ifndef HK_WIN32_NATIVE
				JoinScreenShowError(text);
#endif
				break;
			}
		case ENTER:
			{
				if(GetLocalPlayer()->playstate==1&&connected==0&&msg2<MAXPLAYERS)
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
					datasock.filterip=1;   //from now on, only the server's IP
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
				if(!ValidPlayer(msg)) break;
				GetPlayer(msg)->playstate=10;
				consolevisible=0;
				if(msg==GetLocalNum()) ConsoleDisplayText("entering game");
				ResetPlayer(msg);
				break;
			}
		case DELETEPLAYER:
			{
				if(!ValidPlayer(msg)) break;
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
				if(!ValidPlayer(msg)) break;
				GetPlayer(msg)->ping=LOWORD(msg2);
				GetPlayer(msg)->packetloss=HIWORD(msg2);
				break;
			}
		case PAIN:
			{
				if(!ValidPlayer(msg)) break;
				SetSoundPosition(msg2+1,GetPlayer(msg)->pos);
				if(msg2<500) PlaySound(msg2+1,0);
				else PlaySound(msg2,0);
				break;
			}
		case KILLED:
			{
				if(!ValidPlayer(msg)||!ValidPlayer(msg2)) break;
				ReceiveKillMessage(msg,msg2);
				break;
			}
		case CHANGETEAM:
			{
				if(!ValidPlayer(msg)||msg2>2) break;
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
				if(credit>=0&&credit<(int)sizeof(GetLocalPlayer()->credits)) GetLocalPlayer()->credits[credit]=number;
				else if(credit<0)
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
		int frominfo=0;
		int msglen=datasock.Receive(&msg,&source);
		if(!msglen)
		{
			msglen=infosock.Receive(&msg,&source);
			frominfo=1;
		}
		if(!msglen) break;
		//before joining, game messages only come from the server we asked; anyone may
		//answer a LAN search (INFO)
		if(frominfo&&msg.type!=INFO)
		{
			if(source.sin_addr.s_addr!=infosock.dest.sin_addr.s_addr) continue;
			if(msg.type!=SIMPLE&&msg.type!=CHATMESSAGE) continue;
		}
		GetPlayer(0)->timeout=curtime+70000;
		//if(!(source.sin_addr.s_addr==infosock.dest.sin_addr.s_addr)) continue;
		switch(msg.type)
		{
			case INFO:
				{
					if(numlocallistaddr==MAXLOCALLIST) break;
					int i;
					for(i=0;i<numlocallistaddr;i++)
					{
						if(locallistaddr[i].sin_addr.s_addr==source.sin_addr.s_addr&&
							locallistaddr[i].sin_port==source.sin_port) break;
					}
					if(i!=numlocallistaddr) break;
					Info *info=(Info *)&msg;
					info->name[sizeof(info->name)-1]=0;
					int namelen=(unsigned char)info->namelen;
					if(namelen>99) namelen=99;
					PeerVersion peer;
					ReadVersionTrailer((char *)&msg+2+namelen+1,msglen-(2+namelen+1),&peer);
					locallistaddr[numlocallistaddr]=source;
					numlocallistaddr++;
#ifdef HK_WIN32_NATIVE
					SendDlgItemMessage(dlghwnd,IDC_LOCALGAMES,LB_ADDSTRING,0,(long)info->name);
#else
					JoinScreenAddGame(info->name,&peer);
#endif
					ConsoleDisplayText("received info");
					break;
				}
			case LANDINFO:
				{
					
					LandInfo *linfo=(LandInfo*)&msg;
					//bound everything the land builder uses as a count or a file name
					LandDefs *ld=&linfo->ldfs;
					if(ld->numdefs<0) ld->numdefs=0;
					if(ld->numdefs>NUMDEF-1) ld->numdefs=NUMDEF-1;
					if(ld->numgroundtex<0) ld->numgroundtex=0;
					if(ld->numgroundtex>9) ld->numgroundtex=9;
					for(int t=0;t<9;t++) SanitizeNetFileName(ld->groundtex[t],sizeof(ld->groundtex[t]));
					if(ld->numscenery>1000) ld->numscenery=1000;
					if(ld->numblotch>2000) ld->numblotch=2000;
					SkySettings *ss=&linfo->skysets;
					SanitizeNetFileName(ss->cloudtex,sizeof(ss->cloudtex));
					SanitizeNetFileName(ss->misttex,sizeof(ss->misttex));
					SanitizeNetFileName(ss->sound,sizeof(ss->sound));
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
						ReceivePosition(&msg,-1);
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
					int bufsize=msglen-1;   //bytes after the message type
					if(bufsize<1) break;
					int numnames=(unsigned char)buf[bufpos];
					bufpos++;
					if(numnames>MAXPLAYERS) numnames=MAXPLAYERS;
					for(int i=0;i<numnames;i++)
					{
						if(bufpos+11>bufsize) break;   //number, kills, deaths, team, name length
						int playernum=(unsigned char)buf[bufpos];
						bufpos++;
						int namelen=(unsigned char)buf[bufpos+9];
						if(!ValidPlayer(playernum)||namelen>31||bufpos+10+namelen>bufsize) break;
						CopyMemory(&GetPlayer(playernum)->kills,&buf[bufpos],4);
						bufpos+=4;
						CopyMemory(&GetPlayer(playernum)->deaths,&buf[bufpos],4);
						bufpos+=4;
						GetPlayer(playernum)->team=buf[bufpos];
						bufpos+=1;
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
					ReceiveBlockBuffer((char *)&msg,0,msglen);
					break;
				}
			case CHATMESSAGE:
				{
					//ConsoleDisplayText("chat message");
					ReceiveChatMessage(&msg,msglen);
					break;		
				}
			case FIREGUN:
				{
					HandleFireGun(0,&msg);
					break;
				}
			case SERVEROBJECTSNAPSHOT:
				{
					CopyMemory(sobject,&msg.buf,sizeof(ServerObject)*MAXSERVEROBJS);
					break;
				}
			case UPDATESERVEROBJECT:
				{
					UpdateServerObject *update=(UpdateServerObject *)&msg;
					if(ValidSObj(update->num)) ReceiveUpdateServerObject(update);
					break;
				}
			case KILLEDPOSITION:
				{
					KilledPosition *kp=(KilledPosition*)&msg;
					if(!ValidPlayer(kp->killed)||!ValidPlayer(kp->killer)) break;
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
			SendRequestEnter();
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
	if(connected&&GetLocalPlayer()->playstate>=10&&(curtime>GetPlayer(0)->timeout||datasock.reliablefailed))
	{
		ConsoleDisplayText("server timed out");
		Disconnect();
		GetLocalPlayer()->playstate=0;
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

#ifdef HK_WIN32_NATIVE
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
				SendRequestEnter();
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
#else

//in-game replacement for the connect dialog (drawn by DrawMenu)

int joinscreen;
char joinip[64];
int joinselected;
int joinipedited;
struct JoinGame
{
	char name[100];
	char version[16];
	int compatible;
	char reason[120];
};
JoinGame joingames[MAXLOCALLIST];
char joinstatus[200];   //last refusal or error, shown on the screen

static void JoinScreenRefresh()
{
	joinstatus[0]=0;
	infosock.dest=MakeSockAddr(255,255,255,255,netsettings.infosendport);
	SimpleMessage simple;
	simple.type=SIMPLE;
	simple.type2=REQUESTINFO;
	infosock.Send(&simple,sizeof(simple));
	infosock.Flush();
	numlocallistaddr=0;
	joinselected=0;
	ConsoleDisplayText("Sent info request");
}

//asks a server for its info; it shows up in the list when it answers

static void JoinScreenQueryIP()
{
	datasock.ResetReliable();
	infosock.dest=MakeSockAddr(255,255,255,255,netsettings.infosendport);
	in_addr_t addr=inet_addr(joinip);
	if(addr==INADDR_NONE)
	{
		addrinfo hints,*res=0;
		memset(&hints,0,sizeof(hints));
		hints.ai_family=AF_INET;
		hints.ai_socktype=SOCK_DGRAM;
		if(getaddrinfo(joinip,0,&hints,&res)!=0||!res)
		{
			ConsoleDisplayText("Unknown host");
			return;
		}
		addr=((sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
		freeaddrinfo(res);
	}
	infosock.dest.sin_addr.s_addr=addr;
	SimpleMessage simple;
	simple.type=SIMPLE;
	simple.type2=REQUESTINFO;
	infosock.Send(&simple,sizeof(simple));
	infosock.Flush();
	numlocallistaddr=0;
	joinselected=0;
	ConsoleDisplayText("Sent info request");
}

static void JoinScreenJoin()
{
	if(joinselected<0||joinselected>=numlocallistaddr) return;
	if(!joingames[joinselected].compatible)
	{
		snprintf(joinstatus,sizeof(joinstatus),"Can't join:\r%s",joingames[joinselected].reason);
		return;
	}
	joinstatus[0]=0;
	datasock.ResetReliable();
	infosock.dest=locallistaddr[joinselected];
	SendRequestEnter();
	connected=0;
	GetLocalPlayer()->playstate=1;
	GetLocalPlayer()->timeout=curtime+2000;
	joinscreen=0;
	ConsoleDisplayText("Sent connection request");
}

void OpenJoinScreen()
{
	LoadNetSettings();
	if(!infosock.sock)
	{
		infosock.sock=WSASocket(AF_INET,SOCK_DGRAM,0,0,0,WSA_FLAG_OVERLAPPED);
		SOCKADDR_IN self=MakeSockAddr(0,0,0,0,netsettings.infosendport);
		bind(infosock.sock,(SOCKADDR *)&self,sizeof(self));
		BOOL broadcast=TRUE;
		setsockopt(infosock.sock,SOL_SOCKET,SO_BROADCAST,(char *)&broadcast,sizeof(broadcast));
	}
	joinscreen=1;
	JoinScreenRefresh();
}

static void CloseJoinScreen()
{
	joinscreen=0;
	GetLocalPlayer()->playstate=0;
	if(infosock.sock)
	{
		closesocket(infosock.sock);
		infosock.sock=0;
	}
}

int IsJoinScreenOpen()
{
	return joinscreen;
}

void JoinScreenAddGame(const char *name,const PeerVersion *peer)
{
	if(numlocallistaddr<1||numlocallistaddr>MAXLOCALLIST) return;
	JoinGame &game=joingames[numlocallistaddr-1];
	strncpy(game.name,name,99);
	game.name[99]=0;
	strcpy(game.version,peer->version);
	game.compatible=VersionCompatible(peer,game.reason,sizeof(game.reason));
}

//back to the join screen after a refused connection, with the reason
void JoinScreenShowError(const char *text)
{
	OpenJoinScreen();
	strncpy(joinstatus,text,sizeof(joinstatus)-1);
	joinstatus[sizeof(joinstatus)-1]=0;
}

//returns 1 when the key was used by the join screen

int HandleJoinScreenKey(int sdlkey)
{
	if(!joinscreen) return 0;
	switch(sdlkey)
	{
	case 27:  //escape
		CloseJoinScreen();
		break;
	case '\r':
	case 0x40000058:  //keypad enter
		if(joinipedited&&joinip[0])
		{
			joinipedited=0;
			JoinScreenQueryIP();
		}
		else
			JoinScreenJoin();
		break;
	case '\b':
		{
			int len=strlen(joinip);
			if(len) joinip[len-1]=0;
			joinipedited=1;
		}
		break;
	case 0x4000003E:  //F5
		JoinScreenRefresh();
		break;
	case 0x40000052:  //up
		if(joinselected>0) joinselected--;
		break;
	case 0x40000051:  //down
		if(joinselected<numlocallistaddr-1) joinselected++;
		break;
	case 'v':
		if(SDL_GetModState()&(KMOD_GUI|KMOD_CTRL))
		{
			char *clip=SDL_GetClipboardText();
			if(clip)
			{
				strncpy(joinip,clip,sizeof(joinip)-1);
				joinip[sizeof(joinip)-1]=0;
				joinipedited=1;
				SDL_free(clip);
			}
		}
		break;
	}
	return 1;
}

void HandleJoinScreenText(const char *text)
{
	if(!joinscreen) return;
	if(SDL_GetModState()&(KMOD_GUI|KMOD_CTRL)) return;
	for(;*text;text++)
	{
		char c=*text;
		int len=strlen(joinip);
		if(len>=(int)sizeof(joinip)-1) break;
		if((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='.'||c=='-')
		{
			joinip[len]=c;
			joinip[len+1]=0;
			joinipedited=1;
		}
	}
}

void DrawJoinScreen()
{
	if(!joinscreen) return;
	glDisable(GL_TEXTURE_2D);
	glColor4f(0,0,0,.75f);
	glBegin(GL_QUADS);
	glVertex2f(-.1f,-.8f);
	glVertex2f(.99f,-.8f);
	glVertex2f(.99f,.6f);
	glVertex2f(-.1f,.6f);
	glEnd();
	glEnable(GL_TEXTURE_2D);
	glColor3f(1,1,1);
	DrawText(-.05f,.5f,"Multiplayer Game");
	DrawText(-.05f,.35f,"Internet game IP:");
	char line[200];
	snprintf(line,sizeof(line),"%s_",joinip);
	glColor3f(1,1,.5f);
	DrawText(-.05f,.27f,line);
	glColor3f(1,1,1);
	DrawText(-.05f,.12f,"Games found:");
	if(numlocallistaddr==0)
	{
		glColor3f(.6f,.6f,.6f);
		DrawText(-.05f,.04f,"(searching...)");
	}
	for(int i=0;i<numlocallistaddr&&i<8;i++)
	{
		const JoinGame &game=joingames[i];
		float dim=game.compatible?1:.5f;
		if(i==joinselected) glColor3f(dim,dim,.5f*dim);
		else glColor3f(.8f*dim,.8f*dim,.8f*dim);
		char name[17];
		strncpy(name,game.name,16);
		name[16]=0;
		snprintf(line,sizeof(line),"%s %s v%s",i==joinselected?">":" ",name,game.version);
		DrawText(-.05f,.04f-i*.08f,line);
	}
	if(joinstatus[0])
	{
		//one line per '\r'
		glColor3f(1,.55f,.45f);
		char status[200];
		strcpy(status,joinstatus);
		float y=-.4f;
		for(char *part=status;part;y-=.07f)
		{
			char *next=strchr(part,'\r');
			if(next) *next++=0;
			DrawText(-.05f,y,part);
			part=next;
		}
	}
	glColor3f(.5f,.5f,.5f);
	snprintf(line,sizeof(line),"You: v%s (protocol %d)",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	DrawText(-.05f,-.76f,line);
	glColor3f(.6f,.6f,.6f);
	DrawText(-.05f,-.6f,"Enter: look up / join");
	DrawText(-.05f,-.68f,"F5: LAN    Esc: back");
	glColor3f(1,1,1);
}

#endif // HK_WIN32_NATIVE

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

//sender: the client that sent it (server side), or -1 for positions relayed by the server
void ReceivePosition(Message *msg,int sender)
{
	PlayerPosition *pos=(PlayerPosition*)msg;
	int pnum=sender>=0?sender:pos->pnum;
	if(!ValidPlayer(pnum)) return;
	Player *src=GetPlayer(pnum);
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
	src->weapon=ValidWeapon(pos->weapon)?pos->weapon:0;
	if(sender>=0)
	{
		//a client only reports being alive or dead
		if(pos->state==10||pos->state==11)
			src->playstate=pos->state;
	}
	else if(pos->state!=1) 
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
		ReceiveBlockBuffer(blockbuffer,0,blockbufferpos);
	}
	else
	{
		datasock.SendReliable(blockbuffer,blockbufferpos,0);
	}
}

//creates a structure

//len: size of the message, type included
void ReceiveBlockBuffer(char *buf,int pnum,int len)
{
	if(len<5) return;
	if(server&&!GetPlayer(pnum)->credits[0])
	{
		return;
	}
	GiveCredit(pnum,0,-1);
						
	int pos=0;
	DWORD numblocks;
	CopyMemory(&numblocks,&buf[1],4);
	if(numblocks>(DWORD)(len-5)/9) numblocks=(len-5)/9;   //9 bytes per block
	pos=5;
	int i;
	for(i=0;i<numblocks;i++)
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
//msglen: size of the message as received, -1 when it was built locally
void ReceiveChatMessage(Message *msg,int msglen)
{
	char dest=msg->buf[0];
	if(msglen!=-1&&msglen<5) return;
	//score, objective and info lines come from the server only
	if(server&&msg->buf[1]&&dest!=CMSG_BROADCAST&&dest!=CMSG_TEAM) return;
	//if(server==1&&(dest!=CMSG_BROADCAST&&dest!=CMSG_TEAM)) return;
	


	char chatmessage[1000];
	char message[1000];
	
	WORD len;
	CopyMemory(&len,&msg->buf[2],2);
	if(len>255) len=255;
	if(msglen!=-1&&len>msglen-5) len=msglen-5;
	CopyMemory(chatmessage,&msg->buf[4],len);
	chatmessage[len]=0;
		
	if(dest==CMSG_BROADCAST||dest==CMSG_TEAM)
	{
		int sender=(unsigned char)msg->buf[1];
		snprintf(message,sizeof(message),"%.31s: %s",ValidPlayer(sender)?GetPlayer(sender)->name:"?",chatmessage);
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
	if(!ValidPlayer(firegun->pnum)) return;
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
	if(!ValidPlayer(killed)||!ValidPlayer(killer)) return;
	GetPlayer(killed)->deaths++;
	if(killed!=killer) GetPlayer(killer)->kills++;
	char cmsg[300];
	char *skilled=GetPlayer(killed)->name;
	char *skiller=GetPlayer(killer)->name;
	if(killed==killer) 
	{
		skilled="himself";
	}
		
	snprintf(cmsg,sizeof(cmsg),"%.31s killed %.31s",GetPlayer(killer)->name,skilled);
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
	if(!ValidPlayer(player)||team<0||team>2) return;
	char message[500];
	char *teamname="White";
	if(team==0) teamname="White";
	if(team==1) teamname="Red";
	if(team==2) teamname="Blue";
	snprintf(message,sizeof(message),"%.31s has joined team %s",GetPlayer(player)->name,teamname);
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
	if(!ValidSObj(update->num)) return;
	int size=(char *)&sobject->timeout-(char *)sobject;
	CopyMemory(&sobject[update->num],&update->updated,size);

}

//called by console.cpp for the command, "createdome"
//basically creates a server controlled object

void CreateInstallation(Vector pos,char team)
{
	if(!server) return;
	ServerObject *cur=0;
	int i;
	for(i=0;i<MAXSERVEROBJS;i++)
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