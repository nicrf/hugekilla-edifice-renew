//WebRtcLink: DatagramLink over WebRTC DataChannels in the browser (see webrtclink.h)

#include "hksocket.h"
#include <deque>
#include <random>
#include <string>
#include <vector>
#include <emscripten.h>
#include "webrtclink.h"
#include "lobby.h"
#include "signaling.h"

//the framing of GnsLink (gnslink.h), which the host's bridge reads
#define WEBCHANNEL_INFO 0
#define WEBCHANNEL_DATA 1
#define WEBMAXQUEUE 512      //messages waiting per connection and channel: then the oldest go
#define WEBMAXPENDING 64     //messages sent before the DataChannel is open, sent once it is
#define WEBMAXMESSAGE 4096   //channel byte included
//host: the limits of the native bridge (rtchost.cpp)
#define WEBMAXCLIENTS 16     //clients connected or connecting: offers beyond are ignored
#define WEBOPENTIME 30000    //ms a client has to open its DataChannel
#define WEBOFFERGAP 1000     //ms between two offers of one identity

extern "C"
{
	//src/platform/web/webrtc.js
	void hk_rtc_ws_open(const char *url,const char *hello);
	void hk_rtc_ws_send(const char *line);
	int hk_rtc_ws_recv(char *buf,int max);
	void hk_rtc_ws_close();
	int hk_rtc_connect(const char *ice,int relay);
	int hk_rtc_accept(const char *ice,int relay,const char *offer);
	int hk_rtc_local(int h,char *kind,char *a,int amax,char *b,int bmax);
	void hk_rtc_remote(int h,int kind,const char *a,const char *b);
	int hk_rtc_state(int h);
	int hk_rtc_relayed(int h);
	int hk_rtc_send(int h,const void *data,int size);
	int hk_rtc_recv(int h,void *data,int max);
	void hk_rtc_close(int h);
	void hk_web_hosting(int on);
}

//a connection to a host, shared like a GnsConn by the info link that opened it and
//the data link that took it over; or, hosting, a client's connection to us
struct WebConn
{
	int h;                       //webrtc.js handle, 0 once closed
	std::string peer;            //the other side's identity
	char cid[SIGNAL_RTC_MAXCID+1];
	SOCKADDR_IN addr;
	WebRtcLink *owner;
	int refs;
	int up;
	int warned;
	int incoming;                //a client's offer that we answered (hosting)
	int told;                    //hosting: its route was logged
	double created,upat;         //emscripten_get_now()
	std::deque<std::string> queue[2];
	std::vector<std::string> pending;
};

static std::vector<WebConn *> conns;
static std::vector<WebRtcLink *> infolinks;
static int started;
static std::string me,icejson;
static int relay;
static unsigned int nextpseudo;
static std::deque<std::pair<std::string,double>> lastoffers;   //rate limit

static int SameAddr(const SOCKADDR_IN *a,const SOCKADDR_IN *b)
{
	return a->sin_addr.s_addr==b->sin_addr.s_addr&&a->sin_port==b->sin_port;
}

static WebConn *FindAddr(const SOCKADDR_IN *addr,WebRtcLink *owner)
{
	for(size_t i=0;i<conns.size();i++)
	{
		WebConn *c=conns[i];
		if(!c->h||(owner&&c->owner!=owner)) continue;
		if(SameAddr(&c->addr,addr)) return c;
	}
	return 0;
}

static void Forget(WebConn *c)
{
	if(c->h) hk_rtc_close(c->h);
	for(size_t i=0;i<conns.size();i++)
		if(conns[i]==c)
		{
			conns.erase(conns.begin()+i);
			break;
		}
	delete c;
}

static const char *AddrText(const SOCKADDR_IN *a)
{
	static char text[40];
	unsigned int ip=ntohl(a->sin_addr.s_addr);
	snprintf(text,sizeof(text),"%u.%u.%u.%u",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255);
	return text;
}

//hosting: a client's name, 0.1.x.y:0 as the native bridge's (multiplay.cpp only
//compares addresses; 0.x.y.z never is a real host)
static SOCKADDR_IN PseudoAddress()
{
	SOCKADDR_IN addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family=AF_INET;
	do
	{
		nextpseudo=(nextpseudo+1)&0xffff;
		addr.sin_addr.s_addr=htonl(0x10000u|(nextpseudo?nextpseudo:1));
	}
	while(FindAddr(&addr,0));
	return addr;
}

