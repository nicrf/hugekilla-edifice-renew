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
#include "udplink.h"
#include "version.h"
#include "wire.h"
#include "netmsg.h"
#include "ui.h"
#include "voice.h"
#include "lobbygame.h"
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
int SendMultMessage(const void *msg,int len,int player);




//used to make a SIMPLE message, serialized: send simple.b,sizeof(simple.b)

SimpleBuf MakeSimple(char type,DWORD msg,DWORD msg2)
{
	SimpleBuf simple;
	WriteSimple(simple.b,type,msg,msg2);
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

//---- game/protocol version: src/net/version.h, version.cpp ----

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
	char buf[2+100+VERSIONTRAILER_MAX+4];
	int nl=strlen(netsettings.servername);
	if(nl>99) nl=99;
	buf[0]=INFO;
	buf[1]=nl;
	memcpy(&buf[2],netsettings.servername,nl);
	buf[2+nl]=0;
	int len=2+nl+1;
	len+=WriteVersionTrailer(&buf[len]);
	//server details for the join list (ignored by older clients)
	buf[len++]=(char)numplayers;
	buf[len++]=(char)MAXPLAYERS;
	buf[len++]=(char)sv.gamemode;
	buf[len++]=(char)sv.capturelimit;
	infosock.dest=dest;
	infosock.Send(buf,len);
	infosock.Flush();
}

//server turns a client down: a SIMPLE message new clients understand, plus a
//console text (CMSG_INFO chat) that original clients display
static void RefuseClient(SOCKADDR_IN dest,char why,const char *text)
{
	infosock.dest=dest;
	SimpleBuf refuse=MakeSimple(why,EDIFICE_PROTOCOL_VERSION,0);
	infosock.Send(refuse.b,sizeof(refuse.b));
	char chat[300];
	int len=strlen(text);
	if(len>250) len=250;
	ByteWriter w(chat,sizeof(chat));
	w.U8(CHATMESSAGE);
	w.U8(CMSG_INFO);
	w.U8(0);
	w.U16(len);
	w.Bytes(text,len);
	infosock.Send(chat,w.Size());
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

//wire format of each message: src/net/netmsg.h

//PLAYERNAMES: uint8 count, then per player: uint8 number, int32 kills,
//int32 deaths, int8 team, uint8 name length (<=31), name
static void WritePlayerName(ByteWriter &w,int i)
{
	Player *pl=GetPlayer(i);
	int namelen=strlen(pl->name);
	if(namelen>31) namelen=31;
	w.U8(i);
	w.I32(pl->kills);
	w.I32(pl->deaths);
	w.U8((unsigned char)pl->team);
	w.U8(namelen);
	w.Bytes(pl->name,namelen);
}

void HandleSimpleServer(char type,DWORD msg,DWORD msg2,SOCKADDR_IN source,int pnum);
void ReceivePosition(const char *msg,int msglen,int sender);
static void RelayVoice(VoiceMessage *voice);
#ifndef HK_WIN32_NATIVE
static void JoinNetAddGame(const char *msg,int msglen,SOCKADDR_IN source);
#endif
void DeletePlayer(int num,int reason);
void ReceiveChatMessage(char *msg,int msglen);


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
	
	infosock.Open(UdpLink::Open(netsettings.infosendport,1));

	SetLocalNum(0);
	GetLocalPlayer()->playstate=10;
#ifdef HKDS
	GetLocalPlayer()->playstate=1;
#endif

	ConsoleDisplayText("Started server");
	RedrawSplash(800,"Loading Textures");
	LoadTextures();
	if(!StartServerMap())   //a saved map chosen by loadmap or Server Setup (hugekilla.cpp)
	{
		RedrawSplash(800,"Loading Sky");
		LoadSky();
		RedrawSplash(800,"Creating Land");
		RandomLand(ldfs);
		CreateLand(ldfs);
	}

	consolevisible=0;
	strcpy(GetLocalPlayer()->name,netsettings.playername);
	curport=netsettings.datasendport;
	LockCursor(1);
#ifndef HKDS
	GPlayerEnterGame(GetLocalNum());
#endif
	LobbyHostStart();   //announced on the Internet list if net.ini says so
}

//closes sockets

void Disconnect()
{
	LockCursor(0);
	LobbyHostStop();
	infosock.Close();
	datasock.Close();
	for(int i=1;i<numplayers;i++)
		clientsock[i].Close();
	server=0;
	connected=0;
	numplayers=0;
	VoiceReset();
}

