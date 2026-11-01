//GnsLink: DatagramLink over GameNetworkingSockets connections (see gnslink.h)

#include <StdAfx.h>
#include <deque>
#include <string>
#include <vector>
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include "gnslink.h"
#include "udplink.h"

#define GNSMAXQUEUE 512      //messages waiting per connection and channel: then the oldest go
#define GNSMAXPENDING 64     //messages sent before the connection is up, sent once it is
#define GNSMAXMESSAGE 4096   //channel byte included (Sock's datagrams are ~1000 bytes)

//a connection, shared by the info link that opened (client) or accepted (server) it and
//the data links using it. It lives while one of them does; the server closes it when
//its player's link goes, the client when its data link goes.
struct GnsConn
{
	HSteamNetConnection h;      //k_HSteamNetConnection_Invalid once closed
	SOCKADDR_IN addr;           //pseudo-address (gnslink.h)
	GnsLink *owner;             //its info link, 0 once that one is closed
	int refs;                   //data links using it
	int up;                     //connected: messages go out right away
	std::deque<std::string> queue[2];   //received, per channel
	std::vector<std::string> pending;   //sent while connecting
};

static int initialized;
static std::vector<GnsConn *> conns;
static std::vector<GnsLink *> infolinks;   //open info links, the last one is CurrentInfo
static std::vector<HSteamListenSocket> listensockets;   //...and the listen socket of each (or invalid)
static unsigned int nextpseudo;

static ISteamNetworkingSockets *Net()
{
	return SteamNetworkingSockets();
}

static int SameAddr(const SOCKADDR_IN *a,const SOCKADDR_IN *b)
{
	return a->sin_addr.s_addr==b->sin_addr.s_addr&&a->sin_port==b->sin_port;
}

static const char *AddrText(const SOCKADDR_IN *a)
{
	static char text[40];
	unsigned int ip=ntohl(a->sin_addr.s_addr);
	snprintf(text,sizeof(text),"%u.%u.%u.%u:%u",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,ntohs(a->sin_port));
	return text;
}

static GnsConn *FindHandle(HSteamNetConnection h)
{
	for(size_t i=0;i<conns.size();i++)
		if(conns[i]->h==h) return conns[i];
	return 0;
}

//the open connection named addr; of owner only, unless owner is 0
static GnsConn *FindAddr(const SOCKADDR_IN *addr,GnsLink *owner)
{
	for(size_t i=0;i<conns.size();i++)
	{
		GnsConn *c=conns[i];
		if(c->h==k_HSteamNetConnection_Invalid) continue;
		if(owner&&c->owner!=owner) continue;
		if(SameAddr(&c->addr,addr)) return c;
	}
	return 0;
}

//closes the connection (what was sent still goes out) and forgets it
static void Forget(GnsConn *c)
{
	if(c->h!=k_HSteamNetConnection_Invalid)
		Net()->CloseConnection(c->h,0,"closed",true);
	for(size_t i=0;i<conns.size();i++)
		if(conns[i]==c)
		{
			conns.erase(conns.begin()+i);
			break;
		}
	delete c;
}

//the pseudo-address of an accepted connection: its IPv4 address, else 0.0.x.y
static SOCKADDR_IN PseudoAddress(const SteamNetworkingIPAddr &remote)
{
	SOCKADDR_IN addr;
	ZeroMemory(&addr,sizeof(addr));
	addr.sin_family=AF_INET;
	if(remote.IsIPv4())
	{
		addr.sin_addr.s_addr=htonl(remote.GetIPv4());
		addr.sin_port=htons(remote.m_port);
		if(!FindAddr(&addr,0)) return addr;
	}
	do
	{
		nextpseudo=(nextpseudo+1)&0xffff;
		addr.sin_addr.s_addr=htonl(nextpseudo?nextpseudo:1);
		addr.sin_port=0;
	}
	while(FindAddr(&addr,0));
	return addr;
}

static int SendOn(GnsConn *c,int channel,const void *data,int size)
{
	if(c->h==k_HSteamNetConnection_Invalid) return -1;
	if(size<0||size+1>GNSMAXMESSAGE) return -1;
	char buf[GNSMAXMESSAGE];
	buf[0]=(char)channel;
	memcpy(&buf[1],data,size);
	if(!c->up)
	{
		if(c->pending.size()>=GNSMAXPENDING) return -1;
		c->pending.push_back(std::string(buf,size+1));
		return size;
	}
	EResult r=Net()->SendMessageToConnection(c->h,buf,size+1,k_nSteamNetworkingSend_UnreliableNoNagle,0);
	return r==k_EResultOK?size:-1;
}

