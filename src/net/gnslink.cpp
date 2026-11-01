//GnsLink: DatagramLink over GameNetworkingSockets connections (see gnslink.h)

#include "win32compat.h"
#include <deque>
#include <string>
#include <vector>
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include "gnslink.h"
#include "udplink.h"
#include "gnssignal.h"

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
	int p2p;                    //a P2P connection (named by an identity)
	std::deque<std::string> queue[2];   //received, per channel
	std::vector<std::string> pending;   //sent while connecting
};

static int initialized;
static std::vector<GnsConn *> conns;
static std::vector<GnsLink *> infolinks;   //open info links, the last one is CurrentInfo
static std::vector<HSteamListenSocket> listensockets;   //...and the listen socket of each (or invalid)
static std::vector<HSteamListenSocket> p2psockets;      //...and its P2P one (or invalid)
static unsigned int nextpseudo;
static int p2pready;                 //StartP2P done: our identity is p2pidentity
static std::string p2pidentity;

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

//the pseudo-address of an accepted connection: its IPv4 address, else (and for P2P)
//0.0.x.y
static SOCKADDR_IN PseudoAddress(const SteamNetworkingIPAddr &remote,int p2p)
{
	SOCKADDR_IN addr;
	ZeroMemory(&addr,sizeof(addr));
	addr.sin_family=AF_INET;
	if(remote.IsIPv4()&&!p2p)
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
	//who: its address, or for P2P its identity (vouched for by the signaling relay)
	char remote[SteamNetworkingIdentity::k_cchMaxString];
	int p2p=info->m_info.m_identityRemote.m_eType==k_ESteamNetworkingIdentityType_GenericString;
	if(p2p) info->m_info.m_identityRemote.ToString(remote,sizeof(remote));
	else info->m_info.m_addrRemote.ToString(remote,sizeof(remote),true);
	switch(info->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_Connecting:
		if(!c&&info->m_info.m_hListenSocket!=k_HSteamListenSocket_Invalid)
		{
			GnsLink *owner=0;
			int byp2p=0;
			for(size_t i=0;i<listensockets.size();i++)
			{
				if(listensockets[i]==info->m_info.m_hListenSocket) owner=infolinks[i];
				if(p2psockets[i]==info->m_info.m_hListenSocket) owner=infolinks[i],byp2p=1;
			}
			if(!owner||net->AcceptConnection(info->m_hConn)!=k_EResultOK)
			{
				net->CloseConnection(info->m_hConn,0,"not accepted",false);
				break;
			}
			c=new GnsConn;
			c->h=info->m_hConn;
			c->addr=PseudoAddress(info->m_info.m_addrRemote,byp2p);
			c->owner=owner;
			c->refs=0;
			c->up=0;
			c->p2p=byp2p;
			conns.push_back(c);
			fprintf(stderr,"gns: %sconnection from %s (as %s)\n",byp2p?"P2P ":"",remote,AddrText(&c->addr));
		}
		break;
	case k_ESteamNetworkingConnectionState_Connected:
		if(!c) break;
		c->up=1;
		fprintf(stderr,"gns: connected with %s, %s%s\n",remote,
			(info->m_info.m_nFlags&k_nSteamNetworkConnectionInfoFlags_Unencrypted)?"NOT encrypted":"encrypted",
			!c->p2p?"":(info->m_info.m_nFlags&k_nSteamNetworkConnectionInfoFlags_Relayed)?", P2P relayed (TURN)":", P2P direct");
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
	//HK_GNS_DEBUG=1: GameNetworkingSockets' own messages, ICE included (debugging)
	ESteamNetworkingSocketsDebugOutputType level=k_ESteamNetworkingSocketsDebugOutputType_Warning;
	if(getenv("HK_GNS_DEBUG"))
	{
		level=k_ESteamNetworkingSocketsDebugOutputType_Verbose;
		SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_LogLevel_P2PRendezvous,
			k_ESteamNetworkingSocketsDebugOutputType_Verbose);
	}
	SteamNetworkingUtils()->SetDebugOutputFunction(level,DebugOutput);
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
	ip=0;
	gnsport=0;
	listen=k_HSteamListenSocket_Invalid;
	conn=0;
	nextconn=0;
}

