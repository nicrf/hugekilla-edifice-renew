//The WebRTC bridge of a native host: browsers' DataChannels (see rtchost.h)

#include "hksocket.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include <rtc/rtc.hpp>
#include "rtchost.h"
#include "gnslink.h"     //GNSCHANNEL_INFO, GNSCHANNEL_DATA: the same framing
#include "gnssignal.h"
#include "signaling.h"

#define RTCMAXPEERS 32        //browsers at once (MAXPLAYERS): offers beyond are ignored
#define RTCMAXQUEUE 512       //messages waiting per peer and channel: then the oldest go
#define RTCMAXMESSAGE 4096    //channel byte included (Sock's datagrams are ~1000 bytes)
#define RTCMAXSIGNALS 256     //signals waiting for the game thread
#define RTCOPENTIME 30000     //ms a browser has to open its DataChannel
#define RTCOFFERGAP 1000      //ms between two offers of one identity
#define RTCMAXPENDING 64      //client: messages sent before the DataChannel is open

class RtcInfo;

//a browser. The game thread owns the list and the PeerConnection; libdatachannel's
//threads only reach a peer through a weak_ptr, and touch what is marked "locked".
struct RtcPeer
{
	std::string identity,cid;
	std::shared_ptr<rtc::PeerConnection> pc;
	SOCKADDR_IN addr;                    //pseudo-address 0.1.x.y:0
	RtcInfo *owner;                      //its info link, 0 once that one is closed
	int refs;                            //data links using it
	unsigned int created;
	int told;                            //its route was logged
	int outgoing;                        //our offer to a browser host (we are its client)
	std::vector<std::string> pending;    //outgoing: sent before the DataChannel opened
	//locked:
	std::shared_ptr<rtc::DataChannel> dc;
	int up;                              //DataChannel open
	int closed;                          //DataChannel or connection gone
	std::deque<std::string> queue[2];    //received, per channel
};

struct PendingSignal
{
	std::string sender;
	SignalRtc s;
};

static std::mutex lock;                  //what is marked locked, and the two below
static std::deque<PendingSignal> signals;
static rtc::Configuration config;
static std::atomic<int> hostlinks(0);    //info links open: signals are taken
static std::vector<std::shared_ptr<RtcPeer>> peers;   //game thread only
static std::vector<RtcInfo *> infolinks;
static unsigned int nextpseudo;
static int logging;
static std::deque<std::pair<std::string,unsigned int>> lastoffers;   //rate limit

static unsigned int Now()
{
	using namespace std::chrono;
	return (unsigned int)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static const char *AddrText(const SOCKADDR_IN *a)
{
	static char text[40];
	unsigned int ip=ntohl(a->sin_addr.s_addr);
	snprintf(text,sizeof(text),"%u.%u.%u.%u",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255);
	return text;
}

static RtcPeer *FindAddr(const SOCKADDR_IN *addr)
{
	for(size_t i=0;i<peers.size();i++)
		if(peers[i]->addr.sin_addr.s_addr==addr->sin_addr.s_addr&&peers[i]->addr.sin_port==addr->sin_port)
			return peers[i].get();
	return 0;
}

static std::shared_ptr<RtcPeer> FindIdentity(const std::string &identity,int outgoing)
{
	for(size_t i=0;i<peers.size();i++)
		if(peers[i]->identity==identity&&peers[i]->outgoing==outgoing) return peers[i];
	return 0;
}

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
	while(FindAddr(&addr));
	return addr;
}

//forgets the peer and closes its connection (game thread, without the lock:
//libdatachannel may be waiting for it in a callback)
static void Forget(RtcPeer *p)
{
	std::shared_ptr<RtcPeer> keep;
	for(size_t i=0;i<peers.size();i++)
		if(peers[i].get()==p)
		{
			keep=peers[i];
			peers.erase(peers.begin()+i);
			break;
		}
	if(!keep) return;
	std::shared_ptr<rtc::DataChannel> dc;
	{
		std::lock_guard<std::mutex> l(lock);
		dc.swap(keep->dc);
		keep->closed=1;
	}
	try
	{
		if(dc)
		{
			dc->resetCallbacks();
			dc->close();
		}
		keep->pc->resetCallbacks();
		keep->pc->close();
	}
	catch(const std::exception &e)
	{
		fprintf(stderr,"webrtc: closing %s: %s\n",keep->identity.c_str(),e.what());
	}
	fprintf(stderr,"webrtc: %s (%s) closed\n",keep->identity.c_str(),AddrText(&keep->addr));
}