static void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t *info)
{
	ISteamNetworkingSockets *net=Net();
	GnsConn *c=FindHandle(info->m_hConn);
	char remote[SteamNetworkingIPAddr::k_cchMaxString];
	info->m_info.m_addrRemote.ToString(remote,sizeof(remote),true);
	switch(info->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_Connecting:
		if(!c&&info->m_info.m_hListenSocket!=k_HSteamListenSocket_Invalid)
		{
			GnsLink *owner=0;
			for(size_t i=0;i<listensockets.size();i++)
				if(listensockets[i]==info->m_info.m_hListenSocket) owner=infolinks[i];
			if(!owner||net->AcceptConnection(info->m_hConn)!=k_EResultOK)
			{
				net->CloseConnection(info->m_hConn,0,"not accepted",false);
				break;
			}
			c=new GnsConn;
			c->h=info->m_hConn;
			c->addr=PseudoAddress(info->m_info.m_addrRemote);
			c->owner=owner;
			c->refs=0;
			c->up=0;
			conns.push_back(c);
			fprintf(stderr,"gns: connection from %s (as %s)\n",remote,AddrText(&c->addr));
		}
		break;
	case k_ESteamNetworkingConnectionState_Connected:
		if(!c) break;
		c->up=1;
		fprintf(stderr,"gns: connected with %s, %s\n",remote,
			(info->m_info.m_nFlags&k_nSteamNetworkConnectionInfoFlags_Unencrypted)?"NOT encrypted":"encrypted");
		for(size_t i=0;i<c->pending.size();i++)
			net->SendMessageToConnection(c->h,c->pending[i].data(),c->pending[i].size(),k_nSteamNetworkingSend_UnreliableNoNagle,0);
		c->pending.clear();
		break;
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		fprintf(stderr,"gns: connection with %s ended: %s\n",remote,info->m_info.m_szEndDebug);
		net->CloseConnection(info->m_hConn,0,0,false);
		if(!c) break;
		c->h=k_HSteamNetConnection_Invalid;
		c->up=0;
		c->pending.clear();
		if(!c->refs&&!c->owner) Forget(c);
		break;
	default:
		break;
	}
}

static void DebugOutput(ESteamNetworkingSocketsDebugOutputType,const char *text)
{
	fprintf(stderr,"gns: %s\n",text);
}

static int Init()
{
	if(initialized) return 1;
	SteamNetworkingErrMsg err;
	if(!GameNetworkingSockets_Init(0,err))
	{
		fprintf(stderr,"gns: GameNetworkingSockets_Init failed: %s\n",err);
		return 0;
	}
	SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning,DebugOutput);
	SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(OnStatusChanged);
	initialized=1;
	return 1;
}

//runs the callbacks (accepts, state changes) and queues what came in
static void Poll()
{
	ISteamNetworkingSockets *net=Net();
	net->RunCallbacks();
	for(size_t i=0;i<conns.size();i++)
	{
		GnsConn *c=conns[i];
		if(c->h==k_HSteamNetConnection_Invalid) continue;
		ISteamNetworkingMessage *msgs[32];
		int n;
		while((n=net->ReceiveMessagesOnConnection(c->h,msgs,32))>0)
		{
			for(int k=0;k<n;k++)
			{
				const char *d=(const char *)msgs[k]->m_pData;
				int size=msgs[k]->m_cbSize;
				if(size>=2&&(d[0]==GNSCHANNEL_INFO||d[0]==GNSCHANNEL_DATA))
				{
					std::deque<std::string> &q=c->queue[(int)d[0]];
					if(q.size()>=GNSMAXQUEUE) q.pop_front();
					q.push_back(std::string(d+1,size-1));
				}
				msgs[k]->Release();
			}
			if(n<32) break;
		}
	}
}

static int Pop(GnsConn *c,int channel,void *data,int maxsize,SOCKADDR_IN *from)
{
	std::deque<std::string> &q=c->queue[channel];
	if(q.empty()) return 0;
	std::string m;
	m.swap(q.front());
	q.pop_front();
	*from=c->addr;
	if((int)m.size()>maxsize) return -1;
	memcpy(data,m.data(),m.size());
	return (int)m.size();
}

GnsLink::GnsLink()
{
	channel=GNSCHANNEL_INFO;
	udp=0;
	host=0;
	gnsport=0;
	listen=k_HSteamListenSocket_Invalid;
	conn=0;
	nextconn=0;
}

