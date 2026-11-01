// P2P through GameNetworkingSockets and edifice-lobby (roadmap M7d), without the
// game: GnsLink as the game uses it, two processes, each with its own lobby session.
//   gns_p2p_test <lobby URL> host <file> [relay]
//       listens for P2P connections, writes its identity to <file>, answers each
//       datagram of a client with "pong ..." (info channel) and "echo ..." (data
//       channel), until a client says "bye" (60 s at most)
//   gns_p2p_test <lobby URL> join <host identity> [relay]
//       connects P2P to the host (signaling through the lobby), info then data
//       channel; the connection must be encrypted, and relayed by the lobby's TURN
//       server with "relay" (both sides then only offer relay candidates), direct
//       without
// Run by tests/net_p2p.py, after the games themselves.

#include "win32compat.h"
#include <chrono>
#include <string>
#include <thread>
#include <steam/steamnetworkingsockets.h>
#include "gnslink.h"
#include "lobby.h"
#include "signaling.h"

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

//a session from the lobby, made into what GnsLink::StartP2P takes
static std::string stun,turn,users,passes;
static char relayhost[254];
static LobbySession session;

static int Session(const char *url,int relay,GnsP2PConfig *config)
{
	if(!LobbySetURL(url,"Edifice-p2p-test")) return 0;
	int r=LobbySend("POST","/v1/sessions",0,0);
	int status=0;
	std::string reply;
	for(int i=0;i<1000&&!LobbyPoll(r,&status,&reply);i++) Wait(10);
	if(status!=201||!LobbyParseSession(reply.data(),(int)reply.size(),&session)) return 0;
	int port=0;
	if(!SignalParseAddress(session.signaling,relayhost,sizeof(relayhost),&port)&&!session.signalurl[0]) return 0;
	char server[300];
	for(int i=0;i<session.numstun;i++)
		if(SignalIceServer(session.stun[i],"stun",server,sizeof(server))) stun+=(stun.empty()?"":",")+std::string(server);
	for(int i=0;i<session.numturn;i++)
		if(SignalIceServer(session.turn[i].url,"turn",server,sizeof(server)))
		{
			turn+=(turn.empty()?"":",")+std::string(server);
			users+=(users.empty()?"":",")+std::string(session.turn[i].username);
			passes+=(passes.empty()?"":",")+std::string(session.turn[i].credential);
		}
	GnsP2PConfig c={session.identity,session.token,relayhost,port,stun.c_str(),turn.c_str(),users.c_str(),passes.c_str(),relay,
		session.signalurl};
	*config=c;
	printf("session %s, signaling %s / %s:%d, STUN %s, TURN %s\n",session.identity,session.signalurl,relayhost,port,
		stun.c_str(),turn.c_str());
	return 1;
}

static int Flags(unsigned int conn,int *state)
{
	SteamNetConnectionInfo_t info;
	*state=0;
	if(!SteamNetworkingSockets()->GetConnectionInfo(conn,&info)) return -1;
	*state=info.m_eState;
	return info.m_nFlags;
}

static int Host(const char *file)
{
	GnsLink *info=GnsLink::OpenInfo(0,1,0,0);
	Check(info!=0,"host: info link");
	if(!info) return 1;
	FILE *f=fopen(file,"w");
	if(!f) return 1;
	fprintf(f,"%s\n",session.identity);
	fclose(f);
	GnsLink *data=0;
	SOCKADDR_IN client,from;
	memset(&client,0,sizeof(client));
	char buf[2000];
	int done=0,pings=0;
	DWORD start=GetTickCount();
	while(!done&&GetTickCount()-start<60000)
	{
		int n;
		while((n=info->ReceiveFrom(buf,sizeof(buf)-1,&from))>0)
		{
			buf[n]=0;
			if(!pings++)
			{
				unsigned int ip=ntohl(from.sin_addr.s_addr);
				printf("host: first datagram from %u.%u.%u.%u:%u\n",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,ntohs(from.sin_port));
				Check((ip>>16)==0,"host: the P2P client is named by a made-up 0.0.x.y address");
			}
			std::string reply=std::string("pong ")+buf;
			info->SendTo(reply.data(),(int)reply.size(),&from);
			if(!data)
			{
				client=from;
				data=GnsLink::OpenData(&client);
			}
		}
		while(data&&(n=data->ReceiveFrom(buf,sizeof(buf)-1,&from))>0)
		{
			buf[n]=0;
			if(!strcmp(buf,"bye")) done=1;
			std::string reply=std::string("echo ")+buf;
			data->SendTo(reply.data(),(int)reply.size(),&client);
		}
		Wait(5);
	}
	Check(done,"host: the client came and said bye");
	Wait(300);   //lets the last echo go out
	delete data;
	delete info;
	return 0;
}