//P2P listen sockets open: connection requests are taken while there is one
static void CountP2PListen()
{
	int n=0;
	for(size_t i=0;i<p2psockets.size();i++)
		n+=p2psockets[i]!=k_HSteamListenSocket_Invalid;
	GnsSignalListen(n);
}

GnsLink *GnsLink::OpenInfo(UdpLink *udp,int host,int gnsport,int ip)
{
	if(!Init()) return 0;
	HSteamListenSocket listen=k_HSteamListenSocket_Invalid,p2p=k_HSteamListenSocket_Invalid;
	if(host&&ip)
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
	if(host&&p2pready)
	{
		p2p=Net()->CreateListenSocketP2P(0,0,0);
		if(p2p!=k_HSteamListenSocket_Invalid)
			fprintf(stderr,"gns: listening for P2P connections as %s\n",p2pidentity.c_str());
	}
	GnsLink *link=new GnsLink;
	link->udp=udp;
	link->host=host;
	link->ip=ip;
	link->gnsport=gnsport;
	link->listen=listen;
	infolinks.push_back(link);
	listensockets.push_back(listen);
	p2psockets.push_back(p2p);
	CountP2PListen();
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

int GnsLink::IsConnection(const SOCKADDR_IN *peer)
{
	return initialized&&FindAddr(peer,0)!=0;
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
	{
		if(listensockets[i]!=k_HSteamListenSocket_Invalid) Net()->CloseListenSocket(listensockets[i]);
		if(p2psockets[i]!=k_HSteamListenSocket_Invalid) Net()->CloseListenSocket(p2psockets[i]);
	}
	listensockets.clear();
	p2psockets.clear();
	infolinks.clear();
	GameNetworkingSockets_Kill();
	GnsSignalStop();   //after: closing connections may still have signals to send
	initialized=0;
	p2pready=0;
	p2pidentity.clear();
}

int GnsLink::StartP2P(const GnsP2PConfig *config)
{
	if(!Init()) return 0;
	SteamNetworkingIdentity me;
	if(!me.ParseString(config->identity)||me.m_eType!=k_ESteamNetworkingIdentityType_GenericString)
	{
		fprintf(stderr,"gns: not a P2P identity: %s\n",config->identity);
		return 0;
	}
	ISteamNetworkingSockets *net=Net();
	if(p2pidentity!=config->identity)
	{
		//a new identity: GameNetworkingSockets closes every connection and listen
		//socket (ResetIdentity), the IP listen sockets are opened again
		if(!conns.empty()) fprintf(stderr,"gns: new identity, open connections closed\n");
		net->ResetIdentity(&me);
		for(size_t i=conns.size();i-->0;)
		{
			GnsConn *c=conns[i];
			c->h=k_HSteamNetConnection_Invalid;
			c->up=0;
			c->pending.clear();
			if(!c->refs&&!c->owner) Forget(c);
		}
		for(size_t i=0;i<infolinks.size();i++)
		{
			p2psockets[i]=k_HSteamListenSocket_Invalid;
			if(listensockets[i]==k_HSteamListenSocket_Invalid) continue;
			SteamNetworkingIPAddr any;
			any.Clear();
			any.m_port=(uint16)infolinks[i]->gnsport;
			listensockets[i]=net->CreateListenSocketIP(any,0,0);
			if(listensockets[i]==k_HSteamListenSocket_Invalid)
				fprintf(stderr,"gns: can't listen on port %d again\n",infolinks[i]->gnsport);
		}
		p2pidentity=config->identity;
	}
	//STUN/TURN servers and the candidates we share: global defaults, which the P2P
	//listen sockets and connections take when they are created
	ISteamNetworkingUtils *utils=SteamNetworkingUtils();
	utils->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_STUN_ServerList,config->stun);
	utils->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_ServerList,config->turn);
	utils->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_UserList,config->turnusers);
	utils->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_PassList,config->turnpasses);
	utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,config->relayonly?
		k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Relay:k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_All);
	GnsSignalStart(config->signalhost,config->signalport,config->signalurl,config->identity,config->token);
	p2pready=1;
	char relay[300];
	if(config->signalurl&&config->signalurl[0]) snprintf(relay,sizeof(relay),"%s",config->signalurl);
	else snprintf(relay,sizeof(relay),"%s:%d",config->signalhost,config->signalport);
	fprintf(stderr,"gns: P2P as %s, signaling %s, STUN %s, TURN %s%s\n",config->identity,
		relay,config->stun[0]?config->stun:"none",
		config->turn[0]?config->turn:"none",config->relayonly?" (relay only)":"");
	int ok=1;
	for(size_t i=0;i<infolinks.size();i++)
	{
		if(!infolinks[i]->host||p2psockets[i]!=k_HSteamListenSocket_Invalid) continue;
		p2psockets[i]=net->CreateListenSocketP2P(0,0,0);
		if(p2psockets[i]==k_HSteamListenSocket_Invalid)
		{
			fprintf(stderr,"gns: can't listen for P2P connections\n");
			ok=0;
		}
		else fprintf(stderr,"gns: listening for P2P connections as %s\n",config->identity);
	}
	CountP2PListen();
	return ok;
}