GnsLink *GnsLink::OpenInfo(UdpLink *udp,int host,int gnsport)
{
	if(!Init()) return 0;
	HSteamListenSocket listen=k_HSteamListenSocket_Invalid;
	if(host)
	{
		SteamNetworkingIPAddr any;
		any.Clear();
		any.m_port=(uint16)gnsport;
		listen=Net()->CreateListenSocketIP(any,0,0);
		if(listen==k_HSteamListenSocket_Invalid)
		{
			fprintf(stderr,"gns: can't listen on port %d\n",gnsport);
			return 0;
		}
		fprintf(stderr,"gns: listening on port %d\n",gnsport);
	}
	GnsLink *link=new GnsLink;
	link->udp=udp;
	link->host=host;
	link->gnsport=gnsport;
	link->listen=listen;
	infolinks.push_back(link);
	listensockets.push_back(listen);
	return link;
}

GnsLink *GnsLink::OpenData(const SOCKADDR_IN *peer)
{
	if(!initialized) return 0;
	GnsConn *c=FindAddr(peer,0);
	if(!c) return 0;
	GnsLink *link=new GnsLink;
	link->channel=GNSCHANNEL_DATA;
	link->conn=c;
	c->refs++;
	return link;
}

GnsLink *GnsLink::CurrentInfo()
{
	return infolinks.empty()?0:infolinks.back();
}

void GnsLink::Shutdown()
{
	if(!initialized) return;
	while(!conns.empty()) Forget(conns.back());
	for(size_t i=0;i<listensockets.size();i++)
		if(listensockets[i]!=k_HSteamListenSocket_Invalid) Net()->CloseListenSocket(listensockets[i]);
	listensockets.clear();
	infolinks.clear();
	GameNetworkingSockets_Kill();
	initialized=0;
}

void GnsLink::Connect(const SOCKADDR_IN *addr)
{
	if(channel!=GNSCHANNEL_INFO||host||FindAddr(addr,this)) return;
	SteamNetworkingIPAddr ip;
	ip.Clear();
	ip.SetIPv4(ntohl(addr->sin_addr.s_addr),(uint16)gnsport);
	HSteamNetConnection h=Net()->ConnectByIPAddress(ip,0,0);
	if(h==k_HSteamNetConnection_Invalid)
	{
		fprintf(stderr,"gns: can't connect to %s\n",AddrText(addr));
		return;
	}
	GnsConn *c=new GnsConn;
	c->h=h;
	c->addr=*addr;
	c->owner=this;
	c->refs=0;
	c->up=0;
	conns.push_back(c);
	char text[SteamNetworkingIPAddr::k_cchMaxString];
	ip.ToString(text,sizeof(text),true);
	fprintf(stderr,"gns: connecting to %s\n",text);
}

unsigned int GnsLink::Connection()
{
	return conn?conn->h:k_HSteamNetConnection_Invalid;
}

GnsLink::~GnsLink()
{
	if(conn)
	{
		conn->refs--;
		if(!conn->refs) Forget(conn);   //its player left, or the client left the game
	}
	else
	{
		for(size_t i=conns.size();i-->0;)
		{
			if(i>=conns.size()||conns[i]->owner!=this) continue;
			if(conns[i]->refs) conns[i]->owner=0;   //the data link goes on with it
			else Forget(conns[i]);
		}
		for(size_t i=0;i<infolinks.size();i++)
			if(infolinks[i]==this)
			{
				if(listensockets[i]!=k_HSteamListenSocket_Invalid) Net()->CloseListenSocket(listensockets[i]);
				infolinks.erase(infolinks.begin()+i);
				listensockets.erase(listensockets.begin()+i);
				break;
			}
	}
	delete udp;
}

int GnsLink::SendTo(const void *data,int size,const SOCKADDR_IN *to)
{
	if(conn) return SendOn(conn,GNSCHANNEL_DATA,data,size);
	GnsConn *c=FindAddr(to,this);
	if(c) return SendOn(c,GNSCHANNEL_INFO,data,size);
	return udp?udp->SendTo(data,size,to):-1;
}

int GnsLink::ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from)
{
	if(conn)
	{
		int got=Pop(conn,GNSCHANNEL_DATA,data,maxsize,from);
		if(got) return got;
		Poll();
		return Pop(conn,GNSCHANNEL_DATA,data,maxsize,from);
	}
	if(udp)
	{
		int got=udp->ReceiveFrom(data,maxsize,from);
		if(got) return got;
	}
	for(int pass=0;pass<2;pass++)
	{
		if(pass) Poll();
		int n=(int)conns.size();
		for(int i=0;i<n;i++)
		{
			GnsConn *c=conns[(nextconn+i)%n];
			if(c->owner!=this) continue;
			int got=Pop(c,GNSCHANNEL_INFO,data,maxsize,from);
			if(!got) continue;
			nextconn=(nextconn+i+1)%n;
			return got;
		}
	}
	return 0;
}