//the server's info link, 0 if we don't host
static WebRtcLink *HostLink()
{
	for(size_t i=infolinks.size();i-->0;)
		if(infolinks[i]->IsHost()) return infolinks[i];
	return 0;
}

//a JSON string (the session's texts are printable ASCII without spaces)
static void Quote(std::string &out,const char *s)
{
	out+='"';
	for(;*s;s++)
	{
		if(*s=='"'||*s=='\\') out+='\\';
		if((unsigned char)*s>=32&&(unsigned char)*s<127) out+=*s;
	}
	out+='"';
}

static void SendSignal(WebConn *c,char kind,const char *a,const char *b)
{
	SignalRtc s;
	s.kind=kind;
	snprintf(s.cid,sizeof(s.cid),"%s",c->cid);
	s.a=a;
	s.b=b;
	std::string line;
	if(!SignalRtcFormat(c->peer.c_str(),&s,&line))
	{
		fprintf(stderr,"webrtc: a %c signal doesn't fit in a line, dropped\n",kind);
		return;
	}
	line.erase(line.size()-1);   //one line a WebSocket frame, without its '\n'
	hk_rtc_ws_send(line.c_str());
}

//1 if identity may make an offer now (at most one a second)
static int OfferAllowed(const std::string &identity,double now)
{
	for(size_t i=0;i<lastoffers.size();i++)
		if(lastoffers[i].first==identity&&now-lastoffers[i].second<WEBOFFERGAP) return 0;
	if(lastoffers.size()>=64) lastoffers.pop_front();
	lastoffers.push_back(std::make_pair(identity,now));
	return 1;
}

//hosting: a client's offer (a browser or a desktop game, which both offer)
static void Accept(const char *sender,const SignalRtc &s,double now)
{
	WebRtcLink *host=HostLink();
	if(!host||!started) return;
	WebConn *old=0;
	int clients=0;
	for(size_t i=0;i<conns.size();i++)
		if(conns[i]->incoming&&conns[i]->h)
		{
			clients++;
			if(conns[i]->peer==sender) old=conns[i];
		}
	if(old&&!strcmp(old->cid,s.cid)) return;   //sent twice
	if(!OfferAllowed(sender,now)) return;
	if(old)
	{
		//a new try from the same client: its last one goes, unless a player uses it
		if(old->refs) return;
		Forget(old);
		clients--;
	}
	if(clients>=WEBMAXCLIENTS||me==sender)
	{
		fprintf(stderr,"webrtc: offer from %s ignored (%s)\n",sender,me==sender?"ourselves":"too many clients");
		return;
	}
	int h=hk_rtc_accept(icejson.c_str(),relay,s.a.c_str());
	if(!h) return;
	WebConn *c=new WebConn;
	c->h=h;
	c->peer=sender;
	snprintf(c->cid,sizeof(c->cid),"%s",s.cid);
	c->addr=PseudoAddress();
	c->owner=host;
	c->refs=0;
	c->up=0;
	c->warned=0;
	c->incoming=1;
	c->told=0;
	c->created=now;
	c->upat=0;
	conns.push_back(c);
	fprintf(stderr,"webrtc: offer from %s (as %s)\n",sender,AddrText(&c->addr));
}

