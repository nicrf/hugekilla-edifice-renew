//Which DatagramLink each Sock gets: UDP, or GameNetworkingSockets (see netlink.h)

#include "platform.h"
#include "cfgfile.h"
#include "netlink.h"
#include "udplink.h"
#ifdef EDIFICE_GNS
#include <string>
#include "gnslink.h"
#include "lobby.h"
#include "signaling.h"

#define SIGNAL_DEFAULTPORT 10000   //edifice-lobby's -signal default

//transport=gns in net.ini, HK_TRANSPORT overrides it; port: gnsport, else datasendport
static int UseGns(int *port)
{
	CfgFile netsetfile;
	netsetfile.Load(UserFile("net.ini"));
	const char *transport=getenv("HK_TRANSPORT");
	if(!transport) transport=netsetfile.GetValues("transport");
	*port=netsetfile.GetValuei("gnsport");
	if(*port<=0||*port>65535) *port=netsetfile.GetValuei("datasendport");
	return transport&&!strncmp(transport,"gns",3);
}
#endif

DatagramLink *NetOpenInfoLink(WORD port,int host,int p2p)
{
	UdpLink *udp=UdpLink::Open(port,1);
#ifdef EDIFICE_GNS
	int gnsport;
	int ip=UseGns(&gnsport);
	if(ip||(p2p&&NetP2PMode()))
	{
		GnsLink *link=GnsLink::OpenInfo(udp,host,gnsport,ip);
		if(link) return link;
		fprintf(stderr,"gns: unavailable, plain UDP only\n");
	}
#endif
	return udp;
}

DatagramLink *NetOpenDataLink(WORD port,const SOCKADDR_IN *server)
{
#ifdef EDIFICE_GNS
	GnsLink *link=GnsLink::OpenData(server);
	if(link) return link;
#endif
	return UdpLink::Open(port,0);
}

DatagramLink *NetOpenPeerLink(const SOCKADDR_IN *source)
{
#ifdef EDIFICE_GNS
	return GnsLink::OpenData(source);
#else
	return 0;
#endif
}

int NetSourceIsConnection(const SOCKADDR_IN *source)
{
#ifdef EDIFICE_GNS
	return GnsLink::IsConnection(source);
#else
	(void)source;
	return 0;
#endif
}

void NetQueryHost(const SOCKADDR_IN *dest)
{
#ifdef EDIFICE_GNS
	GnsLink *info=GnsLink::CurrentInfo();
	if(info) info->Connect(dest);
#endif
}

void NetLinkShutdown()
{
#ifdef EDIFICE_GNS
	GnsLink::Shutdown();
#endif
}

int NetLinkIsGns(DatagramLink *link)
{
#ifdef EDIFICE_GNS
	return link&&dynamic_cast<GnsLink *>(link)!=0;
#else
	return 0;
#endif
}

int NetP2PMode()
{
#ifdef EDIFICE_GNS
	CfgFile netsetfile;
	netsetfile.Load(UserFile("net.ini"));
	const char *mode=getenv("HK_P2P");
	if(!mode) mode=netsetfile.GetValues("p2p");
	if(!mode||!mode[0]) return 1;
	if(!strncmp(mode,"relay",5)) return 2;
	return strncmp(mode,"0",1)&&strncmp(mode,"off",3)?1:0;
#else
	return 0;
#endif
}

#ifdef EDIFICE_GNS
//the host of an http(s):// URL (the lobby's, already checked by LobbySetURL)
static int UrlHost(const char *url,char *host,int size)
{
	const char *p=strstr(url,"://");
	if(!p) return 0;
	p+=3;
	int n=0;
	while(p[n]&&p[n]!='/'&&p[n]!=':'&&p[n]!='?') n++;
	if(n<1||n>=size) return 0;
	memcpy(host,p,n);
	host[n]=0;
	return 1;
}

//a comma-separated list of what SignalIceServer makes of each url
static void Append(std::string &list,const char *item)
{
	if(!list.empty()) list+=',';
	list+=item;
}
#endif

int NetP2PStart(const LobbySession *s,const char *lobbyurl)
{
#ifdef EDIFICE_GNS
	int mode=NetP2PMode();
	if(!mode) return 0;
	char host[254]="";
	int port=SIGNAL_DEFAULTPORT;
	if((s->signaling[0]?!SignalParseAddress(s->signaling,host,sizeof(host),&port):!UrlHost(lobbyurl,host,sizeof(host)))&&
		!s->signalurl[0])
	{
		fprintf(stderr,"p2p: no signaling relay address\n");
		return 0;
	}
	if(!SignalValidIdentity(s->identity))
	{
		fprintf(stderr,"p2p: the lobby gave no usable identity\n");
		return 0;
	}
	std::string stun,turn,users,passes;
	char server[300];
	for(int i=0;i<s->numstun;i++)
		if(SignalIceServer(s->stun[i],"stun",server,sizeof(server))) Append(stun,server);
	for(int i=0;i<s->numturn;i++)
		if(SignalIceServer(s->turn[i].url,"turn",server,sizeof(server)))
		{
			Append(turn,server);
			Append(users,s->turn[i].username);
			Append(passes,s->turn[i].credential);
		}
	if(mode==2&&turn.empty())
	{
		fprintf(stderr,"p2p: relay only, but the lobby has no TURN relay\n");
		return 0;
	}
	GnsP2PConfig config={s->identity,s->token,host,port,stun.c_str(),turn.c_str(),users.c_str(),passes.c_str(),mode==2,
		s->signalurl};
	return GnsLink::StartP2P(&config);
#else
	return 0;
#endif
}

int NetQueryHostP2P(const SOCKADDR_IN *dest,const char *identity)
{
#ifdef EDIFICE_GNS
	GnsLink *info=GnsLink::CurrentInfo();
	return info&&info->ConnectP2P(dest,identity);
#else
	return 0;
#endif
}
