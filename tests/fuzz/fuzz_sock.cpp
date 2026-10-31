// Sock::Receive (src/net/sock.cpp): datagram framing, grouped messages,
// reliable transfers and ACKs, driven from the input instead of a socket
// (Sock::Feed). Input: uint8 options, then datagrams as uint16 length + bytes.
//   options bits 0-2: maxsize given to Receive (SIMPLESIZE .. sizeof(Message))
//   bit 3: queue reliable data first (so that ACKs match something)
//   bit 4: filter by source IP (as once connected)
// Receive never returns more than maxsize bytes, and never touches memory
// outside its buffers.
#include "fuzz.h"
#include "sock.h"
#include <new>

DWORD GetTickCount() { return 0; }   //win32compat.cpp isn't linked

static const int maxsizes[8]={SIMPLESIZE,16,100,450,1000,4096,BIGBUFSIZE,(int)sizeof(Message)};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	static Sock *sock;
	if(!sock) sock=new Sock;
	sock->ResetReliable();   //frees the queue
	new(sock) Sock;          //all zero: no socket, nothing is sent
	if(size<1) return 0;
	int options=data[0];
	int maxsize=maxsizes[options&7];
	if(options&8)
	{
		static char payload[2000];
		memset(payload,'q',sizeof(payload));
		sock->SendReliable(payload,sizeof(payload),RECEIVEDLANDINFO);
		sock->SendReliable(payload,10,RECEIVEDPLAYERNAMES);
	}
	if(options&16)
	{
		sock->filterip=1;
		sock->dest.sin_addr.s_addr=htonl(0x7f000001);
	}
	char *out=(char *)malloc(maxsize);   //exact size: ASan catches an overflow
	size_t pos=1;
	while(pos+2<=size)
	{
		size_t len=data[pos]|(data[pos+1]<<8);
		pos+=2;
		if(len>size-pos) len=size-pos;
		//a copy of the exact length: ASan catches a read past the datagram
		char *datagram=(char *)malloc(len?len:1);
		memcpy(datagram,data+pos,len);
		pos+=len;
		SOCKADDR_IN source;
		memset(&source,0,sizeof(source));
		source.sin_addr.s_addr=htonl(0x7f000001);
		sock->Feed(datagram,(int)len);
		//0 also means an empty message, with more after it: stop after a few in a row
		for(int i=0,zeros=0;i<20000&&zeros<4;i++)
		{
			int n=sock->Receive(out,&source,maxsize);
			FUZZ_CHECK(n>=0&&n<=maxsize);
			zeros=n?0:zeros+1;
		}
		sock->feeddata=0;   //the datagram is freed below
		free(datagram);
	}
	free(out);
	return 0;
}