//every frame, from the links: signals both ways, datagrams in, state
static void Poll()
{
	static char line[SIGNAL_MAXLINE+1],sender[SIGNAL_MAXIDENTITY+1];
	static SignalRtc s;
	double now=emscripten_get_now();
	int n;
	while((n=hk_rtc_ws_recv(line,sizeof(line)))!=0)
	{
		if(n<0) continue;
		if(!strncmp(line,"ERROR",5))
		{
			fprintf(stderr,"signaling: the lobby refused our session\n");
			continue;
		}
		if(!SignalRtcParseLine(line,n,sender,sizeof(sender),&s)) continue;
		if(s.kind=='o')
		{
			Accept(sender,s,now);
			continue;
		}
		for(size_t i=0;i<conns.size();i++)
		{
			WebConn *c=conns[i];
			if(!c->h||c->peer!=sender||strcmp(c->cid,s.cid)) continue;
			if(s.kind=='a'&&c->incoming) continue;   //we answer those
			hk_rtc_remote(c->h,s.kind,s.a.c_str(),s.b.c_str());
		}
	}
	static char a[SIGNAL_RTC_MAXTEXT+1],b[SIGNAL_RTC_MAXTEXT+1];
	static char msg[WEBMAXMESSAGE];
	for(size_t i=0;i<conns.size();i++)
	{
		WebConn *c=conns[i];
		if(!c->h) continue;
		char kind;
		int r;
		while((r=hk_rtc_local(c->h,&kind,a,sizeof(a),b,sizeof(b)))!=0)
			if(r>0) SendSignal(c,kind,a,b);
		int state=hk_rtc_state(c->h);
		if(state>0&&!c->up)
		{
			c->up=1;
			c->upat=now;
			fprintf(stderr,"webrtc: DataChannel open with %s\n",c->peer.c_str());
			for(size_t k=0;k<c->pending.size();k++)
				hk_rtc_send(c->h,c->pending[k].data(),(int)c->pending[k].size());
			c->pending.clear();
			if(c->incoming) hk_rtc_relayed(c->h);   //asks the browser which route ICE took
		}
		if(c->up&&c->incoming&&!c->told&&now-c->upat>1000)
		{
			//the route, as the native bridge tells it (tests check it)
			c->told=1;
			fprintf(stderr,"webrtc: connected with %s, encrypted (DTLS), %s\n",c->peer.c_str(),
				hk_rtc_relayed(c->h)?"relayed (TURN)":"direct");
		}
		while((n=hk_rtc_recv(c->h,msg,sizeof(msg)))!=0)
		{
			if(n<2||(msg[0]!=WEBCHANNEL_INFO&&msg[0]!=WEBCHANNEL_DATA)) continue;
			std::deque<std::string> &q=c->queue[(int)msg[0]];
			if(q.size()>=WEBMAXQUEUE) q.pop_front();
			q.push_back(std::string(msg+1,n-1));
		}
		//a client that never opened, or whose server stopped (no player on it)
		if(c->incoming&&!c->refs&&((!c->up&&now-c->created>WEBOPENTIME)||!c->owner)) state=-1;
		if(state<0)
		{
			if(!c->warned++) fprintf(stderr,"webrtc: connection with %s %s\n",c->peer.c_str(),c->up?"lost":"failed");
			c->up=0;
			c->pending.clear();
			hk_rtc_close(c->h);
			c->h=0;
		}
	}
	for(size_t i=conns.size();i-->0;)
		if(!conns[i]->h&&!conns[i]->refs) Forget(conns[i]);
}

static int SendOn(WebConn *c,int channel,const void *data,int size)
{
	if(!c->h||size<0||size+1>WEBMAXMESSAGE) return -1;
	char buf[WEBMAXMESSAGE];
	buf[0]=(char)channel;
	memcpy(&buf[1],data,size);
	if(!c->up)
	{
		if(c->pending.size()>=WEBMAXPENDING) return -1;
		c->pending.push_back(std::string(buf,size+1));
		return size;
	}
	return hk_rtc_send(c->h,buf,size+1)>0?size:-1;
}

static int Pop(WebConn *c,int channel,void *data,int maxsize,SOCKADDR_IN *from)
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

WebRtcLink::WebRtcLink()
{
	channel=WEBCHANNEL_INFO;
	host=0;
	conn=0;
	nextconn=0;
}

WebRtcLink *WebRtcLink::OpenInfo(int host)
{
	WebRtcLink *link=new WebRtcLink;
	link->host=host;
	infolinks.push_back(link);
	return link;
}

WebRtcLink *WebRtcLink::OpenData(const SOCKADDR_IN *peer)
{
	WebConn *c=FindAddr(peer,0);
	if(!c) return 0;
	WebRtcLink *link=new WebRtcLink;
	link->channel=WEBCHANNEL_DATA;
	link->conn=c;
	c->refs++;
	return link;
}

WebRtcLink *WebRtcLink::CurrentInfo()
{
	return infolinks.empty()?0:infolinks.back();
}

