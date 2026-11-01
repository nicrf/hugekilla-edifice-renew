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

#ifdef EDIFICE_WEBRTC
#include "rtchost.h"
#endif

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

#ifdef __EMSCRIPTEN__
#include "lobby.h"
#include "webrtclink.h"

//The browser has no UDP (roadmap M14): Emscripten would turn each address into a
//WebSocket connection. A solo game's info socket sends into the void; an Internet
//game is joined over a WebRTC DataChannel (WebRtcLink, M14 task 3).
class NullLink:public DatagramLink
{
public:
	int SendTo(const void *,int size,const SOCKADDR_IN *) { return size; }
	int ReceiveFrom(void *,int,SOCKADDR_IN *) { return 0; }
};
#endif

DatagramLink *NetOpenInfoLink(WORD port,int host,int p2p)
{
#ifdef __EMSCRIPTEN__
	(void)port;(void)host;(void)p2p;
	return WebRtcLink::OpenInfo();
#else
	UdpLink *udp=UdpLink::Open(port,1);
#ifdef EDIFICE_GNS
	int gnsport;
	int ip=UseGns(&gnsport);
	if(ip||(p2p&&NetP2PMode()))
	{
		GnsLink *link=GnsLink::OpenInfo(udp,host,gnsport,ip);
#ifdef EDIFICE_WEBRTC
		//an Internet host also takes browsers, over WebRTC (rtchost.h)
		if(link&&host&&p2p&&NetP2PMode()) return RtcHostInfoLink(link);
#endif
		if(link) return link;
		fprintf(stderr,"gns: unavailable, plain UDP only\n");
	}
#endif
	return udp;
#endif
}

DatagramLink *NetOpenDataLink(WORD port,const SOCKADDR_IN *server)
{
#ifdef __EMSCRIPTEN__
	(void)port;
	DatagramLink *link=WebRtcLink::OpenData(server);
	return link?link:new NullLink;
#else
#ifdef EDIFICE_GNS
	GnsLink *link=GnsLink::OpenData(server);
	if(link) return link;
#endif
	return UdpLink::Open(port,0);
#endif
}

DatagramLink *NetOpenPeerLink(const SOCKADDR_IN *source)
{
#ifdef EDIFICE_WEBRTC
	if(DatagramLink *browser=RtcHostOpenData(source)) return browser;
#endif
#ifdef EDIFICE_GNS
	return GnsLink::OpenData(source);
#else
	return 0;
#endif
}

int NetSourceIsConnection(const SOCKADDR_IN *source)
{
#ifdef EDIFICE_WEBRTC
	if(RtcHostIsPeer(source)) return 1;   //DTLS proved who it is
#endif
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
#ifdef EDIFICE_WEBRTC
	RtcHostShutdown();   //first: its peers still signal through GnsLink's relay client
#endif
#ifdef EDIFICE_GNS
	GnsLink::Shutdown();
#endif
#ifdef __EMSCRIPTEN__
	WebRtcLink::Shutdown();
#endif
}

int NetLinkIsP2P(DatagramLink *link)
{
#ifdef EDIFICE_GNS
	return link&&dynamic_cast<GnsLink *>(link)!=0;
#elif defined(__EMSCRIPTEN__)
	return link&&dynamic_cast<WebRtcLink *>(link)!=0;
#else
	(void)link;
	return 0;
#endif
}

int NetWebRtcHost()
{
#ifdef EDIFICE_WEBRTC
	return 1;
#else
	return 0;
#endif
}

int NetP2PMode()
{
#if defined(EDIFICE_GNS)||defined(__EMSCRIPTEN__)
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
#ifdef __EMSCRIPTEN__
	(void)lobbyurl;
	int mode=NetP2PMode();
	return mode&&WebRtcLink::Start(s,mode==2);
#elif defined(EDIFICE_GNS)
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
#ifdef EDIFICE_WEBRTC
	RtcHostConfigure(stun.c_str(),turn.c_str(),users.c_str(),passes.c_str(),mode==2);   //the same servers for browsers
#endif
	return GnsLink::StartP2P(&config);
#else
	return 0;
#endif
}

int NetQueryHostP2P(const SOCKADDR_IN *dest,const char *identity)
{
#ifdef __EMSCRIPTEN__
	WebRtcLink *info=WebRtcLink::CurrentInfo();
	return info&&info->ConnectP2P(dest,identity);
#elif defined(EDIFICE_GNS)
	GnsLink *info=GnsLink::CurrentInfo();
	return info&&info->ConnectP2P(dest,identity);
#else
	return 0;
#endif
}
