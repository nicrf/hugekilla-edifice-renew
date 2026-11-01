//Signaling client of GnsLink's P2P connections (see gnssignal.h)

#include "win32compat.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <random>
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
#ifdef EDIFICE_LOBBY_CURL
#include <curl/curl.h>
#define SIGNAL_WS 1               //the WebSocket relay (wss://) is usable
#endif

#define SIGNAL_MAXQUEUE 64        //lines waiting to be sent: then the oldest go
#define SIGNAL_MAXEXPECTED 16     //hosts a client takes signals from

static std::mutex lock;           //everything below, but the thread's own socket
static std::thread worker;
static std::atomic<int> stopping(0);
static int running;
static std::string relayhost,identity,hello;   //hello: "IDENTITY TOKEN\n", never logged
static int relayport;
static std::string relayurl;      //the WebSocket relay, "" for TCP to relayhost:relayport
#ifdef SIGNAL_WS
static int curlstarted;           //we called curl_global_init
#endif
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

//---- the connection to the relay: TCP, or WebSocket on the lobby's HTTP(S) API ----

struct Conn
{
	SOCKET s;
	int ws;
#ifdef SIGNAL_WS
	CURL *curl;
#endif
	SignalReader lines;
	SignalWsReader frames;
	std::mt19937 rng;      //WebSocket masks and keys (they needn't be secret)
};

#ifdef SIGNAL_WS
static int Aborted(void *,curl_off_t,curl_off_t,curl_off_t,curl_off_t)
{
	return stopping.load();   //nonzero aborts
}

//waits up to ms for s to be readable (or writable)
static void WaitSocket(SOCKET s,int write,int ms)
{
	fd_set f;
	FD_ZERO(&f);
	FD_SET(s,&f);
	timeval tv={ms/1000,(ms%1000)*1000};
	select((int)s+1,write?0:&f,write?&f:0,0,&tv);
}

//the upgrade to WebSocket over curl's connection, 5 s at most; what came after the
//server's answer goes to leftover
static int Handshake(CURL *curl,SOCKET s,const std::string &request,std::string *leftover,std::string *why)
{
	DWORD deadline=Now()+5000;
	size_t sent=0;
	while(sent<request.size())
	{
		if(stopping.load()||(int)(Now()-deadline)>0) { *why="timed out"; return 0; }
		size_t n=0;
		CURLcode r=curl_easy_send(curl,request.data()+sent,request.size()-sent,&n);
		if(r==CURLE_AGAIN) { WaitSocket(s,1,50); continue; }
		if(r!=CURLE_OK) { *why=curl_easy_strerror(r); return 0; }
		sent+=n;
	}
	std::string answer;
	char buf[1024];
	while(1)
	{
		if(stopping.load()||(int)(Now()-deadline)>0) { *why="timed out"; return 0; }
		size_t n=0;
		CURLcode r=curl_easy_recv(curl,buf,sizeof(buf),&n);
		if(r==CURLE_AGAIN) { WaitSocket(s,0,50); continue; }
		if(r!=CURLE_OK||n==0) { *why=r!=CURLE_OK?curl_easy_strerror(r):"connection closed"; return 0; }
		answer.append(buf,n);
		int end=SignalWsResponse(answer.data(),(int)answer.size());
		if(end<0) { *why="the server refused the WebSocket upgrade"; return 0; }
		if(end>0)
		{
			leftover->assign(answer,end,std::string::npos);
			return 1;
		}
	}
}

//TCP and TLS (wss://) by libcurl, then the upgrade by hand: curl_ws_* only come
//with curl 7.86 and were experimental, off in most builds, up to 8.11 (none in
//Ubuntu 22.04's 7.81 nor in macOS' 8.7); CONNECT_ONLY with curl_easy_send/recv
//work with all of them.
static int OpenWs(Conn &c,const std::string &url,std::string *leftover,std::string *why)
{
	char host[254],path[200],target[300];
	int port,tls;
	if(!SignalParseURL(url.c_str(),host,sizeof(host),&port,path,sizeof(path),&tls)) { *why="bad URL"; return 0; }
	CURL *curl=curl_easy_init();
	if(!curl) { *why="libcurl failed to start"; return 0; }
	snprintf(target,sizeof(target),"%s://%s:%d/",tls?"https":"http",host,port);
	char error[CURL_ERROR_SIZE]="";
	curl_easy_setopt(curl,CURLOPT_URL,target);
	curl_easy_setopt(curl,CURLOPT_CONNECT_ONLY,1L);
	curl_easy_setopt(curl,CURLOPT_HTTP_VERSION,(long)CURL_HTTP_VERSION_1_1);   //ALPN: the upgrade is HTTP/1.1 only
	curl_easy_setopt(curl,CURLOPT_HTTPPROXYTUNNEL,1L);   //through a proxy, if any: a tunnel
	curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
	curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);
	curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
	curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,Aborted);
	curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,error);
	//tests only: a lobby with a self-signed certificate (tests/net_p2p.py); the
	//certificate is still checked, against that file instead of the system's
	const char *ca=getenv("EDIFICE_TEST_CA_FILE");
	if(ca&&ca[0]) curl_easy_setopt(curl,CURLOPT_CAINFO,ca);
	curl_socket_t s=CURL_SOCKET_BAD;
	CURLcode r=curl_easy_perform(curl);
	if(r!=CURLE_OK||curl_easy_getinfo(curl,CURLINFO_ACTIVESOCKET,&s)!=CURLE_OK||s==CURL_SOCKET_BAD)
	{
		*why=error[0]?error:curl_easy_strerror(r);
		curl_easy_cleanup(curl);
		return 0;
	}
	unsigned char key[16];
	for(int i=0;i<16;i++) key[i]=(unsigned char)c.rng();
	if(!Handshake(curl,(SOCKET)s,SignalWsRequest(host,port,tls,path,key),leftover,why))
	{
		curl_easy_cleanup(curl);
		return 0;
	}
	c.curl=curl;
	c.s=(SOCKET)s;
	return 1;
}
#endif

