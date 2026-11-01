// Netplay through GameNetworkingSockets (roadmap M7b): Sock over GnsLink, as the game
// uses them, against a real server started with HK_AUTOSTART=1 HK_TRANSPORT=gns.
// Two clients join the way tests/net_join.py and net_voice.py do over UDP: INFO,
// REQUESTENTER/ENTER, PORTACK, player names, LANDINFO and the blocks (reliable
// transfers), JOINGAME. Then they play next to the host: positions relayed, the voice
// frames of one relayed to the other, the host's own voice (HK_VOICE_TEST_TONE +
// HK_VOICE_AUTOTALK) sent to both. Every connection must be encrypted.
// A client has one connection per server address: the second one names the server
// with another info port (the connection goes to gnsport all the same).
// Run by tests/run_gns_netplay_test.sh.

#include "win32compat.h"
#include <chrono>
#include <thread>
#include <steam/steamnetworkingsockets.h>
#include "multiplay.h"
#include "land.h"
#include "multiplaystruct.h"
#include "netmsg.h"
#include "gnslink.h"

#ifndef _WIN32
DWORD GetTickCount()   //win32compat.cpp isn't linked
{
	using namespace std::chrono;
	return (DWORD)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
SOCKET WSASocket(int af,int type,int protocol,void *,int,DWORD)   //UdpLink: unused here
{
	return socket(af,type,protocol);
}
#endif

static const int INFOPORT=27020;
static int gnsport=27021;
static int failures;

static void Check(int ok,const char *what)
{
	printf("%s %s\n",ok?"ok  ":"FAIL",what);
	fflush(stdout);
	if(!ok) failures++;
}

static void Wait(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static int Encrypted(unsigned int conn)
{
	SteamNetConnectionInfo_t info;
	if(!SteamNetworkingSockets()->GetConnectionInfo(conn,&info)) return 0;
	return info.m_eState==k_ESteamNetworkingConnectionState_Connected&&
		!(info.m_nFlags&k_nSteamNetworkConnectionInfoFlags_Unencrypted);
}

static int Quantize(float v)
{
	return (int)(v/LANDGRIDSIZE*65536)&0xffff;
}

struct Client
{
	const char *name;
	SOCKADDR_IN server;
	GnsLink *info;
	Sock *infosock,*datasock;
	GnsLink *data;
	int slot,port;
	int names,landinfo,blocks,joined;
	int hostknown;
	float hostpos[3];
	int sawpositionof[MAXPLAYERS];
	int voices[MAXPLAYERS];    //VOICE frames received, per player
	int intact[MAXPLAYERS];    //...whose content was as sent (seq 100+k: 40 bytes 0x40+k)
};

static void InitClient(Client &c,const char *name,int infoport)
{
	memset(&c,0,sizeof(c));
	c.name=name;
	c.server.sin_family=AF_INET;
	c.server.sin_addr.s_addr=inet_addr("127.0.0.1");
	c.server.sin_port=htons(infoport);
	c.infosock=new Sock;
	c.datasock=new Sock;
	c.slot=-1;
}

static void SendSimpleTo(Sock *s,char type,DWORD msg,DWORD msg2,int reliable)
{
	SimpleBuf b;
	WriteSimple(b.b,type,msg,msg2);
	if(reliable) s->SendReliable(b.b,sizeof(b.b),0);
	else s->Send(b.b,sizeof(b.b));
}

//asks for INFO, then joins: 1 once ENTER came
static int Enter(Client &c)
{
	c.info=GnsLink::OpenInfo(0,0,gnsport);
	if(!c.info) return 0;
	c.infosock->Open(c.info);
	c.info->Connect(&c.server);
	c.infosock->dest=c.server;
	static Message msg;
	SOCKADDR_IN source;
	char trailer[300];
	int trailerlen=-1;
	DWORD start=GetTickCount(),last=0;
	while(GetTickCount()-start<20000&&c.slot<0)
	{
		if(GetTickCount()-last>500)
		{
			last=GetTickCount();
			if(trailerlen<0)
				SendSimpleTo(c.infosock,REQUESTINFO,0,0,0);
			else
			{
				char req[2+32+300];
				int nl=strlen(c.name);
				req[0]=REQUESTENTER;
				req[1]=nl;
				memcpy(&req[2],c.name,nl+1);
				memcpy(&req[2+nl+1],trailer,trailerlen);
				c.infosock->Send(req,2+nl+1+trailerlen);
			}
			c.infosock->Flush();
		}
		int len;
		while((len=c.infosock->Receive(&msg,&source)))
		{
			const unsigned char *m=(const unsigned char *)&msg;
			if(m[0]==INFO&&len>2&&trailerlen<0)
			{
				int at=2+m[1]+1;
				if(len>=at+11&&len>=at+11+m[at+10])
				{
					trailerlen=11+m[at+10];
					memcpy(trailer,&m[at],trailerlen);
					printf("%s: INFO from \"%.*s\" through GameNetworkingSockets\n",c.name,m[1],(const char *)&m[2]);
					last=0;
				}
			}
			SimpleMessage simple;
			if(m[0]==SIMPLE&&ReadSimple(&msg,len,&simple)&&simple.type2==ENTER)
			{
				c.port=simple.msg;
				c.slot=simple.msg2;
			}
		}
		Wait(5);
	}
	return c.slot>=0;
}

static void Handle(Client &c,const Message &msg,int len)
{
	const unsigned char *m=(const unsigned char *)&msg;
	SimpleMessage simple;
	switch(m[0])
	{
	case SIMPLE:
		if(!ReadSimple(&msg,len,&simple)||c.datasock->fromack) break;
		if(simple.type2==JOINGAME&&(int)simple.msg==c.slot) c.joined=1;
		if(simple.type2==PING) SendSimpleTo(c.datasock,PINGREPLY,simple.msg,c.datasock->packetsreceived,0);
		break;
	case PLAYERNAMES:
		c.names++;
		break;
	case LANDINFO:
		if(len>=LANDINFOSIZE) c.landinfo++;
		break;
	case BLOCKS:
		c.blocks++;
		break;
	case PLAYERPOSITION:
		if(len<16||m[1]>=MAXPLAYERS) break;
		c.sawpositionof[m[1]]=1;
		if(m[1]==0)
		{
			c.hostknown=1;
			for(int i=0;i<3;i++)
				c.hostpos[i]=WireGetU16(&m[7+2*i])*(float)LANDGRIDSIZE/65536;
		}
		break;
	case VOICE:
		if(len<6||m[1]>=MAXPLAYERS) break;
		c.voices[m[1]]++;
		{
			int seq=WireGetU16(&m[2]),n=WireGetU16(&m[4]);
			int k=seq-100,ok=n==40&&len>=6+n&&k>=0&&k<64;
			for(int i=0;ok&&i<n;i++)
				if(m[6+i]!=0x40+k) ok=0;
			if(ok) c.intact[m[1]]++;
		}
		break;
	}
}

static void Pump(Client &c)
{
	static Message msg;
	SOCKADDR_IN source;
	c.datasock->CheckReliableResend();
	int len;
	while((len=c.datasock->Receive(&msg,&source)))
		Handle(c,msg,len);
	c.datasock->Flush();
}

//after ENTER: the data channel, then everything up to JOINGAME
static int Join(Client &c)
{
	c.data=GnsLink::OpenData(&c.server);
	if(!c.data) return 0;
	c.datasock->Open(c.data);
	c.datasock->dest=c.server;
	c.datasock->dest.sin_port=c.port;
	c.datasock->filterip=1;
	SendSimpleTo(c.datasock,PORTACK,c.port,0,1);
	c.infosock->Close();   //as the game does: the connection goes on with the data link
	DWORD start=GetTickCount();
	while(GetTickCount()-start<20000&&!c.joined)
	{
		Pump(c);
		Wait(5);
	}
	return c.joined;
}

static void Position(Client &c,float x,float y,float z)
{
	unsigned char p[16];
	memset(p,0,sizeof(p));
	p[0]=PLAYERPOSITION;
	p[4]=10;   //state: playing
	p[5]=p[6]=128;
	WirePutU16(&p[7],Quantize(x));
	WirePutU16(&p[9],Quantize(y));
	WirePutU16(&p[11],Quantize(z));
	c.datasock->Send(p,sizeof(p));
}

static void Voice(Client &c,int seq)
{
	unsigned char v[6+40];
	v[0]=VOICE;
	v[1]=0;   //the server puts the sender's slot
	WirePutU16(&v[2],seq);
	WirePutU16(&v[4],40);
	memset(&v[6],0x40+seq-100,40);
	c.datasock->Send(v,sizeof(v));
}

int main(int argc,char **argv)
{
	if(argc>1) gnsport=atoi(argv[1]);
	static Client a,b;
	InitClient(a,"Alice",INFOPORT);
	InitClient(b,"Bob",INFOPORT+1000);

	Check(Enter(a),"Alice: INFO and ENTER on the info channel");
	if(failures) return 1;
	Check(Join(a),"Alice: PORTACK, names, LANDINFO, blocks, JOINGAME on the data channel");
	Check(a.names>0&&a.landinfo==1,"Alice: got the player names and the land (LANDINFO)");
	Check(Encrypted(a.data->Connection()),"Alice: connection encrypted");
	Check(Enter(b),"Bob: INFO and ENTER (second connection)");
	if(failures) return 1;
	Check(Join(b)&&b.landinfo==1,"Bob: joined, with the land");
	Check(Encrypted(b.data->Connection()),"Bob: connection encrypted");
	Check(a.slot!=b.slot,"two slots");
	if(failures) return 1;

	//play: next to the host, both send their position, Alice talks
	for(DWORD start=GetTickCount();GetTickCount()-start<5000&&!a.hostknown;)
	{
		Pump(a);
		Pump(b);
		Wait(10);
	}
	Check(a.hostknown,"Alice gets the host's position");
	memset(a.voices,0,sizeof(a.voices));
	memset(b.voices,0,sizeof(b.voices));
	memset(b.intact,0,sizeof(b.intact));
	for(int k=0;k<60;k++)
	{
		float *h=a.hostpos;
		Position(a,h[0]+2,h[1],h[2]);
		Position(b,h[0]+3,h[1],h[2]);
		if(k>=20&&k<50) Voice(a,100+k-20);
		Pump(a);
		Pump(b);
		Wait(30);
	}
	for(int k=0;k<30;k++)
	{
		Pump(a);
		Pump(b);
		Wait(10);
	}
	Check(b.sawpositionof[a.slot],"Bob gets Alice's position");
	int fromalice=b.voices[a.slot],fromhost=b.voices[0],fromhosta=a.voices[0];
	char text[200];
	snprintf(text,sizeof(text),"Bob hears Alice: %d/30 frames, %d intact",fromalice,b.intact[a.slot]);
	Check(fromalice>=25&&b.intact[a.slot]==fromalice,text);
	snprintf(text,sizeof(text),"Alice and Bob hear the host: %d and %d frames",fromhosta,fromhost);
	Check(fromhosta>10&&fromhost>10,text);

	a.datasock->Close();
	b.datasock->Close();
	GnsLink::Shutdown();
	printf(failures?"SOME CHECKS FAILED\n":"ALL OK\n");
	return failures?1:0;
}