static int SendOn(RtcPeer *p,int channel,const void *data,int size)
{
	if(size<0||size+1>RTCMAXMESSAGE) return -1;
	std::shared_ptr<rtc::DataChannel> dc;
	int up;
	{
		std::lock_guard<std::mutex> l(lock);
		if(p->closed) return -1;
		up=p->up;
		dc=p->dc;
	}
	rtc::byte buf[RTCMAXMESSAGE];
	buf[0]=(rtc::byte)channel;
	memcpy(&buf[1],data,size);
	if(!up&&p->outgoing)
	{
		//a client's first REQUESTINFO goes out once the DataChannel opens (Pump)
		if(p->pending.size()>=RTCMAXPENDING) return -1;
		p->pending.push_back(std::string((const char *)buf,size+1));
		return size;
	}
	if(!up||!dc) return -1;
	try
	{
		dc->send(buf,size+1);   //false only means buffered
	}
	catch(const std::exception &)
	{
		return -1;   //closed meanwhile
	}
	return size;
}

//a message SendOn kept until the DataChannel opened
static void SendRaw(RtcPeer *p,const std::string &m)
{
	std::shared_ptr<rtc::DataChannel> dc;
	{
		std::lock_guard<std::mutex> l(lock);
		if(!p->up||p->closed) return;
		dc=p->dc;
	}
	try
	{
		if(dc) dc->send((const rtc::byte *)m.data(),m.size());
	}
	catch(const std::exception &) {}
}

static int Pop(RtcPeer *p,int channel,void *data,int maxsize,SOCKADDR_IN *from)
{
	std::string m;
	{
		std::lock_guard<std::mutex> l(lock);
		std::deque<std::string> &q=p->queue[channel];
		if(q.empty()) return 0;
		m.swap(q.front());
		q.pop_front();
	}
	*from=p->addr;
	if((int)m.size()>maxsize) return -1;
	memcpy(data,m.data(),m.size());
	return (int)m.size();
}

//a signal for the browser, through the relay (from libdatachannel's threads)
static void Signal(const std::string &to,char kind,const std::string &cid,const std::string &a,const std::string &b)
{
	SignalRtc s;
	s.kind=kind;
	snprintf(s.cid,sizeof(s.cid),"%s",cid.c_str());
	s.a=a;
	s.b=b;
	std::string line;
	if(SignalRtcFormat(to.c_str(),&s,&line)) GnsSignalSendLine(line);
	else fprintf(stderr,"webrtc: a %c signal for %s doesn't fit in a line, dropped\n",kind,to.c_str());
}

static void TakeChannel(std::weak_ptr<RtcPeer> weak,std::shared_ptr<rtc::DataChannel> dc)
{
	std::shared_ptr<RtcPeer> p=weak.lock();
	if(!p) return;
	{
		std::lock_guard<std::mutex> l(lock);
		if(p->dc||p->closed||dc->label()!="edifice") return;   //one channel, ours
		p->dc=dc;
	}
	dc->onOpen([weak]()
	{
		std::shared_ptr<RtcPeer> p=weak.lock();
		if(!p) return;
		std::lock_guard<std::mutex> l(lock);
		p->up=1;
	});
	dc->onClosed([weak]()
	{
		std::shared_ptr<RtcPeer> p=weak.lock();
		if(!p) return;
		std::lock_guard<std::mutex> l(lock);
		p->closed=1;
	});
	dc->onMessage([weak](rtc::message_variant m)
	{
		const rtc::binary *bin=std::get_if<rtc::binary>(&m);
		if(!bin||bin->size()<2||bin->size()>RTCMAXMESSAGE) return;
		int channel=(int)(*bin)[0];
		if(channel!=GNSCHANNEL_INFO&&channel!=GNSCHANNEL_DATA) return;
		std::shared_ptr<RtcPeer> p=weak.lock();
		if(!p) return;
		std::lock_guard<std::mutex> l(lock);
		std::deque<std::string> &q=p->queue[channel];
		if(q.size()>=RTCMAXQUEUE) q.pop_front();
		q.push_back(std::string((const char *)bin->data()+1,bin->size()-1));
	});
	if(dc->isOpen())   //opened before the callback was there
	{
		std::lock_guard<std::mutex> l(lock);
		p->up=1;
	}
}

//1 if identity may make an offer now (at most one a second)
static int OfferAllowed(const std::string &identity,unsigned int now)
{
	for(size_t i=0;i<lastoffers.size();i++)
		if(lastoffers[i].first==identity&&now-lastoffers[i].second<RTCOFFERGAP) return 0;
	if(lastoffers.size()>=64) lastoffers.pop_front();
	lastoffers.push_back(std::make_pair(identity,now));
	return 1;
}