static void Close(Conn &c)
{
#ifdef SIGNAL_WS
	if(c.ws)
	{
		if(c.curl) curl_easy_cleanup(c.curl);   //closes c.s
		c.curl=0;
		c.s=INVALID_SOCKET;
		return;
	}
#endif
	if(c.s!=INVALID_SOCKET) closesocket(c.s);
	c.s=INVALID_SOCKET;
}

//bytes sent, 0 if it would block, -1 if the connection is lost
static int Send(Conn &c,const std::string &out)
{
#ifdef SIGNAL_WS
	if(c.ws)
	{
		size_t n=0;
		CURLcode r=curl_easy_send(c.curl,out.data(),out.size(),&n);
		if(r==CURLE_AGAIN) return 0;
		return r==CURLE_OK?(int)n:-1;
	}
#endif
	int n=send(c.s,out.data(),(int)out.size(),SendFlags());
	if(n<0) return WouldBlock()?0:-1;
	return n;
}

//bytes received, 0 if none now, -1 if the connection is lost or closed
static int Recv(Conn &c,char *buf,int size)
{
#ifdef SIGNAL_WS
	if(c.ws)
	{
		size_t n=0;
		CURLcode r=curl_easy_recv(c.curl,buf,size,&n);
		if(r==CURLE_AGAIN) return 0;
		return r==CURLE_OK&&n>0?(int)n:-1;
	}
#endif
	int n=recv(c.s,buf,size,0);
	if(n==0) return -1;
	if(n<0) return WouldBlock()?0:-1;
	return n;
}

//a line ("...\n") as it goes on the connection
static void Put(Conn &c,const std::string &line,std::string *out)
{
	if(!c.ws)
	{
		*out+=line;
		return;
	}
	unsigned char mask[4];
	for(int i=0;i<4;i++) mask[i]=(unsigned char)c.rng();
	int n=(int)line.size();
	if(n&&line[n-1]=='\n') n--;
	SignalWsFrame(SIGNAL_WS_TEXT,line.data(),n,mask,out);
}

//what came from the relay: 1, 0 when it refused our session, -1 when the
//connection is to be dropped (WebSocket protocol error or close)
static int Received(Conn &c,const char *data,int n,std::string *out)
{
	std::string line;
	if(!c.ws)
	{
		c.lines.Add(data,n);
		while(c.lines.Next(&line))
			if(!Handle(line)) return 0;
		return 1;
	}
	c.frames.Add(data,n);
	int opcode,r;
	while((r=c.frames.Next(&opcode,&line))==1)
	{
		if(opcode==SIGNAL_WS_TEXT||opcode==SIGNAL_WS_BINARY)
		{
			if(!Handle(line)) return 0;
		}
		else if(opcode==SIGNAL_WS_PING)
		{
			unsigned char mask[4];
			for(int i=0;i<4;i++) mask[i]=(unsigned char)c.rng();
			SignalWsFrame(SIGNAL_WS_PONG,line.data(),(int)line.size(),mask,out);
		}
		else if(opcode==SIGNAL_WS_CLOSE) return -1;
	}
	return r<0?-1:1;
}