void GnsLink::Connect(const SOCKADDR_IN *addr)
{
	if(channel!=GNSCHANNEL_INFO||host||!ip||FindAddr(addr,this)) return;
	SteamNetworkingIPAddr ipaddr;
	ipaddr.Clear();
	ipaddr.SetIPv4(ntohl(addr->sin_addr.s_addr),(uint16)gnsport);
	HSteamNetConnection h=Net()->ConnectByIPAddress(ipaddr,0,0);
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
	c->p2p=0;
	conns.push_back(c);
	char text[SteamNetworkingIPAddr::k_cchMaxString];
	ipaddr.ToString(text,sizeof(text),true);
	fprintf(stderr,"gns: connecting to %s\n",text);
}

int GnsLink::ConnectP2P(const SOCKADDR_IN *addr,const char *identity)
{
	if(channel!=GNSCHANNEL_INFO||host||!p2pready) return 0;
	if(FindAddr(addr,this)) return 1;
	SteamNetworkingIdentity peer;
	if(!peer.ParseString(identity)||peer.m_eType!=k_ESteamNetworkingIdentityType_GenericString||
		p2pidentity==identity)
	{
		fprintf(stderr,"gns: can't connect P2P to %s\n",identity);
		return 0;
	}
	GnsSignalExpect(identity);
	HSteamNetConnection h=Net()->ConnectP2PCustomSignaling(GnsSignalFor(identity),&peer,0,0,0);
	if(h==k_HSteamNetConnection_Invalid)
	{
		fprintf(stderr,"gns: can't connect P2P to %s\n",identity);
		return 0;
	}
	GnsConn *c=new GnsConn;
	c->h=h;
	c->addr=*addr;
	c->owner=this;
	c->refs=0;
	c->up=0;
	c->p2p=1;
	conns.push_back(c);
	fprintf(stderr,"gns: connecting P2P to %s (the game at %s)\n",identity,AddrText(addr));
	return 1;
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
				if(p2psockets[i]!=k_HSteamListenSocket_Invalid) Net()->CloseListenSocket(p2psockets[i]);
				infolinks.erase(infolinks.begin()+i);
				listensockets.erase(listensockets.begin()+i);
				p2psockets.erase(p2psockets.begin()+i);
				break;
			}
		CountP2PListen();
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