static int Join(const char *hostidentity,int relay)
{
	GnsLink *info=GnsLink::OpenInfo(0,0,0,0);
	Check(info!=0,"join: info link");
	if(!info) return 1;
	SOCKADDR_IN host;
	memset(&host,0,sizeof(host));
	host.sin_family=AF_INET;
	host.sin_addr.s_addr=inet_addr("127.0.0.1");
	host.sin_port=htons(27020);
	Check(info->ConnectP2P(&host,hostidentity),"join: P2P connection opening");
	char buf[2000];
	SOCKADDR_IN from;
	int pong=0;
	DWORD start=GetTickCount(),last=0;
	while(!pong&&GetTickCount()-start<30000)
	{
		if(GetTickCount()-last>500)
		{
			info->SendTo("ping",4,&host);   //queued until connected
			last=GetTickCount();
		}
		int n;
		while((n=info->ReceiveFrom(buf,sizeof(buf)-1,&from))>0)
		{
			buf[n]=0;
			if(!strcmp(buf,"pong ping")&&from.sin_addr.s_addr==host.sin_addr.s_addr&&from.sin_port==host.sin_port) pong=1;
		}
		Wait(5);
	}
	char text[200];
	snprintf(text,sizeof(text),"join: the host answers on the info channel (%u ms)",(unsigned)(GetTickCount()-start));
	Check(pong,text);
	if(!pong) return 1;
	GnsLink *data=GnsLink::OpenData(&host);
	Check(data!=0,"join: data link on the same connection");
	if(!data) return 1;
	int state,flags=Flags(data->Connection(),&state);
	Check(state==k_ESteamNetworkingConnectionState_Connected,"join: connected");
	Check(flags>=0&&!(flags&k_nSteamNetworkConnectionInfoFlags_Unencrypted),"join: encrypted");
	if(relay) Check(flags>=0&&(flags&k_nSteamNetworkConnectionInfoFlags_Relayed),"join: relayed through the lobby's TURN server");
	else Check(flags>=0&&!(flags&k_nSteamNetworkConnectionInfoFlags_Relayed),"join: direct (ICE, not relayed)");
	int echoes=0;
	for(int k=0;k<20;k++)
	{
		snprintf(text,sizeof(text),"data %d",k);
		data->SendTo(text,(int)strlen(text),&host);
		Wait(20);
		int n;
		while((n=data->ReceiveFrom(buf,sizeof(buf)-1,&from))>0)
		{
			buf[n]=0;
			echoes+=!strncmp(buf,"echo data ",10);
		}
	}
	for(int k=0;k<50&&echoes<20;k++)
	{
		int n;
		while((n=data->ReceiveFrom(buf,sizeof(buf)-1,&from))>0)
		{
			buf[n]=0;
			echoes+=!strncmp(buf,"echo data ",10);
		}
		Wait(20);
	}
	snprintf(text,sizeof(text),"join: %d/20 datagrams echoed on the data channel",echoes);
	Check(echoes>=18,text);
	flags=Flags(data->Connection(),&state);
	if(relay) Check(flags>=0&&(flags&k_nSteamNetworkConnectionInfoFlags_Relayed),"join: still relayed");
	data->SendTo("bye",3,&host);
	Wait(500);
	delete data;
	delete info;
	return 0;
}

int main(int argc,char **argv)
{
	if(argc<4||(strcmp(argv[2],"host")&&strcmp(argv[2],"join")))
	{
		fprintf(stderr,"usage: %s <lobby URL> host <identity file> [relay] | join <host identity> [relay]\n",argv[0]);
		return 2;
	}
	int relay=argc>4&&!strcmp(argv[4],"relay");
	GnsP2PConfig config;
	Check(Session(argv[1],relay,&config),"a lobby session with a signaling relay");
	if(failures) return 1;
	Check(GnsLink::StartP2P(&config),"P2P started");
	if(failures) return 1;
	if(!strcmp(argv[2],"host")) Host(argv[3]);
	else Join(argv[3],relay);
	GnsLink::Shutdown();
	LobbyShutdown(0);
	printf(failures?"SOME CHECKS FAILED\n":"ALL OK\n");
	return failures?1:0;
}
