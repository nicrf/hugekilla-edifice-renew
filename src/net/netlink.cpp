//Which DatagramLink each Sock gets: UDP, or GameNetworkingSockets (see netlink.h)

#include <StdAfx.h>
#include "netlink.h"
#include "udplink.h"
#ifdef EDIFICE_GNS
#include "gnslink.h"

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

DatagramLink *NetOpenInfoLink(WORD port,int host)
{
	UdpLink *udp=UdpLink::Open(port,1);
#ifdef EDIFICE_GNS
	int gnsport;
	if(UseGns(&gnsport))
	{
		GnsLink *link=GnsLink::OpenInfo(udp,host,gnsport);
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
