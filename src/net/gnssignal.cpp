//Signaling client of GnsLink's P2P connections (see gnssignal.h)

#include <StdAfx.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include "gnssignal.h"
#include "signaling.h"
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#endif

#define SIGNAL_MAXQUEUE 64        //lines waiting to be sent: then the oldest go
#define SIGNAL_MAXEXPECTED 16     //hosts a client takes signals from

static std::mutex lock;           //everything below, but the thread's own socket
static std::thread worker;
static std::atomic<int> stopping(0);
static int running;
static std::string relayhost,identity,hello;   //hello: "IDENTITY TOKEN\n", never logged
static int relayport;
static std::deque<std::string> queue;
static std::deque<std::string> expected;
static int listening;

static DWORD Now()
{
	using namespace std::chrono;
	return (DWORD)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static int WouldBlock()
{
#ifdef _WIN32
	int e=WSAGetLastError();
	return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS;
#else
	return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS||errno==EINTR;
#endif
}

static void NonBlocking(SOCKET s)
{
#ifdef _WIN32
	u_long on=1;
	ioctlsocket(s,FIONBIO,&on);
#else
	fcntl(s,F_SETFL,fcntl(s,F_GETFL,0)|O_NONBLOCK);
#ifdef SO_NOSIGPIPE
	int on=1;
	setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof(on));
#endif
#endif
}

//a TCP connection to host:port within 5 s, nonblocking; INVALID_SOCKET if none
static SOCKET Dial(const std::string &host,int port)
{
	addrinfo hints,*res=0;
	memset(&hints,0,sizeof(hints));
	hints.ai_family=AF_UNSPEC;
	hints.ai_socktype=SOCK_STREAM;
	char service[8];
	snprintf(service,sizeof(service),"%d",port);
	if(getaddrinfo(host.c_str(),service,&hints,&res)!=0||!res) return INVALID_SOCKET;
	SOCKET s=INVALID_SOCKET;
	for(addrinfo *a=res;a&&s==INVALID_SOCKET&&!stopping.load();a=a->ai_next)
	{
		s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);
		if(s==INVALID_SOCKET) continue;
		NonBlocking(s);
		if(connect(s,a->ai_addr,(int)a->ai_addrlen)!=0&&!WouldBlock())
		{
			closesocket(s);
			s=INVALID_SOCKET;
			continue;
		}
		fd_set w,e;
		FD_ZERO(&w);
		FD_ZERO(&e);
		FD_SET(s,&w);
		FD_SET(s,&e);
		timeval tv={5,0};
		int err=0;
		socklen_t len=sizeof(err);
		if(select((int)s+1,0,&w,&e,&tv)<=0||!FD_ISSET(s,&w)||
			getsockopt(s,SOL_SOCKET,SO_ERROR,(char *)&err,&len)!=0||err)
		{
			closesocket(s);
			s=INVALID_SOCKET;
		}
	}
	freeaddrinfo(res);
	return s;
}

static int SendFlags()
{
#ifdef MSG_NOSIGNAL
	return MSG_NOSIGNAL;
#else
	return 0;
#endif
}

static int Expected(const std::string &peer)
{
	for(size_t i=0;i<expected.size();i++)
		if(expected[i]==peer) return 1;
	return 0;
}

//the signaling of one connection: its signals go to the queue, as lines for peer
struct PeerSignaling : ISteamNetworkingConnectionSignaling
{
	std::string peer;
	PeerSignaling(const char *p) : peer(p) {}
	virtual bool SendSignal(HSteamNetConnection,const SteamNetConnectionInfo_t &,const void *msg,int size)
	{
		std::string line;
		if(!SignalFormatLine(peer.c_str(),msg,size,&line))
		{
			fprintf(stderr,"signaling: a %d-byte signal doesn't fit in a line, dropped\n",size);
			return true;   //best effort, like the relay
		}
		std::lock_guard<std::mutex> l(lock);
		if(!running) return true;
		if(queue.size()>=SIGNAL_MAXQUEUE) queue.pop_front();
		queue.push_back(line);
		return true;
	}
	virtual void Release()
	{
		delete this;
	}
};

//what GameNetworkingSockets asks about a signal while it reads it
struct Context : ISteamNetworkingSignalingRecvContext
{
	std::string sender;   //vouched for by the relay
	int hosting;
	virtual ISteamNetworkingConnectionSignaling *OnConnectRequest(HSteamNetConnection,const SteamNetworkingIdentity &peer,int)
	{
		char text[SteamNetworkingIdentity::k_cchMaxString];
		peer.ToString(text,sizeof(text));
		if(!hosting||sender!=text)
		{
			fprintf(stderr,"signaling: connection request from %s ignored\n",sender.c_str());
			return 0;   //ignored, not refused: nobody learns we are here
		}
		return new PeerSignaling(text);
	}
	virtual void SendRejectionSignal(const SteamNetworkingIdentity &,const void *,int)
	{
	}
};