//the server's info link, 0 if we don't host
static RtcInfo *HostLink();

//the callbacks of a new connection with p, both ways (game thread)
static void Watch(const std::shared_ptr<RtcPeer> &p,char descriptionkind)
{
	std::weak_ptr<RtcPeer> weak=p;
	std::string identity=p->identity,cid=p->cid;   //copies: the callbacks may outlive p
	p->pc->onLocalDescription([identity,cid,descriptionkind](rtc::Description d)
	{
		Signal(identity,descriptionkind,cid,std::string(d),"");
	});
	p->pc->onLocalCandidate([identity,cid](rtc::Candidate c)
	{
		Signal(identity,'c',cid,c.candidate(),c.mid());
	});
	p->pc->onStateChange([weak](rtc::PeerConnection::State state)
	{
		if(state!=rtc::PeerConnection::State::Failed&&state!=rtc::PeerConnection::State::Closed) return;
		std::shared_ptr<RtcPeer> p=weak.lock();
		if(!p) return;
		std::lock_guard<std::mutex> l(lock);
		p->closed=1;
	});
}

static std::shared_ptr<rtc::PeerConnection> NewConnection()
{
	rtc::Configuration c;
	{
		std::lock_guard<std::mutex> l(lock);
		c=config;
	}
	return std::make_shared<rtc::PeerConnection>(c);
}

static void Offer(const std::string &sender,const SignalRtc &s,unsigned int now)
{
	std::shared_ptr<RtcPeer> old=FindIdentity(sender,0);
	if(old&&old->cid==s.cid) return;   //sent twice
	if(!OfferAllowed(sender,now)) return;
	if(old)
	{
		//a new try from the same browser: its last one goes, unless a player uses it
		if(old->refs) return;
		Forget(old.get());
	}
	RtcInfo *host=HostLink();
	if(peers.size()>=RTCMAXPEERS||!host)
	{
		fprintf(stderr,"webrtc: offer from %s ignored (%s)\n",sender.c_str(),!host?"not hosting":"too many browsers");
		return;
	}
	std::shared_ptr<RtcPeer> p=std::make_shared<RtcPeer>();
	p->identity=sender;
	p->cid=s.cid;
	p->addr=PseudoAddress();
	p->owner=host;
	p->refs=0;
	p->created=now;
	p->told=0;
	p->outgoing=0;
	p->up=0;
	p->closed=0;
	try
	{
		p->pc=NewConnection();
	}
	catch(const std::exception &e)
	{
		fprintf(stderr,"webrtc: can't create a connection: %s\n",e.what());
		return;
	}
	Watch(p,'a');
	std::weak_ptr<RtcPeer> weak=p;
	p->pc->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> dc)
	{
		TakeChannel(weak,dc);
	});
	peers.push_back(p);
	fprintf(stderr,"webrtc: offer from %s (as %s)\n",sender.c_str(),AddrText(&p->addr));
	try
	{
		p->pc->setRemoteDescription(rtc::Description(s.a,rtc::Description::Type::Offer));
	}
	catch(const std::exception &e)
	{
		fprintf(stderr,"webrtc: bad offer from %s: %s\n",sender.c_str(),e.what());
		Forget(p.get());
	}
}

//a candidate of either side, or the answer of a browser host to our offer
static void Remote(const std::string &sender,const SignalRtc &s)
{
	for(size_t i=0;i<peers.size();i++)
	{
		std::shared_ptr<RtcPeer> p=peers[i];
		if(p->identity!=sender||p->cid!=s.cid) continue;
		if(s.kind=='a'&&!p->outgoing) continue;   //we answer those
		try
		{
			if(s.kind=='a') p->pc->setRemoteDescription(rtc::Description(s.a,rtc::Description::Type::Answer));
			else p->pc->addRemoteCandidate(rtc::Candidate(s.a,s.b));
		}
		catch(const std::exception &e)
		{
			fprintf(stderr,"webrtc: bad %s from %s: %s\n",s.kind=='a'?"answer":"candidate",sender.c_str(),e.what());
		}
	}
}

