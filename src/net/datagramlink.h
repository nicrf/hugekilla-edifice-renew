//DatagramLink is what Sock sends its datagrams through (roadmap M7, docs/M7.md).
//Sock keeps everything above it: grouping, the home-made reliable channel, its acks
//and callbacks, the IP filter. A link only moves whole datagrams, unreliably, to and
//from addresses.
//
//Peers are named by a SOCKADDR_IN whatever the transport, so that multiplay.cpp
//(dest, filterip, the server list) works unchanged: UdpLink uses real addresses, a
//connection-based link (GnsLink, M7b) gives each connection a unique pseudo-address.

#ifndef DATAGRAMLINKH
#define DATAGRAMLINKH

#include "hksocket.h"   //SOCKADDR_IN

class DatagramLink
{
public:
	virtual ~DatagramLink() {}
	//sends one datagram; returns the bytes sent, <=0 when it failed
	virtual int SendTo(const void *data,int size,const SOCKADDR_IN *to)=0;
	//reads one waiting datagram, without blocking: its size (>0), 0 when nothing is
	//waiting, -1 when a datagram was read but is unusable (error, empty)
	virtual int ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from)=0;
};

#endif