static void Work()
{
	Conn c;
	c.s=INVALID_SOCKET;
#ifdef SIGNAL_WS
	c.curl=0;
#endif
	std::random_device seed;
	c.rng.seed(seed());
	std::string out,where,me,greeting;
	DWORD retryat=0,backoff=1000;
	int warned=0;
	char buf[4096];
	{
		std::lock_guard<std::mutex> l(lock);
		c.ws=!relayurl.empty();
		char text[300];
		snprintf(text,sizeof(text),"%s:%d",relayhost.c_str(),relayport);
		where=c.ws?relayurl:std::string(text);
		me=identity;
	}
	while(!stopping.load())
	{
		if(c.s==INVALID_SOCKET)
		{
			if(!Wanted()||(int)(Now()-retryat)<0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			std::string host,why,leftover;
			int port;
			{
				std::lock_guard<std::mutex> l(lock);
				host=relayhost;
				port=relayport;
				greeting=hello;
			}
#ifdef SIGNAL_WS
			if(c.ws) OpenWs(c,where,&leftover,&why);
			else
#endif
				c.s=Dial(host,port);
			if(c.s==INVALID_SOCKET)
			{
				if(!warned++) fprintf(stderr,"signaling: can't reach %s%s%s%s, trying again\n",where.c_str(),
					why.empty()?"":" (",why.c_str(),why.empty()?"":")");
				retryat=Now()+backoff;
				backoff=backoff*2>30000?30000:backoff*2;
				continue;
			}
			fprintf(stderr,"signaling: connected to %s as %s\n",where.c_str(),me.c_str());
			out.clear();
			Put(c,greeting,&out);
			c.lines.Clear();
			c.frames.Clear();
			warned=0;
			if(!leftover.empty())
			{
				int r=Received(c,leftover.data(),(int)leftover.size(),&out);
				if(r==0) goto refused;
				if(r<0) goto lost;
			}
		}
		{
			std::lock_guard<std::mutex> l(lock);
			while(!queue.empty())
			{
				Put(c,queue.front(),&out);
				queue.pop_front();
			}
		}
		{
			fd_set r,w;
			FD_ZERO(&r);
			FD_ZERO(&w);
			FD_SET(c.s,&r);
			if(!out.empty()) FD_SET(c.s,&w);
			timeval tv={0,50000};
			if(select((int)c.s+1,&r,&w,0,&tv)<0&&!WouldBlock()) goto lost;
			//over TLS, data may wait in curl's buffers that select doesn't see:
			//a WebSocket is always tried
			if(!out.empty()&&(c.ws||FD_ISSET(c.s,&w)))
			{
				int n=Send(c,out);
				if(n<0) goto lost;
				out.erase(0,n);
			}
			if(c.ws||FD_ISSET(c.s,&r))
			{
				int n;
				while((n=Recv(c,buf,sizeof(buf)))>0)
				{
					int got=Received(c,buf,n,&out);
					if(got==0) goto refused;
					if(got<0) goto lost;
					if(!c.ws) break;   //select again first
				}
				if(n<0) goto lost;
			}
		}
		backoff=1000;
		continue;
refused:
		fprintf(stderr,"signaling: the lobby refused our session\n");
		Close(c);
		retryat=Now()+60000;
		continue;
lost:
		fprintf(stderr,"signaling: connection to %s lost\n",where.c_str());
		Close(c);
		retryat=Now()+backoff;
		backoff=backoff*2>30000?30000:backoff*2;
	}
	Close(c);
}

void GnsSignalStart(const char *host,int port,const char *url,const char *id,const char *token)
{
	std::string greeting,useurl;
	if(!SignalHelloLine(id,token,&greeting)) return;
	if(url&&url[0])
	{
		char h[254],path[200];
		int p,tls;
		const char *tcp=getenv("HK_SIGNAL_TCP");   //tests: the TCP relay even when there is a WebSocket one
#ifdef SIGNAL_WS
		if(!SignalParseURL(url,h,sizeof(h),&p,path,sizeof(path),&tls))
			fprintf(stderr,"signaling: unusable relay URL %s\n",url);
		else if(!(tcp&&tcp[0]=='1')||!host||!host[0])
			useurl=url;
#else
		(void)h;(void)path;(void)p;(void)tls;(void)tcp;
		fprintf(stderr,"signaling: built without libcurl, no WebSocket relay\n");
#endif
	}
	if(useurl.empty()&&(!host||!host[0]||port<1))
	{
		fprintf(stderr,"signaling: no relay this build can use\n");
		return;
	}
	if(!host) host="";
	{
		std::lock_guard<std::mutex> l(lock);
		if(running&&relayhost==host&&relayport==port&&relayurl==useurl&&hello==greeting) return;
	}
	GnsSignalStop();
#ifdef SIGNAL_WS
	if(!useurl.empty()&&!curlstarted)
	{
		curl_global_init(CURL_GLOBAL_DEFAULT);   //counted: the lobby's worker has its own
		curlstarted=1;
	}
#endif
	std::lock_guard<std::mutex> l(lock);
	relayhost=host;
	relayport=port;
	relayurl=useurl;
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
#ifdef SIGNAL_WS
	if(curlstarted)
	{
		curl_global_cleanup();
		curlstarted=0;
	}
#endif
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