//game thread: the signals that came in, and peers that are gone
static void Pump()
{
	std::deque<PendingSignal> in;
	{
		std::lock_guard<std::mutex> l(lock);
		in.swap(signals);
	}
	unsigned int now=Now();
	for(size_t i=0;i<in.size();i++)
	{
		if(in[i].s.kind=='o') Offer(in[i].sender,in[i].s,now);
		else Remote(in[i].sender,in[i].s);
	}
	for(size_t i=peers.size();i-->0;)
	{
		RtcPeer *p=peers[i].get();
		if(!p->told)
		{
			int up;
			{
				std::lock_guard<std::mutex> l(lock);
				up=p->up;
			}
			if(up)
			{
				//the route ICE took, as GnsLink tells it (tests check it)
				p->told=1;
				if(p->outgoing)
				{
					fprintf(stderr,"webrtc: DataChannel open with %s\n",p->identity.c_str());
					for(size_t k=0;k<p->pending.size();k++)
						SendRaw(p,p->pending[k]);
					p->pending.clear();
				}
				rtc::Candidate local,remote;
				const char *route="";
				try
				{
					if(p->pc->getSelectedCandidatePair(&local,&remote))
					{
						route=local.type()==rtc::Candidate::Type::Relayed||remote.type()==rtc::Candidate::Type::Relayed?
							", relayed (TURN)":", direct";
						if(getenv("HK_RTC_DEBUG"))
							fprintf(stderr,"webrtc: route %s <-> %s\n",std::string(local).c_str(),std::string(remote).c_str());
					}
				}
				catch(const std::exception &) {}
				fprintf(stderr,"webrtc: connected with %s, encrypted (DTLS)%s\n",p->identity.c_str(),route);
			}
		}
		if(p->refs) continue;   //its player's link sees it closed, the server times it out
		int up,closed;
		{
			std::lock_guard<std::mutex> l(lock);
			up=p->up;
			closed=p->closed;
		}
		if(closed||!p->owner||(!up&&now-p->created>RTCOPENTIME)) Forget(p);
	}
}

static void Log(rtc::LogLevel,std::string message)
{
	fprintf(stderr,"webrtc: %s\n",message.c_str());
}

void RtcHostConfigure(const char *stun,const char *turn,const char *turnusers,const char *turnpasses,int relayonly)
{
	if(!logging)
	{
		//HK_RTC_DEBUG=1: libdatachannel's own messages, ICE included (debugging)
		rtc::InitLogger(getenv("HK_RTC_DEBUG")?rtc::LogLevel::Debug:rtc::LogLevel::Warning,Log);
		logging=1;
	}
	rtc::Configuration c;
	//"host:port,host:port" (SignalIceServer made them, so each part is valid)
	auto each=[](const char *list,std::vector<std::string> *out)
	{
		std::string item;
		for(const char *p=list?list:"";;p++)
		{
			if(*p&&*p!=',') { item+=*p; continue; }
			if(!item.empty()) out->push_back(item);
			item.clear();
			if(!*p) break;
		}
	};
	std::vector<std::string> stuns,turns,users,passes;
	each(stun,&stuns);
	each(turn,&turns);
	each(turnusers,&users);
	each(turnpasses,&passes);
	auto split=[](const std::string &hp,std::string *host,int *port)
	{
		size_t colon=hp.rfind(':');
		if(colon==std::string::npos) return 0;
		*host=hp.substr(0,colon);
		*port=atoi(hp.c_str()+colon+1);
		return *port>0&&*port<65536&&!host->empty()?1:0;
	};
	std::string host;
	int port;
	for(size_t i=0;i<stuns.size();i++)
		if(split(stuns[i],&host,&port)) c.iceServers.emplace_back(host,(uint16_t)port);
	for(size_t i=0;i<turns.size()&&i<users.size()&&i<passes.size();i++)
		if(split(turns[i],&host,&port))
			c.iceServers.emplace_back(host,(uint16_t)port,users[i],passes[i],rtc::IceServer::RelayType::TurnUdp);
	c.iceTransportPolicy=relayonly?rtc::TransportPolicy::Relay:rtc::TransportPolicy::All;
	c.maxMessageSize=RTCMAXMESSAGE;
	std::lock_guard<std::mutex> l(lock);
	config=c;
}

void RtcHostSignal(const char *sender,const SignalRtc &signal)
{
	if(hostlinks.load()<=0) return;
	std::lock_guard<std::mutex> l(lock);
	if(signals.size()>=RTCMAXSIGNALS) return;   //the game thread isn't keeping up
	PendingSignal p;
	p.sender=sender;
	p.s=signal;
	signals.push_back(p);
}

//---- links ----