int WebRtcLink::Start(const LobbySession *s,int relayonly)
{
	char host[254],path[200];
	int port,tls;
	if(!s->signalurl[0]||!SignalParseURL(s->signalurl,host,sizeof(host),&port,path,sizeof(path),&tls))
	{
		fprintf(stderr,"p2p: the lobby gave no WebSocket relay, which a browser needs\n");
		return 0;
	}
	std::string hello;
	if(!SignalHelloLine(s->identity,s->token,&hello))
	{
		fprintf(stderr,"p2p: the lobby gave no usable identity\n");
		return 0;
	}
	hello.erase(hello.size()-1);
	std::string ice="[";
	for(int i=0;i<s->numstun;i++)
	{
		if(ice.size()>1) ice+=',';
		ice+="{\"urls\":";
		Quote(ice,s->stun[i]);
		ice+='}';
	}
	int turns=0;
	for(int i=0;i<s->numturn;i++)
	{
		if(ice.size()>1) ice+=',';
		ice+="{\"urls\":";
		Quote(ice,s->turn[i].url);
		ice+=",\"username\":";
		Quote(ice,s->turn[i].username);
		ice+=",\"credential\":";
		Quote(ice,s->turn[i].credential);
		ice+='}';
		turns++;
	}
	ice+=']';
	if(relayonly&&!turns)
	{
		fprintf(stderr,"p2p: relay only, but the lobby has no TURN relay\n");
		return 0;
	}
	me=s->identity;
	icejson=ice;
	relay=relayonly;
	hk_rtc_ws_open(s->signalurl,hello.c_str());   //right away: the other side answers there
	if(!started) fprintf(stderr,"webrtc: P2P as %s, signaling %s%s\n",s->identity,s->signalurl,relayonly?" (relay only)":"");
	started=1;
	return 1;
}

void WebRtcLink::Shutdown()
{
	while(!conns.empty()) Forget(conns.back());
	infolinks.clear();
	if(started) hk_rtc_ws_close();
	started=0;
	HostBanner(0);
}

int WebRtcLink::IsPeer(const SOCKADDR_IN *peer)
{
	WebConn *c=FindAddr(peer,0);
	return c&&c->incoming&&c->up;
}

void WebRtcLink::HostBanner(int on)
{
	static int shown;
	if(on==shown) return;
	shown=on;
	hk_web_hosting(on);
}

int WebRtcLink::ConnectP2P(const SOCKADDR_IN *addr,const char *identity)
{
	if(channel!=WEBCHANNEL_INFO||!started) return 0;
	if(FindAddr(addr,this)) return 1;
	if(!SignalValidIdentity(identity)||me==identity)
	{
		fprintf(stderr,"webrtc: can't connect to %s\n",identity);
		return 0;
	}
	int h=hk_rtc_connect(icejson.c_str(),relay);
	if(!h) return 0;
	WebConn *c=new WebConn;
	c->h=h;
	c->peer=identity;
	//a new id for each try: the host drops what it had of an older one
	static std::mt19937 rng(std::random_device{}());
	static const char digits[]="0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	for(int i=0;i<12;i++) c->cid[i]=digits[rng()%62];
	c->cid[12]=0;
	c->addr=*addr;
	c->owner=this;
	c->refs=0;
	c->up=0;
	c->warned=0;
	c->incoming=0;
	c->told=0;
	c->created=emscripten_get_now();
	c->upat=0;
	conns.push_back(c);
	fprintf(stderr,"webrtc: connecting to %s\n",identity);
	return 1;
}

WebRtcLink::~WebRtcLink()
{
	if(conn)
	{
		conn->refs--;
		if(!conn->refs) Forget(conn);   //the client left the game, or its player left ours
		return;
	}
	for(size_t i=conns.size();i-->0;)
	{
		if(i>=conns.size()||conns[i]->owner!=this) continue;
		if(conns[i]->refs) conns[i]->owner=0;   //the data link goes on with it
		else Forget(conns[i]);
	}
	for(size_t i=0;i<infolinks.size();i++)
		if(infolinks[i]==this)
		{
			infolinks.erase(infolinks.begin()+i);
			break;
		}
}

int WebRtcLink::SendTo(const void *data,int size,const SOCKADDR_IN *to)
{
	if(conn) return SendOn(conn,WEBCHANNEL_DATA,data,size);
	WebConn *c=FindAddr(to,this);
	if(c) return SendOn(c,WEBCHANNEL_INFO,data,size);
	return size;   //no UDP in a browser: the LAN search and solo games send into the void
}

int WebRtcLink::ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from)
{
	Poll();
	if(conn) return Pop(conn,WEBCHANNEL_DATA,data,maxsize,from);
	int n=(int)conns.size();
	for(int i=0;i<n;i++)
	{
		WebConn *c=conns[(nextconn+i)%n];
		if(c->owner!=this) continue;
		int got=Pop(c,WEBCHANNEL_INFO,data,maxsize,from);
		if(!got) continue;
		nextconn=(nextconn+i+1)%n;
		return got;
	}
	return 0;
}
