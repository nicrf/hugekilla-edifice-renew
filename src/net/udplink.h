//UdpLink: a DatagramLink over a plain UDP socket (sendto/recvfrom), used for the LAN
//and direct IP games. Same socket calls as Sock made before M7a, so nothing changes
//on the wire.

#ifndef UDPLINKH
#define UDPLINKH

#include "datagramlink.h"

class UdpLink : public DatagramLink
{
public:
	UdpLink();
	~UdpLink();
	//port in network order, 0 for any; returns 1 when bound. A failed bind leaves the
	//socket usable (the system picks a port at the first send), and it can be retried.
	int Bind(WORD port);
	void SetBroadcast();
	//creates the socket, binds it to port and enables broadcast if asked
	//(the bind result is ignored, as for the info socket before)
	static UdpLink *Open(WORD port,int broadcast);

	int SendTo(const void *data,int size,const SOCKADDR_IN *to);
	int ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from);
private:
	SOCKET sock;
};

#endif