void CloseSocks()
{
	Disconnect();
	LobbyQuit();
	WSACleanup();
}

//sent to show a change in a player's state (like playing, dead, etc.)

void SendState(int srcnum,int destnum)
{
	SimpleBuf state=MakeSimple(STATECHANGE,GetPlayer(srcnum)->playstate,0);
	if(server)
	{
		clientsock[destnum].SendReliable(state.b,sizeof(state.b),0);
	}
	else
	{
		datasock.SendReliable(state.b,sizeof(state.b),0);
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

				char names[2+MAXPLAYERS*PLAYERNAMESIZE];
				ByteWriter w(names,sizeof(names));
				w.U8(PLAYERNAMES);
				w.U8(numplayers);
				for(int i=0;i<numplayers;i++)
					WritePlayerName(w,i);
				clientsock[pnum].SendReliable(names,w.Size(),RECEIVEDPLAYERNAMES);
				
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
							WirePutU32(&blockbuffer[1],numblocks);
							clientsock[pnum].SendReliable(blockbuffer,blockbufferpos,0);
							StartBlockBuffer();
						}
					}
				}
				WirePutU32(&blockbuffer[1],numblocks);
				clientsock[pnum].SendReliable(blockbuffer,blockbufferpos,RECEIVEDBLOCKINFO);
				StartBlockBuffer();
				ConsoleDisplayText("received landinfo acknowledgement, sending blockinfo");
				static ServerObjectSnapshot snapshot;
				static char sobjectbuf[SNAPSHOTSIZE];
				snapshot.type=SERVEROBJECTSNAPSHOT;
				CopyMemory(snapshot.obj,sobject,sizeof(snapshot.obj));
				int snapsize=WriteSnapshot(sobjectbuf,sizeof(sobjectbuf),&snapshot);
				clientsock[pnum].SendReliable(sobjectbuf,snapsize,0);
				SendServerVars(pnum); 
				SendRGD(pnum);
				
				
				
				break;
			}
		case RECEIVEDBLOCKINFO:
			{
				SimpleBuf joingame=MakeSimple(JOINGAME,pnum,0);
				int i;
				for(i=1;i<numplayers;i++)
				{
					clientsock[i].SendReliable(joingame.b,sizeof(joingame.b),0);
				}
				for(i=0;i<numplayers;i++)
				if(GetPlayer(i)->playstate==10)
				{
					joingame=MakeSimple(JOINGAME,i,0);
					clientsock[pnum].SendReliable(joingame.b,sizeof(joingame.b),0);
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
				SimpleBuf killed=MakeSimple(KILLED,pnum,msg2);
				if(server)
				{
					for(int i=1;i<numplayers;i++)
						if(i!=pnum)clientsock[i].SendReliable(killed.b,sizeof(killed.b),0);
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
	LobbyHostLoop(numplayers);
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
				char reqname[32];
				int trailer=ReadHelloName((char *)&msg,msglen,reqname,sizeof(reqname));
				PeerVersion peer;
				ReadVersionTrailer((char *)&msg+trailer,msglen-trailer,&peer);
				int addplayer=numplayers;

				char message[1000];
				strcpy(message,reqname);
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
					SimpleBuf enter=MakeSimple(ENTER,htons(clientsock[known].localport),known);
					infosock.dest=source;
					infosock.Send(enter.b,sizeof(enter.b));
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
				UdpLink *newlink=new UdpLink;
				int port=0;
				for(int p=baseport+1;p<=baseport+2*MAXPLAYERS&&!port;p++)
				{
					int used=0;
					for(int c=1;c<numplayers;c++)
						if(clientsock[c].link&&clientsock[c].localport==p) used=1;
					if(used) continue;
					if(newlink->Bind(htons(p))) port=p;
				}
				if(!port)
				{
					delete newlink;
					ConsoleDisplayText("no free port for a new client");
					RefuseClient(source,SERVERFULL,"The server has no free port.");
					break;
				}

				strcpy(GetPlayer(addplayer)->name,reqname);
				clientsock[addplayer].Open(newlink);
				clientsock[addplayer].dest=source;
				clientsock[addplayer].dest.sin_port=htons(port);   //the client binds the same port
				clientsock[addplayer].localport=port;
				clientsock[addplayer].requestaddr=source;
				clientsock[addplayer].filterip=1;
				clientsock[addplayer].ResetReliable();
				SimpleBuf enter=MakeSimple(ENTER,htons(port),addplayer);
				infosock.dest=source;
				infosock.Send(enter.b,sizeof(enter.b));
				infosock.Flush();
				/*SimpleBuf tmpmsg=MakeSimple(PORTOPENER,0,0);
				for(int m=0;m<30;m++)
				{
					clientsock[addplayer].Send(tmpmsg.b,sizeof(tmpmsg.b));
					clientsock[addplayer].Flush();
				}*/
				strcpy(message,GetPlayer(addplayer)->name);
				strcat(message," has connected");
				ConsoleDisplayText(message);
				GetPlayer(addplayer)->playstate=1;
				GetPlayer(addplayer)->kills=0;
				GetPlayer(addplayer)->deaths=0;
				char names[2+PLAYERNAMESIZE];
				ByteWriter w(names,sizeof(names));
				w.U8(PLAYERNAMES);
				w.U8(1);
				WritePlayerName(w,addplayer);

				for(int i=1;i<numplayers;i++)
				{
					clientsock[i].SendReliable(names,w.Size(),0);
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
				SimpleMessage simple;
				if(!ReadSimple(&msg,msglen,&simple)) break;
				//anyone can reach the info port: only info requests are answered there
				if(simple.type2==REQUESTINFO)
					HandleSimpleServer(simple.type2,simple.msg,simple.msg2,source,0);
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
						ReceivePosition((char *)&msg,clen,i);   //a client only moves itself
						break;
					}
				case VOICE:
					{
						VoiceMessage voice;
						if(!ReadVoice(&msg,clen,&voice)) break;
						voice.player=i;   //a client only speaks for itself
						if(GetPlayer(i)->playstate<10) break;
						RelayVoice(&voice);
						break;
					}
				case SIMPLE:
				{
					SimpleMessage simple;
					if(!ReadSimple(&msg,clen,&simple)) break;
					int ackcallback=simple.type2==0||simple.type2==RECEIVEDLANDINFO||
						simple.type2==RECEIVEDPLAYERNAMES||simple.type2==RECEIVEDJOINGAME||
						simple.type2==RECEIVEDBLOCKINFO;
					//"received" steps come from our own reliable acks, never from the client
					if(ackcallback&&!clientsock[i].fromack) break;
					HandleSimpleServer(simple.type2,simple.msg,simple.msg2,source,i);
					break;
				}
				case BLOCKS:
					{
						ReceiveBlockBuffer((char *)&msg,i,clen);
						
						break;
					}
				case CHATMESSAGE:
					{
						if(clen>=3) msg.buf[1]=i;   //sender: the client, whatever it wrote
						ReceiveChatMessage((char *)&msg,clen);
						break;
					}
				case FIREGUN:
				{
					FireGunDirection firegun;
					if(!ReadFireGun(&msg,clen,&firegun)) break;
					TryFireGun(i,firegun.pos,firegun.dir,firegun.reload);
					break;
				}
				case KILLEDPOSITION:
				{
					KilledPosition kp;
					if(!ReadKilledPosition(&msg,clen,&kp)) break;
					if(!ValidPlayer(kp.killer)) break;
					kp.killed=i;
					GetPlayer(i)->pos=kp.pos;
					ReceiveKillMessage(i,kp.killer);
					if(server)
					{
						char kpbuf[KILLEDPOSITIONSIZE];
						int kpsize=WriteKilledPosition(kpbuf,sizeof(kpbuf),&kp);
						for(int i3=1;i3<numplayers;i3++)
							if(i3!=i)clientsock[i3].SendReliable(kpbuf,kpsize,0);
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
	SimpleBuf simple=MakeSimple(type,msg,msg2);
	if(server)
		clientsock[num].Send(simple.b,sizeof(simple.b));
	else
		datasock.Send(simple.b,sizeof(simple.b));
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
					datasock.Open(UdpLink::Open(msg,0));
					datasock.dest=source;
					datasock.dest.sin_port=msg;
					datasock.filterip=1;   //from now on, only the server's IP
					SimpleBuf portack=MakeSimple(PORTACK,msg,0);
					datasock.SendReliable(portack.b,sizeof(portack.b),0);
					infosock.Close();
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
#ifdef HK_WIN32_NATIVE
					if(numlocallistaddr==MAXLOCALLIST) break;
					int i;
					for(i=0;i<numlocallistaddr;i++)
					{
						if(locallistaddr[i].sin_addr.s_addr==source.sin_addr.s_addr&&
							locallistaddr[i].sin_port==source.sin_port) break;
					}
					if(i!=numlocallistaddr) break;
					char infoname[100];
					int trailer=ReadHelloName((char *)&msg,msglen,infoname,sizeof(infoname));
					PeerVersion peer;
					ReadVersionTrailer((char *)&msg+trailer,msglen-trailer,&peer);
					locallistaddr[numlocallistaddr]=source;
					numlocallistaddr++;
#ifdef HK_WIN32_NATIVE
					SendDlgItemMessage(dlghwnd,IDC_LOCALGAMES,LB_ADDSTRING,0,(long)infoname);
#else
					JoinScreenAddGame(infoname,&peer);
#endif
					ConsoleDisplayText("received info");
					break;
#else
					JoinNetAddGame((char *)&msg,msglen,source);
					break;
#endif
				}
			case LANDINFO:
				{
					
					static LandInfo landinfo;
					if(!ReadLandInfo(&msg,msglen,&landinfo)) break;
					//bound everything the land builder uses as a count or a file name
					SanitizeLandInfo(&landinfo.ldfs,&landinfo.skysets);
					ApplyLand(&landinfo.ldfs,&landinfo.skysets);
					for(int i=0;i<numplayers;i++)
					{
						GetPlayer(i)->playstate=1;
					}
					ConsoleDisplayText("received landinfo");
					break;
				}
			case SIMPLE:
				{
					SimpleMessage simple;
					if(!ReadSimple(&msg,msglen,&simple)) break;
					HandleSimpleClient(simple.type2,simple.msg,simple.msg2,source);
					break;
				}
			case PLAYERPOSITION:
				{
					if(connected)
					{
						ReceivePosition((char *)&msg,msglen,-1);
					}
					break;
				}
			case VOICE:
				{
					//the server only relays the voices of players near us
					VoiceMessage voice;
					if(!connected||!ReadVoice(&msg,msglen,&voice)) break;
					if(!ValidPlayer(voice.player)||voice.player==GetLocalNum()) break;
					if(GetPlayer(voice.player)->playstate<10) break;
					VoiceReceive(voice.player,voice.seq,voice.data,voice.len);
					break;
				}
			case SERVERVARS:
				{
					ServerVarsMessage vars;
					if(ReadServerVars(&msg,msglen,&vars)) sv=vars.v;
					break;
				}
			case PLAYERNAMES:
				{
					PlayerNameEntry entries[MAXPLAYERS];
					int numnames=ReadPlayerNames(&msg,msglen,entries,MAXPLAYERS);
					for(int i=0;i<numnames;i++)
					{
						int playernum=entries[i].num;
						if(!ValidPlayer(playernum)) break;
						GetPlayer(playernum)->kills=entries[i].kills;
						GetPlayer(playernum)->deaths=entries[i].deaths;
						GetPlayer(playernum)->team=entries[i].team;
						strcpy(GetPlayer(playernum)->name,entries[i].name);
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
					RemoteGameDataMessage data;
					if(ReadRGD(&msg,msglen,&data)) rgd=data.d;
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
					ReceiveChatMessage((char *)&msg,msglen);
					break;		
				}
			case FIREGUN:
				{
					FireGunDirection firegun;
					if(ReadFireGun(&msg,msglen,&firegun)) HandleFireGun(&firegun);
					break;
				}
			case SERVEROBJECTSNAPSHOT:
				{
					static ServerObjectSnapshot snapshot;
					CopyMemory(snapshot.obj,sobject,sizeof(snapshot.obj));   //keeps the (unsent) timeouts
					if(ReadSnapshot(&msg,msglen,&snapshot))
						CopyMemory(sobject,snapshot.obj,sizeof(snapshot.obj));
					break;
				}
			case UPDATESERVEROBJECT:
				{
					UpdateServerObject update;
					if(ReadUpdateServerObject(&msg,msglen,&update)&&ValidSObj(update.num))
						ReceiveUpdateServerObject(&update);
					break;
				}
			case KILLEDPOSITION:
				{
					KilledPosition kp;
					if(!ReadKilledPosition(&msg,msglen,&kp)) break;
					if(!ValidPlayer(kp.killed)||!ValidPlayer(kp.killer)) break;
					GetPlayer(kp.killed)->pos=kp.pos;
					ReceiveKillMessage(kp.killed,kp.killer);
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
#ifndef HK_WIN32_NATIVE
	VoiceUpdate();   //before the loops, which flush what it sends
#endif
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
				infosock.Open(UdpLink::Open(netsettings.infosendport,1));
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
				SimpleBuf simple=MakeSimple(REQUESTINFO,0,0);
				infosock.Send(simple.b,sizeof(simple.b));
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
				SimpleBuf simple=MakeSimple(REQUESTINFO,0,0);
				infosock.Send(simple.b,sizeof(simple.b));
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

//---- server list for the join screen (src/ui/joinscreen.cpp draws it) ----

JoinGame joingames[MAXLOCALLIST];
static DWORD lastinfoquery;   //GetTickCount() of the last info request, for the ping

void JoinNetOpen()
{
	LoadNetSettings();
	if(!infosock.link)
		infosock.Open(UdpLink::Open(netsettings.infosendport,1));
}

void JoinNetClose()
{
	GetLocalPlayer()->playstate=0;
	infosock.Close();
}

void JoinNetClearList()
{
	numlocallistaddr=0;
}

int JoinGameCount()
{
	return numlocallistaddr;
}

JoinGame *JoinGameAt(int i)
{
	if(i<0||i>=numlocallistaddr) return 0;
	return &joingames[i];
}

static void SendInfoRequest(SOCKADDR_IN dest)
{
	infosock.dest=dest;
	SimpleBuf simple=MakeSimple(REQUESTINFO,0,0);
	infosock.Send(simple.b,sizeof(simple.b));
	infosock.Flush();
	lastinfoquery=GetTickCount();
}

void JoinNetSearchLAN()
{
	SendInfoRequest(MakeSockAddr(255,255,255,255,netsettings.infosendport));
}

//asks one server (IP or host name) for its info; it shows up in the list when it
//answers. port: its info port (host order), 0 for ours (net.ini infosendport).
//Returns 0 if the host name can't be resolved.
int JoinNetQuery(const char *host,int port)
{
	in_addr_t addr=inet_addr(host);
	if(addr==INADDR_NONE)
	{
		addrinfo hints,*res=0;
		memset(&hints,0,sizeof(hints));
		hints.ai_family=AF_INET;
		hints.ai_socktype=SOCK_DGRAM;
		if(getaddrinfo(host,0,&hints,&res)!=0||!res)
			return 0;
		addr=((sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
		freeaddrinfo(res);
	}
	SOCKADDR_IN dest=MakeSockAddr(0,0,0,0,port?htons((WORD)port):netsettings.infosendport);
	dest.sin_addr.s_addr=addr;
	SendInfoRequest(dest);
	return 1;
}

//index in the list of the server that answered from address:port (an Internet
//game being joined), -1 if none yet
int JoinNetFind(const char *address,int port)
{
	in_addr_t addr=inet_addr(address);
	for(int i=0;i<numlocallistaddr;i++)
		if(locallistaddr[i].sin_addr.s_addr==addr&&ntohs(locallistaddr[i].sin_port)==port)
			return i;
	return -1;
}

//sends the join request to a listed server. Returns 0 (and the reason) if it
//can't be joined.
int JoinNetConnect(int index,char *reason,int reasonsize)
{
	JoinGame *game=JoinGameAt(index);
	if(!game) return 0;
	if(!game->compatible)
	{
		snprintf(reason,reasonsize,"%s",game->reason);
		return 0;
	}
	datasock.ResetReliable();
	infosock.dest=locallistaddr[index];
	SendRequestEnter();
	connected=0;
	GetLocalPlayer()->playstate=1;
	GetLocalPlayer()->timeout=curtime+2000;
	ConsoleDisplayText("Sent connection request");
	return 1;
}

//debug aid (HK_MENU=joindemo): a fake entry in the server list
void JoinNetDebugAdd(const char *name,const char *addr,const char *version,int protocol,int ping,int players,int mode)
{
	if(numlocallistaddr>=MAXLOCALLIST) return;
	JoinGame &game=joingames[numlocallistaddr];
	memset(&locallistaddr[numlocallistaddr],0,sizeof(SOCKADDR_IN));
	numlocallistaddr++;
	memset(&game,0,sizeof(game));
	strncpy(game.name,name,sizeof(game.name)-1);
	strncpy(game.address,addr,sizeof(game.address)-1);
	strncpy(game.version,version,sizeof(game.version)-1);
	PeerVersion peer;
	memset(&peer,0,sizeof(peer));
	peer.present=protocol>1;
	peer.protocol=protocol;
	peer.layout=EDIFICE_NET_LAYOUT;
	strncpy(peer.version,version,sizeof(peer.version)-1);
	game.compatible=VersionCompatible(&peer,game.reason,sizeof(game.reason));
	game.ping=ping;
	game.players=players;
	game.maxplayers=MAXPLAYERS;
	game.gamemode=mode;
	game.capturelimit=5;
}

//INFO answer: name, version, then (servers since 0.3.0) players, max players,
//game mode, capture limit
static void JoinNetAddGame(const char *msg,int msglen,SOCKADDR_IN source)
{
	char name[100];
	int trailerpos=ReadHelloName(msg,msglen,name,sizeof(name));
	const char *trailer=msg+trailerpos;
	int trailerlen=msglen-trailerpos;
	PeerVersion peer;
	ReadVersionTrailer(trailer,trailerlen,&peer);

	int i;
	for(i=0;i<numlocallistaddr;i++)
		if(locallistaddr[i].sin_addr.s_addr==source.sin_addr.s_addr&&locallistaddr[i].sin_port==source.sin_port)
			break;
	if(i==MAXLOCALLIST) return;
	if(i==numlocallistaddr) numlocallistaddr++;
	locallistaddr[i]=source;
	JoinGame &game=joingames[i];
	memset(&game,0,sizeof(game));
	strncpy(game.name,name,sizeof(game.name)-1);
	strcpy(game.version,peer.version);
	strncpy(game.address,inet_ntoa(source.sin_addr),sizeof(game.address)-1);
	game.compatible=VersionCompatible(&peer,game.reason,sizeof(game.reason));
	game.ping=GetTickCount()-lastinfoquery;
	game.players=game.maxplayers=-1;
	if(peer.present)
	{
		int extra=11+(unsigned char)trailer[10];
		if(trailerlen>=extra+4)
		{
			game.players=(unsigned char)trailer[extra];
			game.maxplayers=(unsigned char)trailer[extra+1];
			game.gamemode=(unsigned char)trailer[extra+2];
			game.capturelimit=(unsigned char)trailer[extra+3];
		}
	}
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
		SimpleBuf deleteplayer=MakeSimple(DELETEPLAYER,num,reason);
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(deleteplayer.b,sizeof(deleteplayer.b),0);
		}
		
		clientsock[num].Close();
		
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
	VoiceReset();   //players are renumbered
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

//builds the land described by LandDefs and SkySettings (already sanitized:
//SanitizeLandInfo): what a client does on LANDINFO, and the server when it
//loads a map (src/game/mapfile.h). The blocks are cleared.

void ApplyLand(const LandDefs *defs,const SkySettings *sky)
{
	CopyMemory(GetDefs(),defs,sizeof(LandDefs));
	RedrawSplash(800,"Loading Textures");
	LoadTextures();
	RedrawSplash(800,"Loading Sky");
	CopyMemory(GetSkySettings(),sky,sizeof(SkySettings));
	//the sky's ambient sound, as LoadSky does on the server
	if(strlen(GetSkySettings()->sound))
	{
		char soundname[200];
		snprintf(soundname,sizeof(soundname),"sound\\%s",GetSkySettings()->sound);
		LoadSound(soundname,0,0);
		PlaySound(0,1);
	}
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
	RedrawSplash(800,"Creating Land");
	CreateLand(*GetDefs());
}

void SendLandInfo(int num)
{
	static LandInfo landinfo;
	static char buf[LANDINFOSIZE];
	landinfo.type=LANDINFO;
	CopyMemory(&landinfo.ldfs,GetDefs(),sizeof(landinfo.ldfs));
	CopyMemory(&landinfo.skysets,GetSkySettings(),sizeof(SkySettings));
	int size=WriteLandInfo(buf,sizeof(buf),&landinfo);
	if(num>0)
	{
		GetPlayer(num)->playstate=1;
		clientsock[num].SendReliable(buf,size,RECEIVEDLANDINFO);
	}
	else
	{
		for(int i=1;i<numplayers;i++)
		{
			GetPlayer(i)->playstate=1;
			clientsock[i].SendReliable(buf,size,RECEIVEDLANDINFO);
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
void ReceivePosition(const char *msg,int msglen,int sender)
{
	PlayerPosition position;
	if(!ReadPosition(msg,msglen,&position)) return;
	PlayerPosition *pos=&position;
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
	char buf[POSITIONSIZE];
	int size=WritePosition(buf,sizeof(buf),&pos);
	if(!server)
	{
		datasock.Send(buf,size);
	}
	else
	{
		clientsock[destnum].Send(buf,size);
	}
}

//---- voice chat (src/audio/voice.cpp): VOICE goes only to players in range ----

static_assert((int)VOICE_WORLDSIZE==LANDGRIDSIZE,"voice.h: VOICE_WORLDSIZE must be LANDGRIDSIZE");

static int VoiceNear(int a,int b)
{
	Vector pa=GetPlayer(a)->pos,pb=GetPlayer(b)->pos;
	return VoiceDistance(pa.x,pa.y,pa.z,pb.x,pb.y,pb.z)<VOICE_RANGE;
}

//server: sends a voice frame of voice->player to the players near it, and plays
//it here if the host is one of them
static void RelayVoice(VoiceMessage *voice)
{
	int speaker=voice->player;
	if(!ValidPlayer(speaker)||speaker>=numplayers) return;
	char buf[VOICEMSGSIZE];
	int size=WriteVoice(buf,sizeof(buf),voice);
	if(!size) return;
	for(int i=1;i<numplayers;i++)
		if(i!=speaker&&GetPlayer(i)->playstate>=10&&VoiceNear(speaker,i))
			clientsock[i].Send(buf,size);
	if(speaker!=0&&GetPlayer(0)->playstate>=10&&VoiceNear(speaker,0))
		VoiceReceive(speaker,voice->seq,voice->data,voice->len);
}

//sends one encoded frame of the local player (voice.cpp)
void SendVoiceFrame(int seq,const unsigned char *data,int len)
{
	if(len<1||len>VOICEMAXFRAME) return;
	if(GetLocalPlayer()->playstate<10) return;
	VoiceMessage voice;
	voice.player=GetLocalNum();
	voice.seq=(uint16_t)seq;
	voice.len=(uint16_t)len;
	memcpy(voice.data,data,len);
	if(server)
	{
		RelayVoice(&voice);
		return;
	}
	if(!connected) return;
	char buf[VOICEMSGSIZE];
	int size=WriteVoice(buf,sizeof(buf),&voice);
	if(size) datasock.Send(buf,size);
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
	WirePutU32(&blockbuffer[1],numblocks);
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
						
	int numblocks=ReadBlocksCount(buf,len);   //bounded by len (netmsg.h)
	int pos=BLOCKSHEADERSIZE+numblocks*BLOCKEDITSIZE;
	int i;
	for(i=0;i<numblocks;i++)
	{
		BlockEdit b;
		ReadBlockEdit(buf,i,&b);
		if(!ValidBlockEdit(&b)) continue;   //type or texture the renderer can't index
		if(b.del)
		{
			DeleteBlock(b.x,b.y,b.z);
		}
		else
		{
			AddBlock(b.x,b.y,b.z,b.type,b.top,b.side,b.bottom);
		}
	}
	if(server)
	{
		WirePutU32(&buf[1],numblocks);   //relay the blocks actually received
		for(i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(buf,pos,0);
		}
	}
}

int SendMultMessage(const void *msg,int len,int player=-1)
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


//CHATMESSAGE: type, int8 dest (CMSG_*), uint8 sender, uint16 length, text
//msglen: size of the message, type included
void ReceiveChatMessage(char *msg,int msglen)
{
	ChatMessage chat;
	if(!ReadChat(msg,msglen,&chat)) return;
	char dest=chat.dest;
	int sender=chat.sender;
	int len=chat.len;
	//score, objective and info lines come from the server only
	if(server&&sender&&dest!=CMSG_BROADCAST&&dest!=CMSG_TEAM) return;
	//if(server==1&&(dest!=CMSG_BROADCAST&&dest!=CMSG_TEAM)) return;
	


	char *chatmessage=chat.text;
#ifdef HK_WIN32_NATIVE
	char message[1000];
#endif

	if(dest==CMSG_BROADCAST||dest==CMSG_TEAM)
	{
#ifdef HK_WIN32_NATIVE
		snprintf(message,sizeof(message),"%.31s: %s",ValidPlayer(sender)?GetPlayer(sender)->name:"?",chatmessage);
		ConsoleDisplayText(message);
#else
		ChatAddLine(ValidPlayer(sender)?GetPlayer(sender)->name:"?",ValidPlayer(sender)?GetPlayer(sender)->team:0,
			dest==CMSG_TEAM,chatmessage);
#endif
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
	if(sender)  //this came from a player (called from serverloop)
	{
		if(dest==CMSG_BROADCAST)
			SendMultMessage(msg,len+5,-1);
		if(dest==CMSG_TEAM&&ValidPlayer(sender))
			SendMultMessage(msg,len+5,GetPlayer(sender)->team+TARGTEAM);
	}

}

void SendChatMessage(char *text,int dest,int player)
{
	static char chat[5+60000];
	int len=strlen(text);
	if(len>60000) len=60000;
	ByteWriter w(chat,sizeof(chat));
	w.U8(CHATMESSAGE);
	w.U8(dest);
	w.U8(0);   //sender: the server fills it in
	w.U16(len);
	w.Bytes(text,len);
	if(server)
	{
		if(SendMultMessage(chat,w.Size(),player))
			ReceiveChatMessage(chat,w.Size());
	}
	else
	{
		datasock.SendReliable(chat,w.Size(),0);
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

void HandleFireGun(FireGunDirection *firegun)
{
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
	HandleFireGun(&firegun);
	char buf[FIREGUNSIZE];
	int size=WriteFireGun(buf,sizeof(buf),&firegun);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
		{
			if(i!=num) clientsock[i].Send(buf,size);
		}

	}
	else
	{
		datasock.Send(buf,size);
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
	SimpleBuf killed=MakeSimple(KILLED,GetLocalNum(),killer);
	ReceiveKillMessage(GetLocalNum(),killer);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
			clientsock[i].SendReliable(killed.b,sizeof(killed.b),0);
	}
	else
	{
		datasock.SendReliable(killed.b,sizeof(killed.b),0);
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
	ServerVarsMessage vars;
	vars.type=SERVERVARS;
	vars.v=sv;
	char buf[SERVERVARSSIZE];
	int size=WriteServerVars(buf,sizeof(buf),&vars);
	if(server)
	{
		if(num)
		{
			clientsock[num].SendReliable(buf,size,0);
		}
		else
		{
			for(int i=0;i<numplayers;i++)
			{
				clientsock[i].SendReliable(buf,size,0);
			}
		}
	}
	else
	{
		datasock.SendReliable(buf,size,0);
	}
}

void SendRGD(int player)
{
	RemoteGameDataMessage data;
	data.type=REMOTEGAMEDATA;
	data.d=rgd;
	char buf[RGDSIZE];
	int size=WriteRGD(buf,sizeof(buf),&data);
	SendMultMessage(buf,size);
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
	SimpleBuf changeteam=MakeSimple(CHANGETEAM,player,team);
	if(server)
	{
		ReceiveChangeTeam(player,team);
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(changeteam.b,sizeof(changeteam.b),0);
		}
	}
	else
	{
		datasock.SendReliable(changeteam.b,sizeof(changeteam.b),0);
	}
}

//updates the position of an server controlled object (sent reliably so it only
//needs to be done once.

void SendUpdateServerObject(int num)
{
	if(!ValidSObj(num)) return;
	UpdateServerObject update;
	update.type=UPDATESERVEROBJECT;
	update.num=num;
	update.updated=sobject[num];   //timeout is not sent
	char buf[UPDATESERVEROBJECTSIZE];
	int size=WriteUpdateServerObject(buf,sizeof(buf),&update);
	if(server)
	{
		for(int i=1;i<numplayers;i++)
		{
			clientsock[i].SendReliable(buf,size,0);
		}
	}
}

void ReceiveUpdateServerObject(UpdateServerObject *update)
{
	if(!ValidSObj(update->num)) return;
	int timeout=sobject[update->num].timeout;   //not sent: stays as it was
	sobject[update->num]=update->updated;
	sobject[update->num].timeout=timeout;
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
		SimpleBuf claim=MakeSimple(CLAIMOBJECT,0,0);
		datasock.SendReliable(claim.b,sizeof(claim.b),0);
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
	SimpleBuf scorereset=MakeSimple(SCORERESET,0,0);
	ConsoleDisplayText("Scores reset");
	for(int i=0;i<numplayers;i++)
	{
		GetPlayer(i)->kills=0;
		GetPlayer(i)->deaths=0;
	}
	if(server)
	for(int i=1;i<numplayers;i++)
	{
		clientsock[i].SendReliable(scorereset.b,sizeof(scorereset.b),0);
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
			SimpleBuf crd;
			if(credit>=0)
				crd=MakeSimple(CREDITS,credit,GetPlayer(i)->credits[credit]);
			else
				crd=MakeSimple(CREDITS,credit,0);
			if(i>0) 
				clientsock[i].SendReliable(crd.b,sizeof(crd.b),0);
		}
	}
}