class RtcInfo : public DatagramLink
{
public:
	RtcInfo(DatagramLink *in,int h) : host(h), inner(in), next(0) {}
	int host;   //a server's: takes offers
	~RtcInfo()
	{
		for(size_t i=peers.size();i-->0;)
		{
			if(i>=peers.size()||peers[i]->owner!=this) continue;
			if(peers[i]->refs) peers[i]->owner=0;   //the player goes on with it
			else Forget(peers[i].get());
		}
		for(size_t i=0;i<infolinks.size();i++)
			if(infolinks[i]==this)
			{
				infolinks.erase(infolinks.begin()+i);
				break;
			}
		hostlinks--;
		delete inner;
	}
	int SendTo(const void *data,int size,const SOCKADDR_IN *to)
	{
		RtcPeer *p=FindAddr(to);
		if(p) return p->owner==this?SendOn(p,GNSCHANNEL_INFO,data,size):-1;
		return inner?inner->SendTo(data,size,to):-1;
	}
	int ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from)
	{
		if(inner)
		{
			int got=inner->ReceiveFrom(data,maxsize,from);
			if(got) return got;
		}
		Pump();
		int n=(int)peers.size();
		for(int i=0;i<n;i++)
		{
			RtcPeer *p=peers[(next+i)%n].get();
			if(p->owner!=this) continue;
			int got=Pop(p,GNSCHANNEL_INFO,data,maxsize,from);
			if(!got) continue;
			next=(next+i+1)%n;
			return got;
		}
		return 0;
	}
private:
	DatagramLink *inner;
	int next;
};

class RtcData : public DatagramLink
{
public:
	RtcData(std::shared_ptr<RtcPeer> peer) : p(peer) { p->refs++; }
	~RtcData()
	{
		p->refs--;
		if(!p->refs) Forget(p.get());   //its player left
	}
	int SendTo(const void *data,int size,const SOCKADDR_IN *)
	{
		return SendOn(p.get(),GNSCHANNEL_DATA,data,size);
	}
	int ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from)
	{
		return Pop(p.get(),GNSCHANNEL_DATA,data,maxsize,from);
	}
private:
	std::shared_ptr<RtcPeer> p;
};

static RtcInfo *HostLink()
{
	for(size_t i=infolinks.size();i-->0;)
		if(infolinks[i]->host) return infolinks[i];
	return 0;
}

DatagramLink *RtcHostInfoLink(DatagramLink *inner,int host)
{
	RtcInfo *link=new RtcInfo(inner,host);
	infolinks.push_back(link);
	hostlinks++;
	return link;
}

int RtcHostIsInfoLink(DatagramLink *link)
{
	return link&&dynamic_cast<RtcInfo *>(link)!=0;
}

int RtcHostConnect(const SOCKADDR_IN *addr,const char *identity)
{
	if(infolinks.empty()) return 0;
	RtcInfo *link=infolinks.back();
	RtcPeer *had=FindAddr(addr);
	if(had&&had->owner==link) return 1;
	if(had&&!had->refs) Forget(had);
	else if(had) return 0;
	if(!SignalValidIdentity(identity)) return 0;
	std::shared_ptr<RtcPeer> p=std::make_shared<RtcPeer>();
	p->identity=identity;
	//a new id for each try: the host drops what it had of an older one
	static std::mt19937 rng(std::random_device{}());
	static const char digits[]="0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	for(int i=0;i<12;i++) p->cid+=digits[rng()%62];
	p->addr=*addr;
	p->owner=link;
	p->refs=0;
	p->created=Now();
	p->told=0;
	p->outgoing=1;
	p->up=0;
	p->closed=0;
	try
	{
		p->pc=NewConnection();
		Watch(p,'o');
		//as a browser's: unordered, never retransmitted (Sock has its own reliable
		//channel); creating it makes the offer
		rtc::DataChannelInit init;
		init.reliability.unordered=true;
		init.reliability.maxRetransmits=0;
		peers.push_back(p);
		TakeChannel(p,p->pc->createDataChannel("edifice",init));
	}
	catch(const std::exception &e)
	{
		fprintf(stderr,"webrtc: can't connect to %s: %s\n",identity,e.what());
		Forget(p.get());
		return 0;
	}
	fprintf(stderr,"webrtc: connecting to %s\n",identity);
	return 1;
}

DatagramLink *RtcHostOpenData(const SOCKADDR_IN *peer)
{
	for(size_t i=0;i<peers.size();i++)
		if(peers[i]->addr.sin_addr.s_addr==peer->sin_addr.s_addr&&peers[i]->addr.sin_port==peer->sin_port)
			return new RtcData(peers[i]);
	return 0;
}

int RtcHostIsPeer(const SOCKADDR_IN *peer)
{
	RtcPeer *p=FindAddr(peer);
	if(!p||p->outgoing) return 0;
	std::lock_guard<std::mutex> l(lock);
	return p->up&&!p->closed;
}

void RtcHostShutdown()
{
	while(!peers.empty()) Forget(peers.back().get());
	std::lock_guard<std::mutex> l(lock);
	signals.clear();
}
