//UdpLink: DatagramLink over a UDP socket (see udplink.h)

#include "win32compat.h"
#include "udplink.h"

UdpLink::UdpLink()
{
	sock=WSASocket(AF_INET,SOCK_DGRAM,0,0,0,WSA_FLAG_OVERLAPPED);
}

UdpLink::~UdpLink()
{
	if(sock) closesocket(sock);
}

int UdpLink::Bind(WORD port)
{
	if(!sock) return 0;
	SOCKADDR_IN self;
	ZeroMemory(&self,sizeof(self));
	self.sin_family=AF_INET;
	self.sin_port=port;
	return bind(sock,(SOCKADDR *)&self,sizeof(self))==0;
}

void UdpLink::SetBroadcast()
{
	BOOL broadcast=TRUE;
	setsockopt(sock,SOL_SOCKET,SO_BROADCAST,(char *)&broadcast,sizeof(broadcast));
}

UdpLink *UdpLink::Open(WORD port,int broadcast)
{
	UdpLink *link=new UdpLink;
	link->Bind(port);
	if(broadcast) link->SetBroadcast();
	return link;
}

int UdpLink::SendTo(const void *data,int size,const SOCKADDR_IN *to)
{
	return sendto(sock,(const char *)data,size,0,(const SOCKADDR *)to,sizeof(SOCKADDR_IN));
}

int UdpLink::ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from)
{
	if(!sock) return 0;
	fd_set recv;
	FD_ZERO(&recv);
	FD_SET(sock,&recv);
	timeval tm;
	tm.tv_sec=0;
	tm.tv_usec=0;
	if(select(sock+1,&recv,0,0,&tm)<=0) return 0;
#ifdef _WIN32
	int addrsize=sizeof(SOCKADDR_IN);
#else
	socklen_t addrsize=sizeof(SOCKADDR_IN);
#endif
	int received=recvfrom(sock,(char *)data,maxsize,0,(SOCKADDR *)from,&addrsize);
	return received>0?received:-1;
}