//a line from the relay; 0 when it refused our session
static int Handle(const std::string &line)
{
	static char sender[SIGNAL_MAXIDENTITY+1],claimed[SIGNAL_MAXIDENTITY+1];
	static unsigned char payload[SIGNAL_MAXPAYLOAD];
	int size;
	int r=SignalParseLine(line.data(),(int)line.size(),sender,sizeof(sender),payload,sizeof(payload),&size);
	if(r<0) return 0;
	if(r==0) return 1;   //not a signal: ignored
	if(!SignalFromIdentity(payload,size,claimed,sizeof(claimed))||strcmp(claimed,sender))
	{
		fprintf(stderr,"signaling: a signal from %s claims to come from someone else, dropped\n",sender);
		return 1;
	}
	Context ctx;
	ctx.sender=sender;
	{
		std::lock_guard<std::mutex> l(lock);
		ctx.hosting=listening>0;
		if(!ctx.hosting&&!Expected(ctx.sender)) return 1;   //not someone we talk to
	}
	//without our lock: GameNetworkingSockets may send signals (SendSignal) meanwhile
	SteamNetworkingSockets()->ReceivedP2PCustomSignal(payload,size,&ctx);
	return 1;
}

static int Wanted()
{
	std::lock_guard<std::mutex> l(lock);
	return listening>0||!queue.empty();
}

static void Work()
{
	SOCKET s=INVALID_SOCKET;
	SignalReader reader;
	std::string out,where,me,greeting;
	DWORD retryat=0,backoff=1000;
	int warned=0;
	char buf[4096];
	{
		std::lock_guard<std::mutex> l(lock);
		char text[300];
		snprintf(text,sizeof(text),"%s:%d",relayhost.c_str(),relayport);
		where=text;
		me=identity;
	}
	while(!stopping.load())
	{
		if(s==INVALID_SOCKET)
		{
			if(!Wanted()||(int)(Now()-retryat)<0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			std::string host;
			int port;
			{
				std::lock_guard<std::mutex> l(lock);
				host=relayhost;
				port=relayport;
				greeting=hello;
			}
			s=Dial(host,port);
			if(s==INVALID_SOCKET)
			{
				if(!warned++) fprintf(stderr,"signaling: can't reach %s, trying again\n",where.c_str());
				retryat=Now()+backoff;
				backoff=backoff*2>30000?30000:backoff*2;
				continue;
			}
			fprintf(stderr,"signaling: connected to %s as %s\n",where.c_str(),me.c_str());
			out=greeting;
			reader.Clear();
			warned=0;
		}
		{
			std::lock_guard<std::mutex> l(lock);
			while(!queue.empty())
			{
				out+=queue.front();
				queue.pop_front();
			}
		}
		fd_set r,w;
		FD_ZERO(&r);
		FD_ZERO(&w);
		FD_SET(s,&r);
		if(!out.empty()) FD_SET(s,&w);
		timeval tv={0,50000};
		if(select((int)s+1,&r,&w,0,&tv)<0&&!WouldBlock()) goto lost;
		if(FD_ISSET(s,&w))
		{
			int n=send(s,out.data(),(int)out.size(),SendFlags());
			if(n>0) out.erase(0,n);
			else if(n<0&&!WouldBlock()) goto lost;
		}
		if(FD_ISSET(s,&r))
		{
			int n=recv(s,buf,sizeof(buf),0);
			if(n==0||(n<0&&!WouldBlock())) goto lost;
			if(n>0)
			{
				reader.Add(buf,n);
				std::string line;
				while(reader.Next(&line))
					if(!Handle(line))
					{
						fprintf(stderr,"signaling: the lobby refused our session\n");
						closesocket(s);
						s=INVALID_SOCKET;
						retryat=Now()+60000;
						break;
					}
			}
		}
		if(s!=INVALID_SOCKET) backoff=1000;
		continue;
lost:
		fprintf(stderr,"signaling: connection to %s lost\n",where.c_str());
		closesocket(s);
		s=INVALID_SOCKET;
		retryat=Now()+backoff;
		backoff=backoff*2>30000?30000:backoff*2;
	}
	if(s!=INVALID_SOCKET) closesocket(s);
}

void GnsSignalStart(const char *host,int port,const char *id,const char *token)
{
	std::string greeting;
	if(!SignalHelloLine(id,token,&greeting)) return;
	{
		std::lock_guard<std::mutex> l(lock);
		if(running&&relayhost==host&&relayport==port&&hello==greeting) return;
	}
	GnsSignalStop();
	std::lock_guard<std::mutex> l(lock);
	relayhost=host;
	relayport=port;
	identity=id;
	hello=greeting;
	queue.clear();
	stopping=0;
	running=1;
	worker=std::thread(Work);
}

void GnsSignalStop()
{
	{
		std::lock_guard<std::mutex> l(lock);
		if(!running) return;
		stopping=1;
	}
	worker.join();
	std::lock_guard<std::mutex> l(lock);
	running=0;
	queue.clear();
	hello.clear();
}

ISteamNetworkingConnectionSignaling *GnsSignalFor(const char *peer)
{
	return new PeerSignaling(peer);
}

void GnsSignalExpect(const char *peer)
{
	std::lock_guard<std::mutex> l(lock);
	if(Expected(peer)) return;
	if(expected.size()>=SIGNAL_MAXEXPECTED) expected.pop_front();
	expected.push_back(peer);
}

void GnsSignalListen(int count)
{
	std::lock_guard<std::mutex> l(lock);
	listening=count;
}
